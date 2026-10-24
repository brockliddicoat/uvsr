#include "image_based_lighting_radiance.h"
#include "image_based_lighting_asset_path.h"
#include "renderer_scene_load_worker.h"
#include <Windows.h>
#include <stb_image_write.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

namespace
{
    using namespace uvsr;
    unsigned checks = 0;
    void Require(bool condition, const char* text)
    {
        if (!condition) { fprintf(stderr, "IBL ownership failed: %s\n", text); exit(1); }
        ++checks;
    }
    using Prepared = PreparedImageBasedLightingRadiance;
    using Error = ImageBasedLightingRadianceError;
    struct WorkerCandidate
    {
        const char* path = nullptr;
        bool failAllocation = false, published = false;
        Prepared radiance;
        static bool Run(void* context, const RendererSceneLoadCancellation& cancellation)
        {
            auto& self = *static_cast<WorkerCandidate*>(context);
            Prepared local;
            if (self.failAllocation) FailImageBasedLightingRadianceAllocationAfter(0);
            const auto result = PrepareImageBasedLightingRadiance(self.path,
                ImageBasedLightingSource::Kloppenheim03Day, false, local);
            ClearImageBasedLightingRadianceAllocationFailure();
            if (result.error == Error::Allocation) return cancellation.Fail("IBL checked storage failure");
            if (cancellation.IsRequested()) return false;
            self.radiance = static_cast<Prepared&&>(local);
            self.published = true;
            return true;
        }
    };
}

