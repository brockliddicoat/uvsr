#pragma once

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <stddef.h>

// Matches the original NGAPI cube's vertex and root prefix, with two selectors
// appended for this diagnostic. See the NoGraphicsAPI MIT attribution in README.
struct CubeVertex
{
    float position[4];
    float uv[2];
};

struct CubeRoot
{
    gpu::uint64 vertices;
    float transform[4][4];
    gpu::uint32 resource;
    gpu::uint32 sampler;
};

static_assert(sizeof(CubeVertex) == 24 && alignof(CubeVertex) == 4);
static_assert(offsetof(CubeVertex, uv) == 16);
static_assert(sizeof(CubeRoot) == 80 && alignof(CubeRoot) == 8);
static_assert(offsetof(CubeRoot, vertices) == 0);
static_assert(offsetof(CubeRoot, transform) == 8);
static_assert(offsetof(CubeRoot, resource) == 72);
static_assert(offsetof(CubeRoot, sampler) == 76);
