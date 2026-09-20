//! Private single-threaded native ownership and allocation limits.
#![forbid(unsafe_code)]
use super::Error;
use std::cell::Cell;

pub(super) struct LiveObjects {
    count: Cell<u32>,
    limit: u32,
}

impl LiveObjects {
    pub(super) fn new(limit: u32) -> Self {
        Self {
            count: Cell::new(0),
            limit,
        }
    }

    pub(super) fn acquire(&self) -> Result<Lease<'_>, Error> {
        let next = self
            .count
            .get()
            .checked_add(1)
            .filter(|&count| count <= self.limit)
            .ok_or_else(|| Error::Unsupported("native resource count limit reached".into()))?;
        self.count.set(next);
        Ok(Lease(self))
    }

    pub(super) fn is_empty(&self) -> bool {
        self.count.get() == 0
    }
}

// No Clone or public constructor. The resource owns this token from before its
// first native creation until after native destruction, including error paths.
// Forgetting a resource leaves its token counted, so its device is leaked too.
pub(super) struct Lease<'a>(&'a LiveObjects);

impl Drop for Lease<'_> {
    fn drop(&mut self) {
        self.0.count.set(self.0.count.get() - 1);
    }
}

pub(super) fn allocation_size(bytes: u64, maximum: u64, heap: u64) -> Result<(), Error> {
    if bytes == 0 || bytes > maximum || bytes > heap {
        return Err(Error::Invalid(
            "allocation size exceeds device or memory heap limits",
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agfx_owner_capacity_and_failed_creation_release() {
        let allocations = LiveObjects::new(2);
        let buffer = allocations.acquire().unwrap();
        let failed_image = allocations.acquire().unwrap();
        assert!(allocations.acquire().is_err());
        drop(failed_image);
        let image = allocations.acquire().unwrap();
        drop(buffer);
        assert!(!allocations.is_empty());
        drop(image);
        assert!(allocations.is_empty());
        assert!(LiveObjects::new(0).acquire().is_err());
    }

    #[test]
    fn agfx_forgotten_owner_prevents_parent_destruction() {
        let owners = LiveObjects::new(1);
        std::mem::forget(owners.acquire().unwrap());
        assert!(!owners.is_empty());
        assert!(owners.acquire().is_err());
    }

    #[test]
    fn agfx_allocation_size_obeys_both_limits() {
        assert!(allocation_size(0, 1024, 2048).is_err());
        assert!(allocation_size(1024, 1024, 2048).is_ok());
        assert!(allocation_size(1025, 1024, 2048).is_err());
        assert!(allocation_size(1024, 2048, 1024).is_ok());
        assert!(allocation_size(1025, 2048, 1024).is_err());
        assert!(allocation_size(u64::MAX, u64::MAX, u64::MAX).is_ok());
    }
}
