#include "denoiser_backend.h"
#include "denoising_settings.h"

#if UVSR_WITH_NRD
#include "nrd_denoiser_backend.h"
#endif

#ifdef NRD_VERSION_MAJOR
#error "The renderer denoiser API must not expose NRD SDK headers"
#endif

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace uvsr;

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Denoiser backend validation failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    DenoiserTextureInfo MakeTexture(
        DenoiserExtent extent,
        nvrhi::Format format,
        bool isUav = true)
    {
        DenoiserTextureInfo texture;
        texture.extent = extent;
        texture.format = format;
        texture.dimension = nvrhi::TextureDimension::Texture2D;
        texture.sampleCount = 1;
        texture.isUav = isUav;
        texture.present = true;
        return texture;
    }

    DenoiserTextureInfoArray MakeCommonTextures(DenoiserExtent extent)
    {
        DenoiserTextureInfoArray textures{};
        textures[size_t(DenoiserTextureSlot::MotionVectors)] =
            MakeTexture(extent, nvrhi::Format::RGBA16_FLOAT);
        textures[size_t(DenoiserTextureSlot::NormalRoughness)] =
            MakeTexture(extent, nvrhi::Format::RGBA16_SNORM);
        textures[size_t(DenoiserTextureSlot::ViewZ)] =
            MakeTexture(extent, nvrhi::Format::R32_FLOAT);
        return textures;
    }

    DenoiserSignalDescription MakeDescription(
        DenoiserSignalType type,
        DenoiserMethod method)
    {
        DenoiserSignalDescription description;
        description.type = type;
        description.method = method;
        description.allocationExtent = { 1280, 720 };
        return description;
    }

    void TestDrawerSettingsDefaults()
    {
        const DenoisingSettings settings;
        const std::array<DenoisingSignalSettings, 4> signals = {
            settings.ambientOcclusion,
            settings.diffuseGi,
            settings.shadows,
            settings.skyVisibility };
        for (const DenoisingSignalSettings& signal : signals)
        {
            Require(signal.method == DenoisingMethodChoice::None,
                "a denoiser must not be enabled by default");
            Require(signal.quality == DenoisingQuality::Balanced,
                "default denoising quality must be Balanced");
            Require(signal.resolution == DenoisingResolution::Half,
                "default denoising resolution must be Half");
            Require(signal.historyLength == 16,
                "default denoising history must be 16 frames");
        }
        Require(GetDenoisingResolutionScale(DenoisingResolution::Quarter) ==
                0.25f &&
            GetDenoisingResolutionScale(DenoisingResolution::Half) == 0.5f &&
            GetDenoisingResolutionScale(DenoisingResolution::Full) == 1.f,
            "Quarter, Half, and Full resolution scales changed");
        Require(SupportsDenoisingMethod(DenoisingEffect::AmbientOcclusion,
                DenoisingMethodChoice::Reblur) &&
            !SupportsDenoisingMethod(DenoisingEffect::AmbientOcclusion,
                DenoisingMethodChoice::Relax) &&
            SupportsDenoisingMethod(DenoisingEffect::DiffuseGi,
                DenoisingMethodChoice::Relax) &&
            SupportsDenoisingMethod(DenoisingEffect::SkyVisibility,
                DenoisingMethodChoice::Reblur) &&
            SupportsDenoisingMethod(DenoisingEffect::Shadows,
                DenoisingMethodChoice::Sigma),
            "drawer method compatibility changed");
    }

    void TestResolutionAndFrameHelpers()
    {
        Require(CalculateDenoiserExtent({ 1921, 1081 }, 0.5f) ==
                DenoiserExtent{ 961, 541 },
            "half resolution must round odd dimensions up");
        Require(!CalculateDenoiserExtent({ 1920, 1080 }, 0.2f).IsValid(),
            "scales below Quarter must be rejected");
        Require(!CalculateDenoiserExtent({ 1920, 1080 },
                std::numeric_limits<float>::quiet_NaN()).IsValid(),
            "nonfinite scales must be rejected");
        Require(GetDenoiserScreenMotionScale({ 1920, 1080 }) ==
                std::array<float, 3>{ 1.f / 1920.f, 1.f / 1080.f, 0.f },
            "screen motion scale changed");
        Require(IsConsecutiveDenoiserFrame(41, 42) &&
            !IsConsecutiveDenoiserFrame(41, 43),
            "history discontinuity detection changed");
        Require(MergeDenoiserHistoryReset(DenoiserHistoryReset::Restart,
                DenoiserHistoryReset::ClearAndRestart) ==
                DenoiserHistoryReset::ClearAndRestart,
            "clear and restart must dominate restart");
    }

    void TestQualityPresets()
    {
        const DenoiserQualityPreset performance =
            GetDenoiserQualityPreset(DenoiserQuality::Performance);
        Require(performance.reblurFastHistory == 3 &&
            performance.reblurHistoryFix == 1 &&
            performance.reblurStabilizationLimit == 0 &&
            performance.reblurPrepassRadius == 10.f &&
            performance.reblurMaximumBlurRadius == 20.f &&
            !performance.reblurAntiFirefly &&
            performance.relaxAtrousIterations == 2 &&
            performance.relaxFastHistory == 3 &&
            performance.relaxHistoryFix == 1 &&
            performance.relaxPrepassRadius == 10.f &&
            !performance.relaxAntiFirefly &&
            performance.sigmaSunStabilization == 0,
            "Performance preset changed");

        const DenoiserQualityPreset balanced =
            GetDenoiserQualityPreset(DenoiserQuality::Balanced);
        Require(balanced.reblurFastHistory == 5 &&
            balanced.reblurHistoryFix == 2 &&
            balanced.reblurStabilizationLimit == 12 &&
            balanced.reblurPrepassRadius == 20.f &&
            balanced.reblurMaximumBlurRadius == 25.f &&
            balanced.reblurAntiFirefly &&
            balanced.relaxAtrousIterations == 3 &&
            balanced.relaxFastHistory == 5 &&
            balanced.relaxHistoryFix == 2 &&
            balanced.relaxPrepassRadius == 20.f &&
            !balanced.relaxAntiFirefly &&
            balanced.sigmaSunStabilization == 3,
            "Balanced preset changed");

        const DenoiserQualityPreset quality =
            GetDenoiserQualityPreset(DenoiserQuality::Quality);
        Require(quality.reblurFastHistory == 6 &&
            quality.reblurHistoryFix == 3 &&
            quality.reblurStabilizationLimit ==
                std::numeric_limits<uint32_t>::max() &&
            quality.reblurPrepassRadius == 30.f &&
            quality.reblurMaximumBlurRadius == 30.f &&
            quality.reblurAntiFirefly &&
            quality.relaxAtrousIterations == 5 &&
            quality.relaxFastHistory == 6 &&
            quality.relaxHistoryFix == 3 &&
            quality.relaxPrepassRadius == 30.f &&
            !quality.relaxAntiFirefly &&
            quality.sigmaSunStabilization == 5,
            "Quality preset changed");

        const DenoiserQualityPreset ultra =
            GetDenoiserQualityPreset(DenoiserQuality::Ultra);
        Require(ultra.reblurFastHistory == 8 &&
            ultra.reblurHistoryFix == 3 &&
            ultra.reblurStabilizationLimit ==
                std::numeric_limits<uint32_t>::max() &&
            ultra.reblurPrepassRadius == 30.f &&
            ultra.reblurMaximumBlurRadius == 40.f &&
            ultra.reblurAntiFirefly &&
            ultra.relaxAtrousIterations == 8 &&
            ultra.relaxFastHistory == 8 &&
            ultra.relaxHistoryFix == 3 &&
            ultra.relaxPrepassRadius == 30.f &&
            ultra.relaxAntiFirefly &&
            ultra.sigmaSunStabilization == 7,
            "Ultra preset changed");
    }

    void TestMethodAndResourceContracts()
    {
        Require(IsDenoiserMethodCompatible(
                DenoiserSignalType::AmbientOcclusion,
                DenoiserMethod::ReblurDiffuse),
            "AO ReBLUR compatibility was rejected");
        Require(IsDenoiserMethodCompatible(DenoiserSignalType::DiffuseGi,
                DenoiserMethod::ReblurDiffuse) &&
            IsDenoiserMethodCompatible(DenoiserSignalType::DiffuseGi,
                DenoiserMethod::RelaxDiffuse),
            "GI method compatibility was rejected");
        Require(IsDenoiserMethodCompatible(DenoiserSignalType::SkyVisibility,
                DenoiserMethod::RelaxDiffuse),
            "sky ReLAX compatibility was rejected");
        Require(IsDenoiserMethodCompatible(DenoiserSignalType::SunShadow,
                DenoiserMethod::SigmaShadow) &&
            IsDenoiserMethodCompatible(DenoiserSignalType::FlashlightShadow,
                DenoiserMethod::SigmaShadow),
            "separate shadow SIGMA compatibility was rejected");
        Require(!IsDenoiserMethodCompatible(
                DenoiserSignalType::AmbientOcclusion,
                DenoiserMethod::SigmaShadow),
            "an incompatible AO SIGMA method was accepted");

        DenoiserSignalDescription ao = MakeDescription(
            DenoiserSignalType::AmbientOcclusion,
            DenoiserMethod::ReblurDiffuse);
        auto aoTextures = MakeCommonTextures(ao.allocationExtent);
        aoTextures[size_t(
            DenoiserTextureSlot::NoisyRadianceHitDistance)] =
            MakeTexture(ao.allocationExtent, nvrhi::Format::RGBA16_FLOAT);
        aoTextures[size_t(
            DenoiserTextureSlot::DenoisedRadianceHitDistance)] =
            MakeTexture(ao.allocationExtent, nvrhi::Format::RGBA16_FLOAT);
        Require(bool(ValidateDenoiserSignalContract(ao, aoTextures)),
            "valid AO resources were rejected");

        DenoiserSignalDescription gi = MakeDescription(
            DenoiserSignalType::DiffuseGi,
            DenoiserMethod::RelaxDiffuse);
        auto radianceTextures = MakeCommonTextures(gi.allocationExtent);
        radianceTextures[size_t(
            DenoiserTextureSlot::NoisyRadianceHitDistance)] =
            MakeTexture(gi.allocationExtent, nvrhi::Format::RGBA16_FLOAT);
        radianceTextures[size_t(
            DenoiserTextureSlot::DenoisedRadianceHitDistance)] =
            MakeTexture(gi.allocationExtent, nvrhi::Format::RGBA16_FLOAT);
        Require(bool(ValidateDenoiserSignalContract(gi, radianceTextures)),
            "valid GI resources were rejected");
        gi.type = DenoiserSignalType::SkyVisibility;
        Require(bool(ValidateDenoiserSignalContract(gi, radianceTextures)),
            "valid sky resources were rejected");

        DenoiserSignalDescription sun = MakeDescription(
            DenoiserSignalType::SunShadow,
            DenoiserMethod::SigmaShadow);
        auto shadowTextures = MakeCommonTextures(sun.allocationExtent);
        shadowTextures[size_t(DenoiserTextureSlot::Penumbra)] =
            MakeTexture(sun.allocationExtent, nvrhi::Format::R16_FLOAT);
        shadowTextures[size_t(DenoiserTextureSlot::Shadow)] =
            MakeTexture(sun.allocationExtent, nvrhi::Format::R8_UNORM);
        Require(bool(ValidateDenoiserSignalContract(sun, shadowTextures)),
            "valid sun SIGMA resources were rejected");
        sun.type = DenoiserSignalType::FlashlightShadow;
        Require(bool(ValidateDenoiserSignalContract(sun, shadowTextures)),
            "valid flashlight SIGMA resources were rejected");

        auto wrongShadowFormat = shadowTextures;
        wrongShadowFormat[size_t(DenoiserTextureSlot::Shadow)].format =
            nvrhi::Format::R16_FLOAT;
        Require(ValidateDenoiserSignalContract(sun, wrongShadowFormat).code ==
                DenoiserStatusCode::InvalidArgument,
            "SIGMA accepted a non R8 shadow history");
        auto missingHit = radianceTextures;
        gi.hitDistanceAvailable = false;
        Require(ValidateDenoiserSignalContract(gi, missingHit).code ==
                DenoiserStatusCode::Unsupported,
            "a signal without physical hit distance was accepted");
    }

    void TestAliasContract()
    {
        DenoiserSignalResources resources;
        resources.motionVectors =
            reinterpret_cast<nvrhi::ITexture*>(uintptr_t{ 1 });
        resources.normalRoughness =
            reinterpret_cast<nvrhi::ITexture*>(uintptr_t{ 2 });
        resources.viewZ =
            reinterpret_cast<nvrhi::ITexture*>(uintptr_t{ 3 });
        resources.noisyRadianceHitDistance =
            reinterpret_cast<nvrhi::ITexture*>(uintptr_t{ 4 });
        resources.denoisedRadianceHitDistance =
            reinterpret_cast<nvrhi::ITexture*>(uintptr_t{ 5 });
        Require(bool(ValidateDenoiserResourceAliases(resources)),
            "distinct resources were rejected");
        resources.denoisedRadianceHitDistance =
            resources.noisyRadianceHitDistance;
        Require(ValidateDenoiserResourceAliases(resources).code ==
                DenoiserStatusCode::InvalidArgument,
            "input and output signal alias was accepted");
    }

    struct RecordingState
    {
        uint32_t configureCalls = 0;
        uint32_t executeCalls = 0;
        uint32_t resetCalls = 0;
        uint32_t shutdownCalls = 0;
        DenoiserSignalType type = DenoiserSignalType::AmbientOcclusion;
    };

    class RecordingBackend final : public IDenoiserSignalBackend
    {
    public:
        explicit RecordingBackend(std::shared_ptr<RecordingState> state)
            : m_State(std::move(state))
        {
            m_Capabilities.backendAvailable = true;
            m_Capabilities.backendName = "Recording";
        }
        const DenoiserBackendCapabilities& GetCapabilities()
            const noexcept override { return m_Capabilities; }
        DenoiserMemoryStats GetMemoryStats() const noexcept override
        {
            return { 13, 7 };
        }
        DenoiserStatus Initialize(nvrhi::IDevice*, uint32_t) override
        {
            return DenoiserStatus::Ok();
        }
        DenoiserStatus ConfigureSignal(
            const DenoiserSignalDescription& description,
            const DenoiserSignalResources&,
            const DenoiserSettings&) override
        {
            ++m_State->configureCalls;
            m_State->type = description.type;
            return DenoiserStatus::Ok();
        }
        void RequestHistoryReset(DenoiserHistoryReset) noexcept override
        {
            ++m_State->resetCalls;
        }
        DenoiserStatus Execute(
            nvrhi::ICommandList*, const DenoiserFrameSettings&) override
        {
            ++m_State->executeCalls;
            return DenoiserStatus::Ok();
        }
        void Shutdown() noexcept override { ++m_State->shutdownCalls; }

    private:
        std::shared_ptr<RecordingState> m_State;
        DenoiserBackendCapabilities m_Capabilities;
    };

    void TestIndependentSignalRegistry()
    {
        auto states = std::make_shared<
            std::vector<std::shared_ptr<RecordingState>>>();
        auto backend = CreateDenoiserBackendRegistry([states]
        {
            auto state = std::make_shared<RecordingState>();
            states->push_back(state);
            return std::make_unique<RecordingBackend>(state);
        });
        Require(bool(backend->Initialize(nullptr, 3)),
            "recording registry initialization failed");

        const std::array<DenoiserSignalType, 5> types = {
            DenoiserSignalType::AmbientOcclusion,
            DenoiserSignalType::DiffuseGi,
            DenoiserSignalType::SkyVisibility,
            DenoiserSignalType::SunShadow,
            DenoiserSignalType::FlashlightShadow };
        std::array<DenoiserSignalHandle, 5> handles{};
        for (size_t index = 0; index < types.size(); ++index)
        {
            DenoiserSignalDescription description;
            description.type = types[index];
            description.allocationExtent = { 64, 64 };
            DenoiserSignalResources resources;
            DenoiserSettings settings;
            Require(bool(backend->RegisterSignal(description,
                    resources, settings, handles[index])) && handles[index],
                "independent signal registration failed");
        }
        Require(states->size() == 5,
            "each denoised signal must own one concrete backend instance");
        Require(backend->GetMemoryStats().permanentBytes == 65 &&
                backend->GetMemoryStats().transientBytes == 35,
            "independent signal memory was not aggregated");
        Require(backend->RequestHistoryReset(handles[3],
                DenoiserHistoryReset::ClearAndRestart) &&
            (*states)[3]->resetCalls == 1 && (*states)[4]->resetCalls == 0,
            "sun history reset leaked into flashlight history");
        DenoiserFrameSettings frame;
        Require(bool(backend->Execute(handles[4], nullptr, frame)) &&
            (*states)[4]->executeCalls == 1 &&
            (*states)[3]->executeCalls == 0,
            "flashlight execution leaked into sun history");
        Require(backend->UnregisterSignal(handles[0]) &&
            (*states)[0]->shutdownCalls == 1,
            "signal unregistration did not release its backend");
    }

    void TestUnavailableFactory()
    {
        std::unique_ptr<IDenoiserBackend> backend = CreateDenoiserBackend();
        Require(backend != nullptr,
            "factory must always return a backend object");
#if UVSR_WITH_NRD
        Require(backend->GetCapabilities().backendAvailable,
            "compiled NRD backend reports unavailable");
        Require(backend->GetCapabilities().maximumReblurHistoryLength == 63 &&
            backend->GetCapabilities().maximumRelaxHistoryLength == 255 &&
            backend->GetCapabilities().maximumSigmaHistoryLength == 7,
            "compiled NRD method limits do not match v4.17.3");
        Require(backend->Initialize(nullptr, 2).code ==
                DenoiserStatusCode::InvalidArgument,
            "compiled backend must reject a null device safely");
#else
        Require(!backend->GetCapabilities().backendAvailable,
            "NRD off stub reports available");
        Require(backend->GetMemoryStats().TotalBytes() == 0,
            "NRD off stub allocated backend memory");
        Require(backend->Initialize(nullptr, 2).code ==
                DenoiserStatusCode::Unavailable,
            "NRD off stub did not fail open as unavailable");
#endif
    }

#if UVSR_WITH_NRD
    void TestTypedUavPolicy()
    {
        const nvrhi::FormatSupport complete =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        Require(HasNrdTypedUavReadWriteSupport(complete),
            "complete typed UAV support was rejected");
        Require(!HasNrdTypedUavReadWriteSupport(
                complete & ~nvrhi::FormatSupport::ShaderUavLoad),
            "typed UAV loads were not required");
    }
#endif
}

int main()
{
    TestDrawerSettingsDefaults();
    TestResolutionAndFrameHelpers();
    TestQualityPresets();
    TestMethodAndResourceContracts();
    TestAliasContract();
    TestIndependentSignalRegistry();
    TestUnavailableFactory();
#if UVSR_WITH_NRD
    TestTypedUavPolicy();
#endif
    std::cout << "Denoiser backend validation passed\n";
    return EXIT_SUCCESS;
}
