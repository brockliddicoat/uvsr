#include "pe_image.h"

#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    struct RuntimeContract
    {
        std::uint32_t sdkVersion = 0u;
        std::string sdkPath;
    };

    [[nodiscard]] DWORD FindExport(
        const uvsr::PeImage& image,
        const IMAGE_EXPORT_DIRECTORY& exports,
        const IMAGE_DATA_DIRECTORY& exportDirectory,
        std::string_view expectedName)
    {
        DWORD result = 0u;
        bool found = false;
        for (DWORD index = 0u; index < exports.NumberOfNames; ++index)
        {
            const DWORD nameRva = image.ReadRva<DWORD>(exports.AddressOfNames, index);
            if (image.ReadString(nameRva) != expectedName)
                continue;
            if (found)
                throw std::runtime_error("a required PE export is duplicated");
            const WORD ordinal = image.ReadRva<WORD>(exports.AddressOfNameOrdinals, index);
            if (ordinal >= exports.NumberOfFunctions)
                throw std::runtime_error("a required PE export has an invalid ordinal");
            result = image.ReadRva<DWORD>(exports.AddressOfFunctions, ordinal);
            const std::uint64_t exportBegin = exportDirectory.VirtualAddress;
            const std::uint64_t exportEnd = exportBegin + exportDirectory.Size;
            if (result == 0u ||
                (result >= exportBegin && result < exportEnd))
            {
                throw std::runtime_error("a required PE export is forwarded or empty");
            }
            found = true;
        }
        if (!found)
            throw std::runtime_error("a required Direct3D export is missing");
        return result;
    }

    [[nodiscard]] RuntimeContract ReadRuntimeContract(const uvsr::PeImage& image)
    {
        if (image.optionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
            throw std::runtime_error("the image has no PE32+ export directory");
        const auto& exportDirectory = image.optionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (exportDirectory.VirtualAddress == 0u || exportDirectory.Size == 0u)
            throw std::runtime_error("the image export directory is empty");
        const IMAGE_EXPORT_DIRECTORY exports =
            image.ReadRva<IMAGE_EXPORT_DIRECTORY>(exportDirectory.VirtualAddress);
        if (exports.NumberOfNames == 0u ||
            exports.NumberOfNames > 100000u ||
            exports.NumberOfFunctions == 0u ||
            exports.NumberOfFunctions > 100000u)
        {
            throw std::runtime_error("the image export count is invalid");
        }

        const DWORD versionRva = FindExport(image, exports, exportDirectory, "D3D12SDKVersion");
        const DWORD pathRva = FindExport(image, exports, exportDirectory, "D3D12SDKPath");
        const std::uint32_t version = image.ReadRva<std::uint32_t>(versionRva);
        const ULONGLONG pathAddress = image.ReadRva<ULONGLONG>(pathRva);
        if (pathAddress < image.optionalHeader.ImageBase ||
            pathAddress - image.optionalHeader.ImageBase >
                std::numeric_limits<DWORD>::max())
        {
            throw std::runtime_error("D3D12SDKPath does not point into the image");
        }
        const DWORD stringRva = static_cast<DWORD>(
            pathAddress - image.optionalHeader.ImageBase);
        return { version, image.ReadString(stringRva) };
    }

    [[nodiscard]] std::string NarrowAscii(const wchar_t* text)
    {
        std::string result;
        for (const wchar_t character : std::wstring_view(text))
        {
            if (character > 0x7f)
                throw std::runtime_error("the expected SDK path is not ASCII");
            result.push_back(static_cast<char>(character));
        }
        return result;
    }

    [[nodiscard]] std::uint32_t ParseVersion(const wchar_t* text)
    {
        errno = 0;
        wchar_t* end = nullptr;
        const unsigned long value = std::wcstoul(text, &end, 10);
        if (errno != 0 || end == text || *end != L'\0' ||
            value > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("the expected SDK version is invalid");
        }
        return static_cast<std::uint32_t>(value);
    }

    void RequireContract(
        const RuntimeContract& actual,
        std::uint32_t expectedVersion,
        std::string_view expectedPath)
    {
        if (actual.sdkVersion != expectedVersion)
            throw std::runtime_error("D3D12SDKVersion has the wrong exported value");
        if (actual.sdkPath != expectedPath)
            throw std::runtime_error("D3D12SDKPath has the wrong exported value");
    }
}

int wmain(int argumentCount, wchar_t** arguments)
{
    try
    {
        if (argumentCount == 3 && std::wstring_view(arguments[1]) == L"--self-test")
        {
            const RuntimeContract actual =
                ReadRuntimeContract(uvsr::PeImage(arguments[2]));
            RequireContract(actual, 619u, ".\\D3D12\\");
            bool rejectedVersion = false;
            bool rejectedPath = false;
            try
            {
                RequireContract(actual, 620u, ".\\D3D12\\");
            }
            catch (const std::runtime_error&)
            {
                rejectedVersion = true;
            }
            try
            {
                RequireContract(actual, 619u, ".\\Wrong\\");
            }
            catch (const std::runtime_error&)
            {
                rejectedPath = true;
            }
            if (!rejectedVersion || !rejectedPath)
                throw std::runtime_error("the negative value checks are ineffective");
            std::cout << "Direct3D export contract self-test passed\n";
            return 0;
        }
        if (argumentCount == 5 && std::wstring_view(arguments[1]) == L"--check")
        {
            const RuntimeContract actual =
                ReadRuntimeContract(uvsr::PeImage(arguments[2]));
            RequireContract(actual, ParseVersion(arguments[3]),
                NarrowAscii(arguments[4]));
            std::cout << "Direct3D export values passed\n";
            return 0;
        }
        std::cerr << "usage: d3d12_export_contract_validator "
            "--check <uvsr-engine.exe> <sdk-version> <sdk-path> | "
            "--self-test <uvsr-engine.exe>\n";
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Direct3D export contract failed: " << error.what() << '\n';
        return 1;
    }
}
