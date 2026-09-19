// U-012. Actual NGAPI consumer of the generic RustGPU aggregate/alias fixture.
#include "physical_readback.hpp"
#include "physical_operations_modules.hpp"
#include <stdio.h>
#include <string.h>

using gpu::uint32;
using gpu::uint64;

constexpr uint32 guard = 0xa5c37e19u;
constexpr uint64 allocation_bytes = 32;

struct Source {
    uint64 alias;
    uint32 values[3];
    uint32 tag;
};
static_assert(sizeof(Source) == 24 && alignof(Source) == 8);
static_assert(offsetof(Source, alias) == 0 && offsetof(Source, values) == 8 && offsetof(Source, tag) == 20);

static bool valid_ranges(uint64 source, uint64 destination, uint64 alias, uint64 source_bytes, uint64 destination_bytes) noexcept
{
    if (!source || !destination || alias != destination || (source & 7u) || (destination & 7u)
        || source_bytes < allocation_bytes || destination_bytes < allocation_bytes
        || source > ~uint64(0) - allocation_bytes || destination > ~uint64(0) - allocation_bytes) return false;
    return source + allocation_bytes <= destination || destination + allocation_bytes <= source;
}

static void expected_words(uint32* words, uint32 seed, uint64 destination) noexcept
{
    const uint32 first = seed + 1u, second = (seed ^ 0x55aa55aau) + 2u, third = seed + 22u;
    const uint32 expected[]{first, second, third, first + second + third,
        uint32(destination), uint32(destination >> 32), seed ^ 0xcedef00du, guard,
        uint32(destination), uint32(destination >> 32), seed, seed ^ 0x55aa55aau,
        seed + 19u, seed ^ 0xcedef00du, guard, guard};
    memcpy(words, expected, sizeof(expected));
}

static int first_mismatch(const uint32* actual, uint32 seed, uint64 destination) noexcept
{
    uint32 expected[16]{};
    expected_words(expected, seed, destination);
    for (int index = 0; index < 16; ++index)
        if (actual[index] != expected[index]) return index;
    return -1;
}

static bool self_test() noexcept
{
    constexpr uint64 address = 0x1234567812340000ull;
    constexpr uint32 seed = 0xfffffffcu;
    uint32 words[16]{};
    expected_words(words, seed, address + 32);
    bool valid = first_mismatch(words, seed, address + 32) == -1
        && valid_ranges(address, address + 32, address + 32, 32, 32)
        && !valid_ranges(address, address + 24, address + 24, 32, 32)
        && !valid_ranges(0, address, address, 32, 32)
        && !valid_ranges(address + 4, address + 32, address + 32, 32, 32)
        && !valid_ranges(address, address + 32, address + 32, 24, 32)
        && !valid_ranges(address, address + 32, address + 32, 32, 28)
        && !valid_ranges(~uint64(0) - 7, address, address, 32, 32)
        && !valid_ranges(address, address + 32, address + 64, 32, 32);
    for (int index = 0; index < 16; ++index) {
        words[index] ^= 1u;
        valid &= first_mismatch(words, seed, address + 32) == index;
        words[index] ^= 1u;
    }
    printf("{\"case_id\":\"theta.m2.ngapi.physical_operations.controls\",\"status\":\"%s\","
           "\"checks\":25,\"shader_cases_executed\":0}\n", valid ? "pass" : "fail");
    return valid;
}

