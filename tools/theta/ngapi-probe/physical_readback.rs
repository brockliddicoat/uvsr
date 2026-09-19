#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use spirv_std::spirv;

/// Matches PhysicalReadbackRoot in physical_readback.hpp. Both fields transport
/// device addresses, not host pointers or target-dependent usize values.
#[repr(C)]
pub struct PhysicalReadbackRoot {
    pub source: u64,
    pub destination: u64,
}

const _: () = {
    assert!(core::mem::size_of::<PhysicalReadbackRoot>() == 16);
    assert!(core::mem::align_of::<PhysicalReadbackRoot>() == 8);
    assert!(core::mem::offset_of!(PhysicalReadbackRoot, source) == 0);
    assert!(core::mem::offset_of!(PhysicalReadbackRoot, destination) == 8);
};

/// Reads a real physical address and reports its value and both address words.
///
/// # Safety
/// U-002 in UNSAFE.md owns this boundary. Dispatch exactly one invocation.
/// `source` must identify a live, initialized, four-byte-aligned u32 allocation.
/// `destination` must identify a disjoint live allocation of at least 12 bytes,
/// aligned to four and exclusively writable by this invocation. Both ranges
/// must remain live through completion, with no concurrent CPU/GPU access.
/// The host must establish initialization visibility before dispatch and
/// completion plus visibility before reading, reusing or freeing either range.
/// Never dispatch synthetic addresses. No safe entry wrapper is provided.
#[allow(unsafe_code, non_snake_case)]
#[spirv(compute(threads(1)))]
pub unsafe fn computeMain(#[spirv(push_constant)] root: &PhysicalReadbackRoot) {
    let source = root.source as *const u32;
    let destination = root.destination as *mut u32;
    // SAFETY: U-002. The source identifies a live, aligned and initialized u32
    // visible to this invocation. All u32 bit patterns are valid.
    let value = unsafe { source.read() };
    // SAFETY: U-002. The first four bytes of the disjoint destination are live,
    // aligned and exclusively writable until queue completion.
    unsafe { destination.write(value.wrapping_add(7)) };
    // SAFETY: U-002. Byte offset four is aligned and inside the same exclusive
    // 12-byte destination. The host validates its complete range before dispatch.
    unsafe { destination.wrapping_add(1).write(root.source as u32) };
    // SAFETY: U-002. Byte offset eight covers the final four bytes of that range.
    // Address bits are transported as integers without creating Rust references.
    unsafe {
        destination
            .wrapping_add(2)
            .write((root.source >> 32) as u32)
    };
}
