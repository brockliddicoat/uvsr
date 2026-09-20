//! Small, synchronous Vulkan owners for AGFX buffers and 2D textures.
//! Source behavior is mapped in tests/parity. No GPU runs in
//! ordinary unit tests. Native boundaries are registered in UNSAFE.md.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

mod vulkan;
pub use vulkan::{
    Buffer, BufferCompute, Completion, ComputeDispatch, ComputeInterface, ComputeRoot, Device,
    DeviceInfo, Memory, ShaderCode, ShaderStage, StorageCompute, Texture, TextureCopy,
    TextureFormat, TextureInfo,
};

use std::fmt;

#[derive(Debug)]
pub enum Error {
    Unsupported(String),
    Invalid(&'static str),
    Vulkan(ash::vk::Result),
    Loader(String),
}

impl From<ash::vk::Result> for Error {
    fn from(value: ash::vk::Result) -> Self {
        Self::Vulkan(value)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Unsupported(message) => write!(f, "unsupported: {message}"),
            Self::Invalid(message) => write!(f, "invalid operation: {message}"),
            Self::Vulkan(result) => write!(f, "Vulkan returned {result:?}"),
            Self::Loader(message) => write!(f, "Vulkan loader: {message}"),
        }
    }
}
impl std::error::Error for Error {}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CopyRegion {
    pub source: u64,
    pub destination: u64,
    pub bytes: u64,
}

// Validate before allocating or recording any command. In particular, Vulkan
// does not define an order for overlapping destination regions in one copy.
fn validate_copy(
    source_bytes: u64,
    destination_bytes: u64,
    regions: &[CopyRegion],
) -> Result<bool, Error> {
    if regions.is_empty() {
        return Err(Error::Invalid("copy has no regions"));
    }
    let mut destinations = Vec::with_capacity(regions.len());
    for region in regions {
        if region.bytes == 0 || (region.source | region.destination | region.bytes) & 3 != 0 {
            return Err(Error::Invalid(
                "copy ranges must be nonempty and four-byte aligned",
            ));
        }
        let source_end = region
            .source
            .checked_add(region.bytes)
            .ok_or(Error::Invalid("source range overflow"))?;
        let destination_end = region
            .destination
            .checked_add(region.bytes)
            .ok_or(Error::Invalid("destination range overflow"))?;
        if source_end > source_bytes || destination_end > destination_bytes {
            return Err(Error::Invalid("copy range exceeds its buffer"));
        }
        destinations.push((region.destination, destination_end));
    }
    destinations.sort_unstable();
    let mut covered = 0;
    let mut complete = true;
    for (start, end) in destinations {
        if start < covered {
            return Err(Error::Invalid("copy destination regions overlap"));
        }
        complete &= start == covered;
        covered = end;
    }
    Ok(complete && covered == destination_bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn source_copy_has_complete_destination_and_overlapping_source() {
        let regions = [
            CopyRegion {
                source: 0,
                destination: 0,
                bytes: 128,
            },
            CopyRegion {
                source: 64,
                destination: 128,
                bytes: 128,
            },
        ];
        assert!(validate_copy(256, 256, &regions).unwrap());
        let source: Vec<u8> = (0..64_u32)
            .flat_map(|i| (0xc0de0000 | (i * 7 + 1)).to_le_bytes())
            .collect();
        let mut actual = source[..128].to_vec();
        actual.extend_from_slice(&source[64..192]);
        assert_eq!(
            actual.as_slice(),
            include_bytes!("../../../tests/parity/fixtures/agfx/copy_buffer_to_buffer.bin")
        );
    }

    #[test]
    fn rejected_copy_cannot_overlap_or_overrun() {
        for region in [
            CopyRegion {
                source: 0,
                destination: 0,
                bytes: 0,
            },
            CopyRegion {
                source: 1,
                destination: 0,
                bytes: 4,
            },
            CopyRegion {
                source: 0,
                destination: 252,
                bytes: 8,
            },
            CopyRegion {
                source: u64::MAX - 3,
                destination: 0,
                bytes: 8,
            },
        ] {
            assert!(validate_copy(256, 256, &[region]).is_err());
        }
        let region = CopyRegion {
            source: 0,
            destination: 0,
            bytes: 128,
        };
        assert!(validate_copy(256, 256, &[]).is_err());
        assert!(validate_copy(256, 256, &[region, region]).is_err());
        assert!(!validate_copy(256, 256, &[region]).unwrap());
    }

    #[test]
    fn ordered_recurrence_matches_frozen_compute_golden() {
        let mut words = [0_u32; 64];
        for pass in 0..4 {
            for (index, word) in words.iter_mut().enumerate() {
                *word = word
                    .wrapping_mul(2)
                    .wrapping_add(pass)
                    .wrapping_add(index as u32);
            }
        }
        let bytes: Vec<u8> = words.into_iter().flat_map(u32::to_le_bytes).collect();
        assert_eq!(
            bytes.as_slice(),
            include_bytes!("../../../tests/parity/fixtures/agfx/compute_multi_dispatch_buffer.bin")
        );
    }
}
