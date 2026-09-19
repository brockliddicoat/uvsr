"""Pinned Windows RustGPU test-sysroot inputs shared by the two actual consumers."""
import hashlib
from pathlib import Path
import shutil
import subprocess


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def one(paths, description):
    paths = list(paths)
    if len(paths) != 1:
        raise RuntimeError(f"expected one {description}, found {len(paths)}")
    return paths[0].resolve()


def run(command, directory, prefix):
    result = subprocess.run(command, cwd=directory, capture_output=True, timeout=180)
    prefix.with_suffix(".stdout.txt").write_bytes(result.stdout)
    prefix.with_suffix(".stderr.txt").write_bytes(result.stderr)
    if result.returncode:
        raise RuntimeError(f"{command[0]} failed with {result.returncode}: see {prefix}.stderr.txt")
    return result.stdout.decode("utf-8").replace("\r\n", "\n")


def compiler_inputs(upstream, backend, source, target_name):
    target = upstream / f"target/compiletest-target-spec/{target_name}.json"
    deps = upstream / "target/compiletest-deps"
    shader_build = deps / f"{target_name}/debug/build"
    host_build = deps / "debug/build"
    libraries = {}
    for name in ["compiler_builtins", "core", "spirv-std", "glam"]:
        ident = name.replace("-", "_")
        libraries[ident] = one((shader_build / name).glob(f"*/out/lib{ident}*.rlib"), name)
    libraries["spirv_std_macros"] = one((host_build / "spirv-std-macros").glob("*/out/spirv_std_macros*.dll"), "Windows spirv-std-macros")
    search_paths = sorted({path.resolve() for build in [shader_build, host_build] for path in build.glob("*/*/out")})
    rustc, validator, disassembler = (shutil.which(name) for name in ("rustc", "spirv-val", "spirv-dis"))
    if not all([rustc, validator, disassembler]):
        raise RuntimeError("rustc, spirv-val and spirv-dis must be available in the owned tool environment")
    identity = {
        "source_sha256": sha256(source),
        "rustc": subprocess.check_output([rustc, "-vV"], text=True).strip(),
        "rustgpu_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=upstream, text=True).strip(),
        "codegen_backend_sha256": sha256(backend), "target_sha256": sha256(target),
        "libraries": {name: {"path": str(path), "sha256": sha256(path)} for name, path in libraries.items()},
        "validator_sha256": sha256(Path(validator)),
        "validator_version": subprocess.check_output([validator, "--version"], text=True).strip(),
    }
    command = [
        rustc, str(source), "--edition=2021", "--crate-type=dylib", f"--crate-name={source.stem}",
        "--target", str(target), "-Zunstable-options", f"-Zcodegen-backend={backend}",
        "-Zbinary-dep-depinfo", "-Csymbol-mangling-version=v0",
        "-Zcrate-attr=feature(register_tool)", "-Zcrate-attr=register_tool(rust_gpu)",
        "-Zcrate-attr=feature(asm_experimental_arch)", "-Coverflow-checks=off", "-Cdebug-assertions=off",
        "-Zinline-mir=off", "-Zmir-enable-passes=-GVN", "-Zshare-generics=off",
        "-Cembed-bitcode=no", "-Cdebuginfo=0", "-Clto=off",
    ]
    for path in search_paths:
        command += ["-L", f"dependency={path}"]
    for name, path in libraries.items():
        prefix = "noprelude:" if name in {"core", "compiler_builtins"} else ""
        command += ["--extern", f"{prefix}{name}={path}"]
    return command, identity, validator, disassembler
