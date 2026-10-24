#include "renderer_view_nvrhi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
    using uvsr::gpu_contract::Float4;
    using uvsr::gpu_contract::Float4x4;

    unsigned comparisons = 0;

    // fixed records captured from the original Donut control. see renderer_view_fixture.md.
    struct ReferenceView
    {
        uvsr::RendererViewport viewport;
        Float4x4 worldToView;
        Float4x4 projection;
        uvsr::gpu_contract::Float2 pixelOffset;
        RendererViewConstants constants;
        uvsr::RendererSceneFrustum frustum;
        uvsr::RendererViewport outputViewport;
        uvsr::RendererViewExtent extent;
        Float4x4 translated;
        uvsr::gpu_contract::Float3 direction;
        uint32_t flags[2];
    };

    template<class T> bool Read(FILE* source, T& value)
    {
        return fread(&value, 1, sizeof(value), source) == sizeof(value);
    }

    bool ReadView(FILE* source, ReferenceView& view)
    {
        static_assert(sizeof(view.viewport) == 24 && sizeof(view.extent) == 16);
        static_assert(sizeof(view.constants) == 720 && sizeof(view.frustum.planes) == 96);
        static_assert(sizeof(view.worldToView) == 64 && sizeof(view.pixelOffset) == 8 && sizeof(view.direction) == 12);
        return Read(source, view.viewport) && Read(source, view.worldToView) &&
            Read(source, view.projection) && Read(source, view.pixelOffset) &&
            Read(source, view.constants) && Read(source, view.frustum.planes) &&
            Read(source, view.outputViewport) && Read(source, view.extent) &&
            Read(source, view.translated) && Read(source, view.direction) &&
            Read(source, view.flags) && view.flags[0] <= 1 && view.flags[1] <= 1;
    }

    bool ReadHeader(FILE* source, Float4x4& failureProjection)
    {
        char magic[8];
        uint32_t count = 0, stride = 0;
        const uint32_t endian = 1;
        return *reinterpret_cast<const unsigned char*>(&endian) == 1 && Read(source, magic) &&
            memcmp(magic, "UVVW0001", sizeof(magic)) == 0 && Read(source, count) && count == 1027 &&
            Read(source, stride) && stride == 1100 && Read(source, failureProjection);
    }

    bool BuildCurrent(const ReferenceView& view, uvsr::RendererView& current)
    {
        return uvsr::BuildRendererView(view.viewport, view.worldToView, view.projection, view.pixelOffset, current);
    }

    bool Near(float actual, float expected)
    {
        return fabsf(actual - expected) <= 0.00002f;
    }

    Float4 Transform(Float4 point, const Float4x4& matrix)
    {
        const float input[4] = { point.x, point.y, point.z, point.w };
        float output[4] = {};
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned row = 0; row < 4; ++row)
                output[column] += input[row] * matrix.values[row * 4 + column];
        return { output[0], output[1], output[2], output[3] };
    }

    bool CheckReference(const ReferenceView& view)
    {
        uvsr::RendererView current{};
        if (!BuildCurrent(view, current)) return false;
        if (memcmp(&view.constants, &current.constants, sizeof(view.constants)) != 0)
        {
            const auto* a = reinterpret_cast<const unsigned char*>(&view.constants);
            const auto* b = reinterpret_cast<const unsigned char*>(&current.constants);
            for (unsigned i = 0; i < sizeof(view.constants); ++i)
                if (a[i] != b[i]) { printf("view %u differs at constant byte %u\n", comparisons, i); break; }
            return false;
        }
        if (memcmp(view.frustum.planes, current.frustum.planes, sizeof(view.frustum.planes)) != 0 ||
            current.reverseDepth != bool(view.flags[0]) || current.mirrored != bool(view.flags[1]))
            return false;
        const auto& v = view.outputViewport;
        const auto& r = view.extent;
        const nvrhi::Viewport expectedViewport(v.minX, v.maxX, v.minY, v.maxY, v.minZ, v.maxZ);
        const nvrhi::Rect expectedRect(r.minX, r.maxX, r.minY, r.maxY);
        const auto actual = uvsr::RendererViewportNvrhi(current);
        if (expectedViewport != actual.viewports[0] || expectedRect != actual.scissorRects[0])
            return false;
        const auto translated = uvsr::RendererClipToTranslatedWorld(current);
        if (memcmp(&view.translated, &translated, sizeof(translated)) != 0) return false;
        const auto previous = current;
        if (!BuildCurrent(view, current) || memcmp(&previous.constants, &current.constants, sizeof(view.constants)))
            return false;
        ++comparisons;
        return true;
    }

    bool CheckPerspective(FILE* source)
    {
        ReferenceView view{};
        if (!ReadView(source, view) || !CheckReference(view)) return false;
        RendererViewConstants constants{};
        uvsr::RendererView current;
        if (!BuildCurrent(view, current)) return false;
        constants = current.constants;
        if (!Near(constants.cameraDirectionOrPosition.x, 2.f) ||
            !Near(constants.cameraDirectionOrPosition.y, -3.f) ||
            !Near(constants.cameraDirectionOrPosition.z, 5.f) ||
            constants.cameraDirectionOrPosition.w != 1.f)
            return false;

        // near maps to 1, twice-near to 0.5; no-offset center stays at viewport center.
        const float distances[3] = { 0.25f, 0.5f, 64.f };
        for (float distance : distances)
        {
            const Float4 world{ 2.f, -3.f, 5.f + distance, 1.f };
            const Float4 clip = Transform(world, constants.matWorldToClip);
            const Float4 unjittered = Transform(world, constants.matWorldToClipNoOffset);
            if (!Near(clip.z / clip.w, 0.25f / distance) ||
                !Near(unjittered.x / unjittered.w, 0.f) ||
                !Near(unjittered.y / unjittered.w, 0.f))
                return false;
            const float pixelX = clip.x / clip.w * constants.clipToWindowScale.x + constants.clipToWindowBias.x;
            const float pixelY = clip.y / clip.w * constants.clipToWindowScale.y + constants.clipToWindowBias.y;
            if (!Near(pixelX, 11.25f) || !Near(pixelY, 20.625f))
                return false;
            const Float4 recovered = Transform(
                { clip.x / clip.w, clip.y / clip.w, clip.z / clip.w, 1.f },
                constants.matClipToWorld);
            if (!Near(recovered.x / recovered.w, world.x) ||
                !Near(recovered.y / recovered.w, world.y) ||
                !Near(recovered.z / recovered.w, world.z))
                return false;
        }
        return true;
    }

    bool CheckOtherViews(FILE* source)
    {
        ReferenceView view{};
        if (!ReadView(source, view) || !view.flags[1] || !CheckReference(view)) return false;
        if (!ReadView(source, view)) return false;
        RendererViewConstants constants{};
        uvsr::RendererView current;
        if (!BuildCurrent(view, current)) return false;
        constants = current.constants;
        const auto direction = view.direction;
        return CheckReference(view) && constants.cameraDirectionOrPosition.w == 0.f &&
            constants.cameraDirectionOrPosition.x == direction.x &&
            constants.cameraDirectionOrPosition.y == direction.y &&
            constants.cameraDirectionOrPosition.z == direction.z;
    }

    bool CheckAffineRange(FILE* source)
    {
        for (unsigned index = 0; index < 1024; ++index)
        {
            ReferenceView view{};
            if (!ReadView(source, view) || !CheckReference(view)) return false;
        }
        return true;
    }

    bool CheckFailures(const Float4x4& projection)
    {
        const Float4x4 identity{{1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f}};
        const uvsr::RendererViewport viewport{0, 100, 0, 100, 0, 1};
        uvsr::RendererView current;
        if (!uvsr::BuildRendererView(viewport, identity, projection, {}, current)) return false;
        for (unsigned index = 0; index < 16; ++index)
        {
            auto v = viewport;
            auto world = identity, clip = projection;
            uvsr::gpu_contract::Float2 offset{};
            switch (index)
            {
            case 0: v.maxX = 0; break;
            case 1: v.maxY = -1; break;
            case 2: v.minX = -INFINITY; break;
            case 3: v.maxY = NAN; break;
            case 4: v.maxX = float(INT32_MAX); break;
            case 5: v.minY = -2147483648.f; break;
            case 6: v.minZ = -0.1f; break;
            case 7: v.maxZ = 1.1f; break;
            case 8: v.minZ = 0.8f; v.maxZ = 0.2f; break;
            case 9: world.values[0] = NAN; break;
            case 10: world.values[0] = 0.f; break;
            case 11: clip.values[5] = 0.f; break;
            case 12: world.values[15] = 0.f; break;
            case 13: world.values[3] = 0.1f; break;
            case 14: offset.x = INFINITY; break;
            case 15: offset.y = NAN; break;
            }
            unsigned char previous[sizeof(current)];
            memcpy(previous, &current, sizeof(current));
            if (uvsr::BuildRendererView(v, world, clip, offset, current) || memcmp(previous, &current, sizeof(current)))
            { printf("invalid view case %u published\n", index); return false; }
        }
        return true;
    }
}

int main()
{
    FILE* source = fopen("renderer_view_fixture.bin", "rb");
    if (!source)
    {
        fputs("renderer view fixture could not be opened\n", stderr);
        return 1;
    }
    Float4x4 failureProjection{};
    const bool passed = ReadHeader(source, failureProjection) && CheckPerspective(source) &&
        CheckOtherViews(source) && CheckAffineRange(source) && CheckFailures(failureProjection) &&
        comparisons == 1027 && fgetc(source) == EOF && !ferror(source);
    const int closed = fclose(source);
    if (!passed || closed != 0)
    {
        fputs("renderer view convention check failed\n", stderr);
        return 1;
    }
    printf("renderer view: %u exact captured comparisons of 720 constant bytes, frusta, viewport and translated background; 16 unchanged failures; reverse-Z and reconstruction passed\n", comparisons);
    return 0;
}
