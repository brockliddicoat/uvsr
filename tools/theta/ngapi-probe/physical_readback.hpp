#pragma once

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <stddef.h>

// Matches physical_readback.rs. These are GPU addresses, never CPU pointers.
struct PhysicalReadbackRoot
{
    gpu::uint64 source;
    gpu::uint64 destination;
};

static_assert(sizeof(PhysicalReadbackRoot) == 16);
static_assert(alignof(PhysicalReadbackRoot) == 8);
static_assert(offsetof(PhysicalReadbackRoot, source) == 0);
static_assert(offsetof(PhysicalReadbackRoot, destination) == 8);
