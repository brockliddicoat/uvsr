#pragma once

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <stddef.h>

struct NativeHeapSampleRoot
{
    gpu::uint64 output;
    gpu::uint32 resource;
    gpu::uint32 sampler;
};

static_assert(sizeof(NativeHeapSampleRoot) == 16);
static_assert(alignof(NativeHeapSampleRoot) == 8);
static_assert(offsetof(NativeHeapSampleRoot, output) == 0);
static_assert(offsetof(NativeHeapSampleRoot, resource) == 8);
static_assert(offsetof(NativeHeapSampleRoot, sampler) == 12);
