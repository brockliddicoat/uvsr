//! Non-executed, generic instruction probes for the real compiler pipeline.
#![forbid(unsafe_code)]

use super::{assemble_spirv, link_with_linker_opts, load, validate};
use rspirv::binary::{Assemble, Disassemble};
use spirv_tools::opt::Optimizer;

fn check_required(words: &[u32], required: &[&str]) {
    validate(words);
    let bytes = spirv_tools::binary::from_binary(words);
    let module = load(bytes);
    let text = module.disassemble();
    for needle in required {
        assert!(text.contains(needle), "missing {needle:?}:\n{text}");
    }
    // The serializer must emit a module that the same parser can read again.
    validate(&load(spirv_tools::binary::from_binary(&module.assemble())).assemble());
}

fn parser(source: &str, required: &[&str]) {
    let binary = assemble_spirv(source);
    let module = load(&binary);
    check_required(&module.assemble(), required);
}

fn spirt(source: &str, required: &[&str]) {
    let binary = assemble_spirv(source);
    let cx = std::rc::Rc::new(spirt::Context::new());
    crate::custom_insts::register_to_spirt_context(&cx);
    let module =
        spirt::Module::lower_from_spv_bytes(cx, binary).expect("SPIR-T must accept the fixture");
    let words = module
        .lift_to_spv_module_emitter()
        .expect("SPIR-T must serialize the fixture")
        .words;
    check_required(&words, required);
}

fn linker(source: &str, required: &[&str], qptr: bool) {
    let binary = assemble_spirv(source);
    let module = link_with_linker_opts(
        &[&binary],
        &crate::linker::Options {
            compact_ids: true,
            early_report_zombies: true,
            infer_storage_classes: true,
            structurize: true,
            spirt_passes: if qptr { vec!["qptr".into()] } else { vec![] },
            ..Default::default()
        },
    )
    .expect("compiler linker must preserve the fixture");
    let words = module.assemble();
    check_required(&words, required);
    let mut optimizer = spirv_tools::opt::create(None);
    optimizer.register_performance_passes();
    let mut diagnostics = Vec::new();
    let optimized = optimizer
        .optimize(
            &words,
            &mut |message: spirv_tools::error::Message| diagnostics.push(message),
            None,
        )
        .unwrap_or_else(|err| panic!("optimizer failed: {err:?}; {diagnostics:?}"));
    check_required(optimized.as_words(), required);
}

macro_rules! instruction_case {
    ($name:ident, [$($required:literal),+ $(,)?]) => {
        mod $name {
            const SOURCE: &str = include_str!(concat!(stringify!($name), ".spvasm"));
            const REQUIRED: &[&str] = &[$($required),+];
            #[test]
            fn parser_roundtrip() { super::parser(SOURCE, REQUIRED); }
            #[test]
            fn spirt_roundtrip() { super::spirt(SOURCE, REQUIRED); }
            #[test]
            fn linker_default() { super::linker(SOURCE, REQUIRED, false); }
            #[test]
            fn linker_qptr() { super::linker(SOURCE, REQUIRED, true); }
        }
    };
}

instruction_case!(logical_store, ["OpMemoryModel Logical GLSL450", "OpStore"]);
instruction_case!(
    physical_store,
    [
        "OpMemoryModel PhysicalStorageBuffer64 GLSL450",
        "OpConvertUToPtr",
        "Aligned 4"
    ]
);
instruction_case!(
    untyped_store,
    [
        "OpCapability UntypedPointersKHR",
        "OpUntypedVariableKHR",
        "OpUntypedAccessChainKHR"
    ]
);
instruction_case!(
    descriptor_heaps,
    [
        "OpCapability DescriptorHeapEXT",
        "ResourceHeapEXT",
        "SamplerHeapEXT",
        "OpConstantSizeOfEXT",
        "ArrayStrideIdEXT",
        "OpImageSampleExplicitLod",
        "Aligned 16"
    ]
);
