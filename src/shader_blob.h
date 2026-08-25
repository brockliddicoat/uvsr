#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace uvsr::shader_blob
{
struct Constant
{
    const char* name;
    const char* value;
};

bool find_permutation(
    const void* blob,
    size_t blobSize,
    const Constant* constants,
    uint32_t constantCount,
    const void** binary,
    size_t* binarySize);

void enumerate_permutations(
    const void* blob,
    size_t blobSize,
    std::vector<std::string>& permutations);

std::string format_not_found_message(
    const void* blob,
    size_t blobSize,
    const Constant* constants,
    uint32_t constantCount);

bool write_header(std::ostream& output);
bool write_permutation(
    std::ostream& output,
    const std::string& key,
    const void* binary,
    size_t binarySize);
} // namespace uvsr::shader_blob
