#pragma once

#include "shader_bytecode.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace uvsr::shader_blob
{
void enumerate_permutations(
    const void* blob,
    size_t blobSize,
    std::vector<std::string>& permutations);

bool write_header(std::ostream& output);
bool write_permutation(
    std::ostream& output,
    const std::string& key,
    const void* binary,
    size_t binarySize);
} // namespace uvsr::shader_blob