bool TestImageBasedLightingOwnership(const wchar_t* directory)
{
    static_assert(!std::is_copy_constructible_v<Prepared> && std::is_nothrow_move_constructible_v<Prepared> &&
        std::is_nothrow_move_assignable_v<Prepared>);
    static_assert(sizeof(ImageBasedLightingHalf4) == 8);
    Require(setlocale(LC_ALL, "C") != nullptr, "fixture locale"); SetFileApisToANSI();
    Require(CreateDirectoryW(directory, nullptr) != FALSE, "fresh IBL fixture directory");
    WindowsPath subdirectory; WindowsPathResult wideResult;
    Require(JoinWindowsRelativePath(directory, L"kloppenheim_03_puresky", subdirectory, wideResult) &&
        CreateDirectoryW(subdirectory.Data(), nullptr), "fresh HDR fixture directory");
    ImageBasedLightingAssetPath path; ImageBasedLightingPathResult pathResult;
    Require(path.Prepare(directory, ImageBasedLightingSource::Kloppenheim03Day, pathResult), "native asset path");
    char missing[4096];
    const int missingSize = snprintf(missing, sizeof(missing), "%s.missing", path.Text());
    Require(missingSize > 0 && size_t(missingSize) < sizeof(missing), "missing fixture path capacity");
    float pixels[24];
    for (unsigned i = 0; i < 24; ++i) pixels[i] = float(i % 3 + 1) * .25f;
    Require(stbi_write_hdr(path.Text(), 4, 2, 3, pixels) != 0, "HDR fixture write");

    Prepared value;
    auto result = PrepareImageBasedLightingRadiance(path.Text(), ImageBasedLightingSource::Kloppenheim03Day, false, value);
    Require(result.error == Error::None && value && value.Faces().Size() == size_t(6) * 512 * 512,
        "complete radiance table");
    Prepared independent;
    result = PrepareImageBasedLightingRadiance(path.Text(), ImageBasedLightingSource::Kloppenheim03Day, false, independent);
    Require(result.error == Error::None && independent.Faces().Data() != value.Faces().Data() &&
        memcmp(independent.Faces().Data(), value.Faces().Data(), value.Faces().Size() * 8) == 0, "independent table bytes");
    const auto* bytes = value.Faces().Data();
    FailImageBasedLightingRadianceAllocationAfter(0);
    result = PrepareImageBasedLightingRadiance(path.Text(), ImageBasedLightingSource::QuadrangleCloudy, true, value);
    ClearImageBasedLightingRadianceAllocationFailure();
    Require(result.error == Error::Allocation && value.Faces().Data() == bytes &&
        value.Source() == ImageBasedLightingSource::Kloppenheim03Day && !value.Neutralize() &&
        memcmp(bytes, independent.Faces().Data(), value.Faces().Size() * 8) == 0,
        "allocation failure changed published state");
    result = PrepareImageBasedLightingRadiance(missing, ImageBasedLightingSource::Kloppenheim03Day, true, value);
    Require(result.Unavailable() && result.error == Error::Decode && value.Faces().Data() == bytes,
        "decode rejection changed output");
    Require(stbi_write_hdr(path.Text(), 3, 2, 3, pixels) != 0, "shape fixture write");
    result = PrepareImageBasedLightingRadiance(path.Text(), ImageBasedLightingSource::Kloppenheim03Day, true, value);
    Require(result.error == Error::Dimensions && result.Unavailable() && value.Faces().Data() == bytes,
        "shape rejection changed output");
    float black[24]{};
    Require(stbi_write_hdr(path.Text(), 4, 2, 3, black) != 0, "black fixture write");
    result = PrepareImageBasedLightingRadiance(path.Text(), ImageBasedLightingSource::Kloppenheim03Day, true, value);
    Require(result.error == Error::Projection && result.Unavailable() && value.Faces().Data() == bytes,
        "projection rejection changed output");
    Require(stbi_write_hdr(path.Text(), 4, 2, 3, pixels) != 0, "restore HDR fixture");
    value = static_cast<Prepared&&>(value);
    Require(value.Faces().Data() == bytes, "self move changed ownership");
    Prepared workerLocal(static_cast<Prepared&&>(value));
    Require(!value && workerLocal.Faces().Data() == bytes, "local to candidate move");
    Prepared staged;
    staged = static_cast<Prepared&&>(workerLocal);
    Require(!workerLocal && staged.Faces().Data() == bytes, "candidate to staged move");
    value.Clear(); workerLocal.Clear();
    Require(staged.Faces().Data() == bytes && memcmp(bytes, independent.Faces().Data(), staged.Faces().Size() * 8) == 0,
        "clearing moved owners changed staged bytes");
    staged.Clear();
    Require(!staged && independent && independent.Faces().Size() == size_t(6) * 512 * 512, "independent clear");
    ImageBasedLightingFaces diffuse;
    Require(PrepareImageBasedLightingDiffuseFaces(independent.DiffuseSh(), diffuse) && diffuse.Size() == size_t(6) * 16 * 16,
        "complete diffuse table");
    const auto* diffuseBytes = diffuse.Data();
    FailImageBasedLightingRadianceAllocationAfter(0);
    Require(!PrepareImageBasedLightingDiffuseFaces(independent.DiffuseSh(), diffuse) && diffuse.Data() == diffuseBytes,
        "diffuse allocation changed previous output");
    ClearImageBasedLightingRadianceAllocationFailure();
    ImageBasedLightingFaces movedDiffuse(static_cast<ImageBasedLightingFaces&&>(diffuse));
    Require(!diffuse.Data() && movedDiffuse.Data() == diffuseBytes, "diffuse move");

    const char* text = path.Text(); const wchar_t* wide = path.NativePath().Data();
    FailWindowsPathOnce(WindowsPathError::Allocation);
    Require(!path.Prepare(directory, ImageBasedLightingSource::QuadrangleCloudy, pathResult) &&
        pathResult.error == ImageBasedLightingPathError::Allocation && path.Text() == text && path.NativePath().Data() == wide,
        "wide allocation changed path");
    FailWindowsPathTextAllocationAfter(0);
    Require(!path.Prepare(directory, ImageBasedLightingSource::QuadrangleCloudy, pathResult) &&
        pathResult.error == ImageBasedLightingPathError::Allocation && path.Text() == text && path.NativePath().Data() == wide,
        "native encoding allocation changed path");
    ClearWindowsPathTextAllocationFailure();
    for (size_t fail = 0; fail < 2; ++fail)
    {
        FailWindowsPathTextAllocationAfter(fail);
        Require(!path.MakeGeneric(pathResult) && pathResult.error == ImageBasedLightingPathError::Allocation &&
            path.Text() == text && path.NativePath().Data() == wide, "generic allocation changed path");
        ClearWindowsPathTextAllocationFailure();
    }
    Require(!path.Prepare(nullptr, ImageBasedLightingSource::Kloppenheim03Day, pathResult) && path.Text() == text,
        "invalid directory changed path");
    FailWindowsPathTextAllocationAfter(1);
    ImageBasedLightingAssetPath successPath;
    Require(successPath.Prepare(directory, ImageBasedLightingSource::Kloppenheim03Day, pathResult), "one native allocation");
    Prepared success;
    result = PrepareImageBasedLightingRadiance(successPath.Text(), ImageBasedLightingSource::Kloppenheim03Day, false, success);
    Require(result.error == Error::None && success, "successful preparation eagerly allocated generic text");
    Require(!successPath.MakeGeneric(pathResult) && pathResult.error == ImageBasedLightingPathError::Allocation,
        "generic allocation hook was not still pending");
    ClearWindowsPathTextAllocationFailure();

    RendererSceneLoadWorker worker;
    WorkerCandidate available; available.path = path.Text();
    Require(worker.Start(WorkerCandidate::Run, &available) && worker.Join() && available.published && available.radiance,
        "worker did not publish prepared radiance after CPU completion");
    WorkerCandidate unavailable; unavailable.path = missing;
    Require(worker.Start(WorkerCandidate::Run, &unavailable) && worker.Join() && unavailable.published && !unavailable.radiance,
        "ordinary IBL rejection failed the scene worker");
    WorkerCandidate failure; failure.path = path.Text(); failure.failAllocation = true;
    Require(worker.Start(WorkerCandidate::Run, &failure) && !worker.Join() && !failure.published && !failure.radiance &&
        worker.GetState() == RendererSceneLoadWorkerState::Failed &&
        strcmp(worker.GetFailureText(), "IBL checked storage failure") == 0, "checked failure published the scene candidate");
    worker.Reset();
    printf("IBL ownership: %u assertions\n", checks);
    return true;
}