static bool run_variant(gpu::Device* device, gpu::Span<const uint32> shader, const char* shader_hash, const char* variant) noexcept
{
    gpu::PSO* pso = gpu::create_compute_pso(device, shader);
    const gpu::GpuHeap upload = gpu::create_gpu_heap(device, allocation_bytes * 2);
    const gpu::GpuHeap source = gpu::create_gpu_heap(device, allocation_bytes, gpu::MemoryType::gpu_only);
    const gpu::GpuHeap destination = gpu::create_gpu_heap(device, allocation_bytes, gpu::MemoryType::gpu_only);
    const gpu::GpuHeap readback = gpu::create_gpu_heap(device, allocation_bytes * 2, gpu::MemoryType::readback);
    gpu::TimelinePoint completion{.semaphore = gpu::create_timeline_semaphore(device)};
    const PhysicalReadbackRoot root{
        .source = reinterpret_cast<uint64>(source.range.gpu),
        .destination = reinterpret_cast<uint64>(destination.range.gpu),
    };
    bool valid = pso && completion.semaphore && upload.range.cpu && readback.range.cpu
        && upload.range.size >= allocation_bytes * 2 && readback.range.size >= allocation_bytes * 2
        && valid_ranges(root.source, root.destination, root.destination, source.range.size, destination.range.size);
    if (!valid)
        printf("{\"case_id\":\"theta.m2.ngapi.physical_operations.preconditions.%s\",\"status\":\"fail\"}\n", variant);
    const uint32 seeds[]{0u, 0x12345678u, 0xfffffffcu};
    for (uint32 case_index = 0; valid && case_index < 3; ++case_index) {
        const uint32 seed = seeds[case_index];
        const Source data{root.destination, {seed, seed ^ 0x55aa55aau, seed + 19u}, seed ^ 0xcedef00du};
        uint32 initialized[16];
        for (auto& word : initialized) word = guard;
        memcpy(initialized, &data, sizeof(data));
        // U-012. Complete coherent upload initialization precedes this submit.
        // The previous case is completed before modifying either mapped heap.
        memcpy(upload.range.cpu, initialized, sizeof(initialized));
        memset(readback.range.cpu, 0xcc, sizeof(initialized));
        gpu::CommandBuffer* commands = gpu::begin_commands(device);
        gpu::copy_memory(commands, {.gpu = upload.range.gpu, .size = allocation_bytes}, gpu::gpu_range(source));
        gpu::copy_memory(commands, {.gpu = upload.range.gpu + allocation_bytes, .size = allocation_bytes}, gpu::gpu_range(destination));
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write,
            gpu::Stage::compute, gpu::Access::shader_read | gpu::Access::shader_write);
        gpu::bind_pso(commands, pso);
        // One invocation, two disjoint allocations, and two address values for
        // the same destination. No simultaneous invocation or reference alias.
        gpu::dispatch(commands, root, {.x = 1, .y = 1, .z = 1});
        gpu::barrier(commands, gpu::Stage::compute | gpu::Stage::transfer,
            gpu::Access::shader_write | gpu::Access::transfer_write, gpu::Stage::transfer, gpu::Access::transfer_read);
        gpu::copy_memory(commands, gpu::gpu_range(destination), {.gpu = readback.range.gpu, .size = allocation_bytes});
        gpu::copy_memory(commands, gpu::gpu_range(source), {.gpu = readback.range.gpu + allocation_bytes, .size = allocation_bytes});
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write, gpu::Stage::host, gpu::Access::host_read);
        ++completion.value;
        gpu::submit({commands}, completion);
        gpu::wait_timeline(completion);
        uint32 actual[16]{};
        memcpy(actual, readback.range.cpu, sizeof(actual));
        const int mismatch = first_mismatch(actual, seed, root.destination);
        valid = mismatch < 0;
        printf("{\"case_id\":\"theta.m2.ngapi.physical_operations.%s.seed%u\",\"status\":\"%s\","
               "\"source_sha256\":\"%s\",\"payload_sha256\":\"%s\",\"source_address\":\"0x%016llx\","
               "\"destination_address\":\"0x%016llx\",\"alias_address\":\"0x%016llx\","
               "\"nonzero_high_address_bits\":%s,\"seed\":%u,\"first_mismatch_word\":%d,\"completion\":%llu,\"actual\":[",
            variant, case_index, valid ? "pass" : "fail", physical_operations_source_sha256, shader_hash,
            static_cast<unsigned long long>(root.source), static_cast<unsigned long long>(root.destination),
            static_cast<unsigned long long>(data.alias), ((root.source | root.destination) >> 32) != 0 ? "true" : "false",
            seed, mismatch, static_cast<unsigned long long>(completion.value));
        for (uint32 index = 0; index < 16; ++index) printf("%s%u", index ? "," : "", actual[index]);
        printf("],\"shader_cases_executed\":1}\n");
    }
    // U-012. Completion precedes reuse and immediate destruction. Native fatal
    // and device-loss behavior remains NGAPI's separately documented boundary.
    gpu::wait_idle(device);
    gpu::destroy_timeline_semaphore(completion.semaphore);
    gpu::destroy_gpu_heap(readback);
    gpu::destroy_gpu_heap(destination);
    gpu::destroy_gpu_heap(source);
    gpu::destroy_gpu_heap(upload);
    gpu::destroy_pso(pso);
    return valid;
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--identity") == 0) {
        printf("{\"source_sha256\":\"%s\",\"host_sha256\":\"%s\",\"root_sha256\":\"%s\","
               "\"payloads\":{\"default_opt0\":\"%s\",\"default_opt3\":\"%s\",\"qptr_opt0\":\"%s\",\"qptr_opt3\":\"%s\"}}\n",
            physical_operations_source_sha256, physical_operations_host_sha256, physical_operations_root_sha256,
            physical_operations_default_opt0_sha256, physical_operations_default_opt3_sha256,
            physical_operations_qptr_opt0_sha256, physical_operations_qptr_opt3_sha256);
        return 0;
    }
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--self-test") != 0)) return 2;
    if (!self_test()) return 1;
    if (argc == 2) return 0;
    const gpu::DeviceInit init = gpu::create_device({.timestamp_query_count = 0});
    if (init.error != gpu::Error::none || !init.device) return init.error == gpu::Error::unsupported ? 77 : 1;
    const bool valid = run_variant(init.device, physical_operations_default_opt0, physical_operations_default_opt0_sha256, "default_opt0")
        && run_variant(init.device, physical_operations_default_opt3, physical_operations_default_opt3_sha256, "default_opt3")
        && run_variant(init.device, physical_operations_qptr_opt0, physical_operations_qptr_opt0_sha256, "qptr_opt0")
        && run_variant(init.device, physical_operations_qptr_opt3, physical_operations_qptr_opt3_sha256, "qptr_opt3");
    gpu::wait_idle(init.device);
    gpu::destroy_device(init.device);
    return valid ? 0 : 1;
}
