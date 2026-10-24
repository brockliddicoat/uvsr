#include "shader_bytecode.h"

#include <string.h>

namespace uvsr::shader_blob
{
    namespace
    {
        uint32_t ReadU32(const uint8_t* bytes) noexcept
        {
            return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
                uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
        }

        bool MatchPart(const char* text, const char* key, size_t keySize,
            size_t& position) noexcept
        {
            const size_t length = strlen(text);
            if (position > keySize || length > keySize - position ||
                memcmp(text, key + position, length) != 0)
                return false;
            position += length;
            return true;
        }

        bool MatchKey(const Constant* constants, uint32_t count,
            const char* key, size_t keySize) noexcept
        {
            size_t position = 0;
            uint32_t previous = UINT32_MAX;
            // shipped requests have at most two constants. selection sort over
            // borrowed names keeps the existing stable key order without storage.
            for (uint32_t ordinal = 0; ordinal < count; ++ordinal)
            {
                uint32_t next = UINT32_MAX;
                for (uint32_t index = 0; index < count; ++index)
                {
                    if (previous != UINT32_MAX)
                    {
                        const int order = strcmp(constants[index].name, constants[previous].name);
                        if (order < 0 || (order == 0 && index <= previous)) continue;
                    }
                    if (next == UINT32_MAX || strcmp(constants[index].name, constants[next].name) < 0)
                        next = index;
                }
                if (next == UINT32_MAX) return false;
                if (ordinal && (position == keySize || key[position++] != ' ')) return false;
                if (!MatchPart(constants[next].name, key, keySize, position) ||
                    position == keySize || key[position++] != '=' ||
                    !MatchPart(constants[next].value, key, keySize, position)) return false;
                previous = next;
            }
            return position == keySize;
        }
    }

    bool find_permutation(const void* blob, size_t blobSize,
        const Constant* constants, uint32_t constantCount,
        const void** binary, size_t* binarySize) noexcept
    {
        if (!blob || !blobSize || blobSize > size_t(PTRDIFF_MAX) || !binary || !binarySize ||
            (constantCount && !constants) || size_t(constantCount) > size_t(PTRDIFF_MAX) / sizeof(Constant))
            return false;
        for (uint32_t index = 0; index < constantCount; ++index)
            if (!constants[index].name || !constants[index].value) return false;
        const auto* bytes = static_cast<const uint8_t*>(blob);
        if (blobSize < 4 || memcmp(bytes, "NVSP", 4) != 0)
        {
            if (constantCount) return false;
            *binary = blob;
            *binarySize = blobSize;
            return true;
        }
        const void* selected = nullptr;
        size_t selectedSize = 0;
        size_t offset = 4;
        while (offset < blobSize)
        {
            if (blobSize - offset < 8) return false;
            const size_t keySize = ReadU32(bytes + offset);
            const size_t dataSize = ReadU32(bytes + offset + 4);
            offset += 8;
            if (!dataSize || keySize > blobSize - offset || dataSize > blobSize - offset - keySize)
                return false;
            if (!selected && MatchKey(constants, constantCount,
                    reinterpret_cast<const char*>(bytes + offset), keySize))
            {
                selected = bytes + offset + keySize;
                selectedSize = dataSize;
            }
            offset += keySize + dataSize;
        }
        if (!selected) return false;
        *binary = selected;
        *binarySize = selectedSize;
        return true;
    }
}
