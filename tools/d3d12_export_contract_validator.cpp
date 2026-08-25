#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct RuntimeContract
    {
        std::uint32_t sdkVersion = 0u;
        std::string sdkPath;
    };

    class PeImage
    {
    public:
        explicit PeImage(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
                throw std::runtime_error("cannot open the PE image");
            const std::streamoff length = stream.tellg();
            if (length <= 0 ||
                static_cast<std::uintmax_t>(length) >
                    std::numeric_limits<std::size_t>::max())
            {
                throw std::runtime_error("the PE image size is invalid");
            }
            m_Bytes.resize(static_cast<std::size_t>(length));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char*>(m_Bytes.data()), length);
            if (!stream)
                throw std::runtime_error("cannot read the PE image");

            const IMAGE_DOS_HEADER dos = ReadAt<IMAGE_DOS_HEADER>(0u);
            if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
                throw std::runtime_error("the image has no valid DOS header");
            const std::size_t ntOffset = static_cast<std::size_t>(dos.e_lfanew);
            if (ReadAt<DWORD>(ntOffset) != IMAGE_NT_SIGNATURE)
                throw std::runtime_error("the image has no valid NT header");
            const IMAGE_FILE_HEADER file = ReadAt<IMAGE_FILE_HEADER>(
                CheckedAdd(ntOffset, sizeof(DWORD)));
            if (file.Machine != IMAGE_FILE_MACHINE_AMD64 ||
                file.NumberOfSections == 0u || file.NumberOfSections > 96u)
            {
                throw std::runtime_error("the image is not a bounded x64 PE");
            }
            const std::size_t optionalOffset = CheckedAdd(
                CheckedAdd(ntOffset, sizeof(DWORD)), sizeof(IMAGE_FILE_HEADER));
            if (file.SizeOfOptionalHeader <
                static_cast<WORD>(sizeof(IMAGE_OPTIONAL_HEADER64)))
                throw std::runtime_error("the PE32+ optional header is truncated");
            m_Optional = ReadAt<IMAGE_OPTIONAL_HEADER64>(optionalOffset);
            if (m_Optional.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
                m_Optional.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
            {
                throw std::runtime_error("the image has no PE32+ export directory");
            }
            m_Export = m_Optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            if (m_Export.VirtualAddress == 0u || m_Export.Size == 0u)
                throw std::runtime_error("the image export directory is empty");

            const std::size_t sectionOffset = CheckedAdd(
                optionalOffset, file.SizeOfOptionalHeader);
            m_Sections.reserve(file.NumberOfSections);
            for (unsigned index = 0u; index < file.NumberOfSections; ++index)
            {
                m_Sections.push_back(ReadAt<IMAGE_SECTION_HEADER>(CheckedAdd(
                    sectionOffset, CheckedMultiply(index,
                        sizeof(IMAGE_SECTION_HEADER)))));
            }
        }

        [[nodiscard]] RuntimeContract ReadRuntimeContract() const
        {
            const IMAGE_EXPORT_DIRECTORY exports =
                ReadRva<IMAGE_EXPORT_DIRECTORY>(m_Export.VirtualAddress);
            if (exports.NumberOfNames == 0u ||
                exports.NumberOfNames > 100000u ||
                exports.NumberOfFunctions == 0u ||
                exports.NumberOfFunctions > 100000u)
            {
                throw std::runtime_error("the image export count is invalid");
            }

            const DWORD versionRva = FindExport(exports, "D3D12SDKVersion");
            const DWORD pathRva = FindExport(exports, "D3D12SDKPath");
            const std::uint32_t version = ReadRva<std::uint32_t>(versionRva);
            const ULONGLONG pathAddress = ReadRva<ULONGLONG>(pathRva);
            if (pathAddress < m_Optional.ImageBase ||
                pathAddress - m_Optional.ImageBase >
                    std::numeric_limits<DWORD>::max())
            {
                throw std::runtime_error("D3D12SDKPath does not point into the image");
            }
            const DWORD stringRva = static_cast<DWORD>(
                pathAddress - m_Optional.ImageBase);
            return { version, ReadString(stringRva) };
        }

    private:
        [[nodiscard]] static std::size_t CheckedAdd(
            std::size_t left, std::size_t right)
        {
            if (right > std::numeric_limits<std::size_t>::max() - left)
                throw std::runtime_error("PE offset overflow");
            return left + right;
        }

        [[nodiscard]] static std::size_t CheckedMultiply(
            std::size_t left, std::size_t right)
        {
            if (left != 0u &&
                right > std::numeric_limits<std::size_t>::max() / left)
            {
                throw std::runtime_error("PE array size overflow");
            }
            return left * right;
        }

        [[nodiscard]] static DWORD CheckedRvaAdd(
            DWORD rva, std::size_t offset)
        {
            if (offset > std::numeric_limits<DWORD>::max() - rva)
                throw std::runtime_error("PE RVA overflow");
            return rva + static_cast<DWORD>(offset);
        }

        template <typename T>
        [[nodiscard]] T ReadAt(std::size_t offset) const
        {
            if (offset > m_Bytes.size() ||
                sizeof(T) > m_Bytes.size() - offset)
            {
                throw std::runtime_error("the PE image is truncated");
            }
            T result{};
            std::memcpy(&result, m_Bytes.data() + offset, sizeof(T));
            return result;
        }

        [[nodiscard]] std::size_t RvaToOffset(
            DWORD rva, std::size_t length) const
        {
            if (rva < m_Optional.SizeOfHeaders)
            {
                const std::size_t offset = rva;
                if (offset <= m_Bytes.size() &&
                    length <= m_Bytes.size() - offset)
                {
                    return offset;
                }
            }
            for (const IMAGE_SECTION_HEADER& section : m_Sections)
            {
                const std::uint64_t begin = section.VirtualAddress;
                const std::uint64_t span = (std::max)(
                    static_cast<std::uint64_t>(section.Misc.VirtualSize),
                    static_cast<std::uint64_t>(section.SizeOfRawData));
                const std::uint64_t value = rva;
                if (value < begin || value - begin >= span)
                    continue;
                const std::uint64_t delta = value - begin;
                if (delta > section.SizeOfRawData ||
                    length > section.SizeOfRawData - delta)
                {
                    throw std::runtime_error("an exported value has no file bytes");
                }
                const std::size_t offset = CheckedAdd(
                    section.PointerToRawData, static_cast<std::size_t>(delta));
                if (offset > m_Bytes.size() ||
                    length > m_Bytes.size() - offset)
                {
                    throw std::runtime_error("an exported value escapes the PE image");
                }
                return offset;
            }
            throw std::runtime_error("an exported RVA is not mapped by the PE image");
        }

        template <typename T>
        [[nodiscard]] T ReadRva(DWORD rva) const
        {
            return ReadAt<T>(RvaToOffset(rva, sizeof(T)));
        }

        [[nodiscard]] std::string ReadString(DWORD rva) const
        {
            std::string result;
            for (DWORD index = 0u; index < 4096u; ++index)
            {
                if (rva > std::numeric_limits<DWORD>::max() - index)
                    throw std::runtime_error("an exported string RVA overflowed");
                const char character = ReadRva<char>(rva + index);
                if (character == '\0')
                    return result;
                result.push_back(character);
            }
            throw std::runtime_error("an exported string is not bounded");
        }

        [[nodiscard]] DWORD FindExport(
            const IMAGE_EXPORT_DIRECTORY& exports,
            std::string_view expectedName) const
        {
            DWORD result = 0u;
            bool found = false;
            for (DWORD index = 0u; index < exports.NumberOfNames; ++index)
            {
                const DWORD nameRva = ReadRva<DWORD>(
                    CheckedRvaAdd(exports.AddressOfNames,
                        CheckedMultiply(index, sizeof(DWORD))));
                if (ReadString(nameRva) != expectedName)
                    continue;
                if (found)
                    throw std::runtime_error("a required PE export is duplicated");
                const WORD ordinal = ReadRva<WORD>(
                    CheckedRvaAdd(exports.AddressOfNameOrdinals,
                        CheckedMultiply(index, sizeof(WORD))));
                if (ordinal >= exports.NumberOfFunctions)
                    throw std::runtime_error("a required PE export has an invalid ordinal");
                result = ReadRva<DWORD>(CheckedRvaAdd(
                    exports.AddressOfFunctions,
                    CheckedMultiply(ordinal, sizeof(DWORD))));
                const std::uint64_t exportBegin = m_Export.VirtualAddress;
                const std::uint64_t exportEnd = exportBegin + m_Export.Size;
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

        std::vector<unsigned char> m_Bytes;
        IMAGE_OPTIONAL_HEADER64 m_Optional{};
        IMAGE_DATA_DIRECTORY m_Export{};
        std::vector<IMAGE_SECTION_HEADER> m_Sections;
    };

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
                PeImage(arguments[2]).ReadRuntimeContract();
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
                PeImage(arguments[2]).ReadRuntimeContract();
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
