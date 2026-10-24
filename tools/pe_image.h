#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr
{
    class PeImage
    {
    public:
        IMAGE_FILE_HEADER fileHeader{};
        IMAGE_OPTIONAL_HEADER64 optionalHeader{};
        std::vector<IMAGE_SECTION_HEADER> sections;

        explicit PeImage(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
                throw std::runtime_error("cannot open the PE image");
            const std::streamoff length = stream.tellg();
            if (length < static_cast<std::streamoff>(sizeof(IMAGE_DOS_HEADER)) ||
                length > static_cast<std::streamoff>(1024u * 1024u * 1024u))
                throw std::runtime_error("PE image size is outside its limit");
            m_Bytes.resize(static_cast<std::size_t>(length));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char*>(m_Bytes.data()), length);
            if (!stream)
                throw std::runtime_error("cannot read the PE image");

            const auto dos = ReadAt<IMAGE_DOS_HEADER>(0u);
            if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
                throw std::runtime_error("the image has no valid DOS header");
            const std::size_t ntOffset = static_cast<std::size_t>(dos.e_lfanew);
            if (ReadAt<DWORD>(ntOffset) != IMAGE_NT_SIGNATURE)
                throw std::runtime_error("the image has no valid NT header");
            fileHeader = ReadAt<IMAGE_FILE_HEADER>(ntOffset + sizeof(DWORD));
            if (fileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
                fileHeader.NumberOfSections == 0u || fileHeader.NumberOfSections > 96u ||
                fileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64))
                throw std::runtime_error("the image is not a bounded PE32+ image");
            const std::size_t optionalOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
            optionalHeader = ReadAt<IMAGE_OPTIONAL_HEADER64>(optionalOffset);
            if (optionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                throw std::runtime_error("the image has no PE32+ optional header");
            const std::size_t sectionOffset = optionalOffset + fileHeader.SizeOfOptionalHeader;
            for (unsigned index = 0; index < fileHeader.NumberOfSections; ++index)
                sections.push_back(ReadAt<IMAGE_SECTION_HEADER>(
                    sectionOffset + std::size_t(index) * sizeof(IMAGE_SECTION_HEADER)));
        }

        template<typename T>
        [[nodiscard]] T ReadAt(std::size_t offset, std::string_view description = "value") const
        {
            if (offset > m_Bytes.size() || sizeof(T) > m_Bytes.size() - offset)
                throw std::runtime_error("PE image is truncated at " + std::string(description));
            T result{};
            std::memcpy(&result, m_Bytes.data() + offset, sizeof(T));
            return result;
        }

        [[nodiscard]] std::size_t FileOffset(DWORD rva, std::size_t size,
            std::string_view description = "RVA") const
        {
            if (rva < optionalHeader.SizeOfHeaders &&
                rva <= m_Bytes.size() && size <= m_Bytes.size() - rva)
                return rva;
            for (const IMAGE_SECTION_HEADER& section : sections)
            {
                const std::uint64_t begin = section.VirtualAddress;
                const std::uint64_t span = std::max<std::uint64_t>(
                    section.Misc.VirtualSize, section.SizeOfRawData);
                const std::uint64_t address = rva;
                if (address < begin || address - begin >= span)
                    continue;
                const std::uint64_t delta = address - begin;
                if (delta > section.SizeOfRawData || size > section.SizeOfRawData - delta)
                    break;
                const std::uint64_t offset = std::uint64_t(section.PointerToRawData) + delta;
                if (offset <= m_Bytes.size() && size <= m_Bytes.size() - offset)
                    return static_cast<std::size_t>(offset);
                break;
            }
            throw std::runtime_error("PE image has an invalid " + std::string(description));
        }

        template<typename T>
        [[nodiscard]] T ReadRva(DWORD rva, std::uint32_t index = 0) const
        {
            const std::uint64_t address = std::uint64_t(rva) + std::uint64_t(index) * sizeof(T);
            if (address > std::numeric_limits<DWORD>::max())
                throw std::runtime_error("PE array RVA overflow");
            return ReadAt<T>(FileOffset(static_cast<DWORD>(address), sizeof(T)));
        }

        [[nodiscard]] std::string ReadString(DWORD rva, std::uint32_t limit = 4096) const
        {
            std::string result;
            for (std::uint32_t index = 0; index < limit; ++index)
            {
                const char character = ReadRva<char>(rva, index);
                if (character == '\0')
                    return result;
                result.push_back(character);
            }
            throw std::runtime_error("PE string is not bounded");
        }

    private:
        std::vector<unsigned char> m_Bytes;
    };
}
