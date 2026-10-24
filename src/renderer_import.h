#pragma once

#include "array_view.h"

namespace uvsr
{
    enum class ImportError : uint8_t
    {
        None, InvalidInput, InvalidJson, InvalidContainer, UnsupportedVersion,
        UnsupportedExtension, InvalidData, InvalidUri, Overflow, OutOfMemory,
        InvalidIndex, InvalidAccessor, InvalidRange, InvalidSparse,
        BufferUnavailable, InvalidOutput, NonFiniteValue, UnsupportedData,
        InvalidState, InvalidHierarchy, Cycle, Capacity, Workspace,
        FileUnavailable, Io, Canceled
    };

    enum class ImportObject : uint8_t
    {
        Document, Buffer, BufferView, Accessor, Image, Scene, Node, Mesh,
        Primitive, Skin, Material, Texture, Light, Camera, Animation
    };

    struct ImportResult
    {
        ImportError error = ImportError::None;
        ImportObject object = ImportObject::Document;
        size_t index = SIZE_MAX;
        uint32_t parserCode = 0;
        uint32_t systemCode = 0;
        [[nodiscard]] explicit operator bool() const noexcept { return error == ImportError::None; }
    };

    enum class ImportShape : uint8_t { Scalar, Vec2, Vec3, Vec4, Mat2, Mat3, Mat4 };
    enum class ImportComponent : uint8_t { Int8, Uint8, Int16, Uint16, Uint32, Float32 };

    struct ImportAccessorInfo
    {
        size_t count = 0;
        size_t scalarCount = 0;
        ImportShape shape = ImportShape::Scalar;
        ImportComponent component = ImportComponent::Float32;
        bool normalized = false;
        bool sparse = false;
    };

    struct ImportBufferInfo
    {
        size_t byteCount = 0;
        // borrows the document until successful Parse, Reset or destruction.
        ArrayView<const char> uri;
        bool resident = false;
    };

    struct ImportState;
    struct ImportSceneOptions;
    struct ImportRuntimeLightIds;
    class RendererScene;
    class ImportGeometry;
    class ImportTextures;

    // one loading operation owns this document. parser/input storage never escapes.
    // calls are synchronous and require exclusive access; failure preserves state.
    class ImportDocument final
    {
    public:
        ImportDocument() noexcept = default;
        ~ImportDocument() noexcept;
        ImportDocument(const ImportDocument&) = delete;
        ImportDocument& operator=(const ImportDocument&) = delete;
        ImportDocument(ImportDocument&& other) noexcept;
        ImportDocument& operator=(ImportDocument&& other) noexcept;

        [[nodiscard]] ImportResult Parse(ArrayView<const uint8_t> bytes) noexcept;
        void Reset() noexcept;
        [[nodiscard]] size_t BufferCount() const noexcept;
        [[nodiscard]] size_t AccessorCount() const noexcept;
        [[nodiscard]] ImportResult BufferInfo(size_t index, ImportBufferInfo& output) const noexcept;
        // copies the declared buffer length. caller bytes are borrowed only for this call.
        [[nodiscard]] ImportResult SupplyBuffer(size_t index, ArrayView<const uint8_t> bytes) noexcept;
        [[nodiscard]] ImportResult AccessorInfo(size_t index, ImportAccessorInfo& output) const noexcept;
        // exact scalar counts, column-major matrices. failed reads leave output unchanged.
        [[nodiscard]] ImportResult ReadFloats(size_t index, ArrayView<float> output) const noexcept;
        // unsigned, non-normalized scalar/vector data only; no float-to-integer narrowing.
        [[nodiscard]] ImportResult ReadUnsigned(size_t index, ArrayView<uint32_t> output) const noexcept;

    private:
        ImportState* m_State = nullptr;
        friend ImportResult ConvertImportScene(const ImportDocument&, const ImportSceneOptions&,
            RendererScene&, ImportGeometry&, ImportTextures*, ImportRuntimeLightIds*) noexcept;
    };

    [[nodiscard]] const char* ImportErrorText(ImportError error) noexcept;

#if defined(UVSR_BUILD_TESTING)
    // only first-party allocations; vendor allocation exhaustion remains fatal.
    void SetImportAllocationFailureCountdown(int64_t successfulAllocations) noexcept;
#endif
}
