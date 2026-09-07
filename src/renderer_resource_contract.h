#pragma once

#include <utility>

namespace uvsr
{
    class RendererResourceCreationSequence
    {
    public:
        template<class Create>
        [[nodiscard]] bool Require(Create&& create)
        {
            if (!m_Valid)
                return false;
            m_Valid = bool(std::forward<Create>(create)());
            return m_Valid;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_Valid;
        }

    private:
        bool m_Valid = true;
    };

    struct RendererCommonInitializationContract
    {
        bool device = false;
        bool fullscreenZeroShader = false;
        bool fullscreenOneShader = false;
        bool blitShader = false;
        bool linearClampSampler = false;
        bool linearWrapSampler = false;
        bool blackTexture = false;
        bool whiteTexture = false;
        bool blackCubeArray = false;
        bool blitBindingLayout = false;
        bool uploadCommandList = false;
        bool uploadSubmitted = false;

        [[nodiscard]] constexpr bool IsComplete() const noexcept
        {
            return device && fullscreenZeroShader && fullscreenOneShader &&
                blitShader && linearClampSampler && linearWrapSampler &&
                blackTexture && whiteTexture && blackCubeArray &&
                blitBindingLayout && uploadCommandList &&
                uploadSubmitted;
        }
    };

    struct RendererBlitDispatchContract
    {
        bool commonInitialized = false;
        bool pipeline = false;
        bool bindingSet = false;

        [[nodiscard]] constexpr bool CanDispatch() const noexcept
        {
            return commonInitialized && pipeline && bindingSet;
        }
    };

    class RendererBlitPipelineFailureLatch
    {
    public:
        [[nodiscard]] constexpr bool CanAttempt() const noexcept
        {
            return !m_Failed;
        }

        constexpr void RecordResult(bool available) noexcept
        {
            if (!available)
                m_Failed = true;
        }

        [[nodiscard]] constexpr bool HasFailed() const noexcept
        {
            return m_Failed;
        }

    private:
        bool m_Failed = false;
    };

    struct RendererPixelReadbackInitializationContract
    {
        bool device = false;
        bool shader = false;
        bool intermediateBuffer = false;
        bool readbackBuffer = false;
        bool constantBuffer = false;
        bool bindingLayout = false;
        bool bindingSet = false;
        bool pipeline = false;

        [[nodiscard]] constexpr bool IsComplete() const noexcept
        {
            return device && shader && intermediateBuffer && readbackBuffer &&
                constantBuffer && bindingLayout && bindingSet && pipeline;
        }
    };

}
