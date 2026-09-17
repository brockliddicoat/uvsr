#pragma once

#include "array_view.h"
#include "renderer_gpu_scalar.h"

namespace uvsr
{
    struct RendererSceneView;

    enum class CameraCollisionBuildError : uint8_t
    {
        None,
        CapacityExceeded,
        AllocationFailed,
        InvalidTree,
        InvalidSource
    };

    struct CameraCollisionSourceBuffers
    {
        // native-endian uint32 bytes. memcpy loads also accept packed import
        // storage without assuming alignment or live uint32 array objects.
        ArrayView<const unsigned char> indices;
        // packed binary32 XYZ bytes, with optional trailing GPU padding.
        // canonical geometry counts define the readable point ranges.
        ArrayView<const unsigned char> positions;
    };

    // immutable world-space triangles and BVH, owned until scene retirement.
    // preparation allocates from validated input counts. queries allocate nothing.
    class CameraCollisionWorld
    {
    public:
        using Point = gpu_contract::Float3;
        struct Triangle { Point a; Point b; Point c; };

        CameraCollisionWorld() noexcept = default;
        ~CameraCollisionWorld();
        CameraCollisionWorld(const CameraCollisionWorld&) = delete;
        CameraCollisionWorld& operator=(const CameraCollisionWorld&) = delete;
        CameraCollisionWorld(CameraCollisionWorld&& other) noexcept;
        CameraCollisionWorld& operator=(CameraCollisionWorld&& other) noexcept;

        // clears logical contents, retaining capacity until replacement/destruction.
        void Clear() noexcept;
        // borrows only during preparation. failure preserves the previous world.
        [[nodiscard]] CameraCollisionBuildError Build(ArrayView<const Triangle> triangles) noexcept;
        // buffer views use canonical buffer-group indices. optional order is a
        // permutation of canonical instances. default order is import order.
        // all inputs stay immutable until this synchronous call returns.
        [[nodiscard]] CameraCollisionBuildError BuildFromScene(const RendererSceneView& scene,
            ArrayView<const CameraCollisionSourceBuffers> buffers,
            ArrayView<const uint32_t> instanceOrder = {}) noexcept;

        [[nodiscard]] bool Empty() const noexcept { return m_TriangleCount == 0; }
        [[nodiscard]] size_t GetTriangleCount() const noexcept { return m_TriangleCount; }
        [[nodiscard]] size_t GetTriangleCapacity() const noexcept { return m_TriangleCapacity; }
        [[nodiscard]] size_t GetNodeCount() const noexcept { return m_NodeCount; }
        [[nodiscard]] size_t GetNodeCapacity() const noexcept { return m_NodeCapacity; }

        // continuous sphere sweep with tangential sliding. stationary movement
        // bypasses the BVH; ResolveSphere explicitly repairs an enlarged hitbox.
        [[nodiscard]] Point MoveSphere(Point start, Point desiredPosition, float radius) const;
        [[nodiscard]] Point ResolveSphere(Point center, Point movementHint, float radius) const;

    private:
        friend struct CameraCollisionTestAccess;
        struct BvhNode
        {
            Point minimum{};
            Point maximum{};
            uint32_t firstTriangle = 0;
            uint32_t triangleCount = 0;
            uint32_t leftChild = 0;
            uint32_t rightChild = 0;
        };
        struct SweepHit
        {
            float time = 1.f;
            Point normal{};
            bool hit = false;
        };

        [[nodiscard]] CameraCollisionBuildError AllocateTriangles(size_t count) noexcept;
        [[nodiscard]] CameraCollisionBuildError FinishBuild() noexcept;
        [[nodiscard]] CameraCollisionBuildError BuildNodes(size_t nodeCapacity) noexcept;
        [[nodiscard]] SweepHit FindEarliestHit(Point start, Point movement, float radius) const;
        [[nodiscard]] Point ResolvePenetration(Point center, Point movement, float radius) const;

        Triangle* m_Triangles = nullptr;
        BvhNode* m_Nodes = nullptr;
        size_t m_TriangleCount = 0;
        size_t m_TriangleCapacity = 0;
        size_t m_NodeCount = 0;
        size_t m_NodeCapacity = 0;
    };

#if defined(UVSR_BUILD_TESTING)
    void SetCameraCollisionAllocationFailure(uint32_t ordinal) noexcept;
#endif
}
