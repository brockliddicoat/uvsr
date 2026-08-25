#include "shader_blob.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <ostream>
#include <sstream>

namespace uvsr::shader_blob
{
namespace
{
constexpr char kSignature[] = {'N', 'V', 'S', 'P'};
constexpr size_t kHeaderSize = sizeof(uint32_t) * 2;

bool read_u32(const uint8_t* bytes, size_t size, size_t offset, uint32_t& value)
{
    if (offset > size || size - offset < sizeof(value))
        return false;
    std::memcpy(&value, bytes + offset, sizeof(value));
    return true;
}

std::string make_key(const Constant* constants, uint32_t constantCount)
{
    if (constantCount != 0 && constants == nullptr)
        return {};
    std::vector<size_t> order(constantCount);
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [constants](size_t left, size_t right) {
        const char* leftName = constants[left].name ? constants[left].name : "";
        const char* rightName = constants[right].name ? constants[right].name : "";
        return std::strcmp(leftName, rightName) < 0;
    });

    std::ostringstream key;
    for (size_t index = 0; index < order.size(); ++index)
    {
        const Constant& constant = constants[order[index]];
        if (constant.name == nullptr || constant.value == nullptr)
            return {};
        if (index != 0)
            key << ' ';
        key << constant.name << '=' << constant.value;
    }
    return key.str();
}

template <typename Visitor>
bool visit_entries(const void* blob, size_t blobSize, Visitor&& visitor)
{
    if (blob == nullptr || blobSize < sizeof(kSignature) ||
        std::memcmp(blob, kSignature, sizeof(kSignature)) != 0)
        return false;

    const auto* bytes = static_cast<const uint8_t*>(blob);
    size_t offset = sizeof(kSignature);
    while (offset < blobSize)
    {
        uint32_t keySize = 0;
        uint32_t dataSize = 0;
        if (!read_u32(bytes, blobSize, offset, keySize) ||
            !read_u32(bytes, blobSize, offset + sizeof(uint32_t), dataSize) ||
            dataSize == 0)
            return false;
        offset += kHeaderSize;
        const size_t payloadSize = static_cast<size_t>(keySize) + dataSize;
        if (offset > blobSize || payloadSize > blobSize - offset)
            return false;
        const char* key = reinterpret_cast<const char*>(bytes + offset);
        const void* binary = bytes + offset + keySize;
        if (!visitor(key, keySize, binary, dataSize))
            return true;
        offset += payloadSize;
    }
    return offset == blobSize;
}
} // namespace

bool find_permutation(
    const void* blob,
    size_t blobSize,
    const Constant* constants,
    uint32_t constantCount,
    const void** binary,
    size_t* binarySize)
{
    if (blob == nullptr || binary == nullptr || binarySize == nullptr)
        return false;
    if (blobSize < sizeof(kSignature) ||
        std::memcmp(blob, kSignature, sizeof(kSignature)) != 0)
    {
        if (constantCount != 0)
            return false;
        *binary = blob;
        *binarySize = blobSize;
        return blobSize != 0;
    }

    const std::string requested = make_key(constants, constantCount);
    if (constantCount != 0 && requested.empty())
        return false;
    bool found = false;
    const bool valid = visit_entries(blob, blobSize,
        [&](const char* key, size_t keySize, const void* data, size_t size) {
            if (requested.size() == keySize &&
                std::memcmp(requested.data(), key, keySize) == 0)
            {
                *binary = data;
                *binarySize = size;
                found = true;
                return false;
            }
            return true;
        });
    return valid && found;
}

void enumerate_permutations(
    const void* blob,
    size_t blobSize,
    std::vector<std::string>& permutations)
{
    std::vector<std::string> discovered;
    if (!visit_entries(blob, blobSize,
            [&](const char* key, size_t keySize, const void*, size_t) {
                discovered.emplace_back(keySize == 0
                    ? "<default>"
                    : std::string(key, keySize));
                return true;
            }))
        return;
    permutations.insert(permutations.end(), discovered.begin(), discovered.end());
}

std::string format_not_found_message(
    const void* blob,
    size_t blobSize,
    const Constant* constants,
    uint32_t constantCount)
{
    std::ostringstream message;
    message << "Couldn't find the required shader permutation in the blob, "
               "or the blob is corrupted.\nRequired permutation key:\n";
    const std::string key = make_key(constants, constantCount);
    message << (key.empty() ? "<default>" : key) << '\n';

    std::vector<std::string> permutations;
    enumerate_permutations(blob, blobSize, permutations);
    if (permutations.empty())
        return message.str() + "No permutations found in the blob.";
    message << "Permutations available in the blob:\n";
    for (const std::string& permutation : permutations)
        message << permutation << '\n';
    return message.str();
}

bool write_header(std::ostream& output)
{
    output.write(kSignature, sizeof(kSignature));
    return output.good();
}

bool write_permutation(
    std::ostream& output,
    const std::string& key,
    const void* binary,
    size_t binarySize)
{
    if (binary == nullptr || binarySize == 0 ||
        key.size() > std::numeric_limits<uint32_t>::max() ||
        binarySize > std::numeric_limits<uint32_t>::max())
        return false;
    const uint32_t keySize = static_cast<uint32_t>(key.size());
    const uint32_t dataSize = static_cast<uint32_t>(binarySize);
    output.write(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
    output.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
    output.write(key.data(), static_cast<std::streamsize>(key.size()));
    output.write(static_cast<const char*>(binary),
        static_cast<std::streamsize>(binarySize));
    return output.good();
}
} // namespace uvsr::shader_blob
