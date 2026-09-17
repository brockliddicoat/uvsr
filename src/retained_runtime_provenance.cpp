#include "retained_runtime_provenance.h"
#include "build_identity.h"
#include "windows_executable_path.h"
#include "windows_file_identity.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace uvsr
{
    bool PrepareRetainedRuntimeProvenance(RetainedRuntimeProvenance& output,
        bool debugValidation, RetainedRuntimeMessage& failure) noexcept
    {
        failure = {};
        RetainedRuntimeProvenance candidate;
        const auto publish = [&](bool success) noexcept {
            output = std::move(candidate);
            return success;
        };
        candidate.settingsHash = GetBuiltSettingsNumberHash();
        candidate.engineVersion = GetBuiltEngineVersion();
        candidate.sourceCommit = GetBuiltSourceCommit();
        candidate.sourceIdentity = GetBuiltSourceIdentity();
        candidate.sourceClean = IsBuiltSourceTreeClean();
        candidate.production = IsBuiltProduction();
        candidate.configuration = GetBuiltConfiguration();
        candidate.debugLayerRequested = debugValidation;
        candidate.nvrhiValidationRequested = debugValidation;

        WindowsPath directory, executable;
        WindowsPathResult pathResult;
        WindowsPathTextResult textResult;
        if (!GetExecutableDirectoryWide(directory, pathResult) ||
            !JoinWindowsRelativePath(directory.Data(), L"uvsr-engine.exe", executable, pathResult) ||
            !candidate.executablePath.Assign(executable.Data(), executable.Size(),
                WindowsPathTextForm::Native, WindowsPathTextEncoding::Utf8, textResult))
        {
            failure = "UVSR could not identify its executable path.";
            return publish(false);
        }

        SettingsSnapshotError textError;
        // getenv may invalidate an earlier result; retain each value immediately.
        const char* packagePath = std::getenv("UVSR_RUNTIME_PACKAGE_PATH");
        if (packagePath && !candidate.packagePath.Assign(packagePath, textError))
        {
            failure = "UVSR could not retain its runtime package path.";
            return publish(false);
        }
        const char* executableSha256 = std::getenv("UVSR_RUNTIME_ENGINE_SHA256");
        if (executableSha256 && !candidate.executableSha256.Assign(executableSha256, textError))
        {
            failure = "UVSR could not retain its executable hash.";
            return publish(false);
        }
        const auto hash = candidate.executableSha256.View();
        const bool canonicalSha256 = hash.size() == 64u &&
            std::all_of(hash.begin(), hash.end(), [](unsigned char character) {
                return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
            });
        const auto package = candidate.packagePath.View();
        WindowsPath declaredPackage, declaredExecutable;
        WindowsSameFileResult identity;
        const bool executableMatchesPackage =
            DecodeWindowsUtf8Path(package.data(), package.size(), declaredPackage, pathResult) &&
            WindowsPathIsAbsolute(declaredPackage) &&
            JoinWindowsRelativePath(declaredPackage.Data(), L"bin/uvsr-engine.exe", declaredExecutable, pathResult) &&
            QueryWindowsSameFile(declaredExecutable.Data(), executable.Data(), identity) && identity.equivalent;
        if (!debugValidation || !canonicalSha256 || !executableMatchesPackage)
        {
            failure = !debugValidation ? "retained runtime verification requires -debug" :
                !canonicalSha256 ? "UVSR_RUNTIME_ENGINE_SHA256 must be 64 lowercase hexadecimal characters" :
                "UVSR_RUNTIME_PACKAGE_PATH must be the absolute package root containing this bin/uvsr-engine.exe";
            return publish(false);
        }
        return publish(true);
    }
}
