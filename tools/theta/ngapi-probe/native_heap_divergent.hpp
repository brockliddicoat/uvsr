#pragma once

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <stddef.h>

struct NativeHeapDivergentRoot
{
    gpu::uint64 output;
    gpu::uint32 resource_xor;
    gpu::uint32 sampler_xor;
};

static_assert(sizeof(NativeHeapDivergentRoot) == 16);
static_assert(alignof(NativeHeapDivergentRoot) == 8);
static_assert(offsetof(NativeHeapDivergentRoot, output) == 0);
static_assert(offsetof(NativeHeapDivergentRoot, resource_xor) == 8);
static_assert(offsetof(NativeHeapDivergentRoot, sampler_xor) == 12);
