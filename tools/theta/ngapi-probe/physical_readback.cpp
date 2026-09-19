// Bounded actual-NGAPI diagnostic. This is not the Rust AGFX port.
#include "physical_readback.hpp"
#include "physical_readback_modules.hpp"
#include <stdio.h>
#include <string.h>

using gpu::uint32;
using gpu::uint64;

constexpr uint32 guard = 0xa5c37e19u;
constexpr uint64 allocation_bytes = 16;

static bool valid_ranges(uint64 source, uint64 destination, uint64 source_bytes, uint64 destination_bytes) noexcept
{
    if (!source || !destination || (source & 3u) || (destination & 3u)
        || source_bytes < allocation_bytes || destination_bytes < allocation_bytes
        || source > ~uint64(0) - allocation_bytes || destination > ~uint64(0) - allocation_bytes) return false;
    return source + allocation_bytes <= destination || destination + allocation_bytes <= source;
}

static int first_mismatch(const uint32* actual, uint32 seed, uint64 address) noexcept
{
    const uint32 expected[]{seed + 7u, uint32(address), uint32(address >> 32), guard, seed, guard, guard, guard};
    for (int index = 0; index < 8; ++index)
        if (actual[index] != expected[index]) return index;
    return -1;
}

static bool self_test() noexcept
{
    constexpr uint64 address = 0x1234567812340000ull;
    constexpr uint32 seed = 0xfffffffcu;
    uint32 words[]{3u, uint32(address & 0xffffffffu), uint32(address >> 32), guard, seed, guard, guard, guard};
    bool valid = first_mismatch(words, seed, address) == -1
        && valid_ranges(address, address + 16, 16, 16)
        && !valid_ranges(address, address + 12, 16, 16)
        && !valid_ranges(0, address, 16, 16)
        && !valid_ranges(address + 1, address + 16, 16, 16)
        && !valid_ranges(address, address + 16, 4, 16)
        && !valid_ranges(address, address + 16, 16, 12)
        && !valid_ranges(~uint64(0) - 3, address, 16, 16);
    for (int index = 0; index < 8; ++index) {
        words[index] ^= 1u;
        valid &= first_mismatch(words, seed, address) == index;
        words[index] ^= 1u;
    }
    printf("{\"case_id\":\"theta.m2.ngapi.physical_readback.controls\",\"status\":\"%s\","
           "\"checks\":16,\"shader_cases_executed\":0}\n", valid ? "pass" : "fail");
    return valid;
}

static bool run_variant(gpu::Device* device, gpu::Span<const uint32> shader, const char* shader_hash, uint32 optimization) noexcept
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
        && valid_ranges(root.source, root.destination, source.range.size, destination.range.size);
    if (!valid)
        printf("{\"case_id\":\"theta.m2.ngapi.physical_readback.preconditions.opt%u\",\"status\":\"fail\","
               "\"source_address\":\"0x%016llx\",\"destination_address\":\"0x%016llx\","
               "\"source_bytes\":%llu,\"destination_bytes\":%llu,\"upload_bytes\":%llu,\"readback_bytes\":%llu,"
               "\"upload_mapped\":%s,\"readback_mapped\":%s,\"pso_created\":%s,\"timeline_created\":%s}\n",
            optimization, static_cast<unsigned long long>(root.source), static_cast<unsigned long long>(root.destination),
            static_cast<unsigned long long>(source.range.size), static_cast<unsigned long long>(destination.range.size),
            static_cast<unsigned long long>(upload.range.size), static_cast<unsigned long long>(readback.range.size),
            upload.range.cpu ? "true" : "false", readback.range.cpu ? "true" : "false", pso ? "true" : "false",
            completion.semaphore ? "true" : "false");

    const uint32 seeds[]{0u, 0x12345678u, 0xfffffffcu};
    for (uint32 case_index = 0; valid && case_index < 3; ++case_index) {
        // U-002: all writes happen before submit. NGAPI requires coherent mapped
        // memory for these heap types. The preceding case has completed already.
        const uint32 initialized[]{seeds[case_index], guard, guard, guard, guard, guard, guard, guard};
        memcpy(upload.range.cpu, initialized, sizeof(initialized));
        memset(readback.range.cpu, 0xcc, sizeof(initialized));
        gpu::CommandBuffer* commands = gpu::begin_commands(device);
        gpu::copy_memory(commands, {.gpu = upload.range.gpu, .size = allocation_bytes}, gpu::gpu_range(source));
        gpu::copy_memory(commands, {.gpu = upload.range.gpu + allocation_bytes, .size = allocation_bytes}, gpu::gpu_range(destination));
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write,
            gpu::Stage::compute, gpu::Access::shader_read | gpu::Access::shader_write);
        gpu::bind_pso(commands, pso);
        // One invocation. Both addresses are checked, live and disjoint, and all
        // heaps/PSO/root data remain owned until recording and submission finish.
        gpu::dispatch(commands, root, {.x = 1, .y = 1, .z = 1});
        gpu::barrier(commands, gpu::Stage::compute | gpu::Stage::transfer,
            gpu::Access::shader_write | gpu::Access::transfer_write, gpu::Stage::transfer, gpu::Access::transfer_read);
        gpu::copy_memory(commands, gpu::gpu_range(destination), {.gpu = readback.range.gpu, .size = allocation_bytes});
        gpu::copy_memory(commands, gpu::gpu_range(source), {.gpu = readback.range.gpu + allocation_bytes, .size = allocation_bytes});
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write, gpu::Stage::host, gpu::Access::host_read);
        ++completion.value;
        gpu::submit({commands}, completion);
        gpu::wait_timeline(completion);
        uint32 actual[8]{};
        memcpy(actual, readback.range.cpu, sizeof(actual));
        const int mismatch = first_mismatch(actual, seeds[case_index], root.source);
        valid = mismatch < 0;
        printf("{\"case_id\":\"theta.m2.ngapi.physical_readback.opt%u.seed%u\",\"status\":\"%s\","
               "\"source_sha256\":\"%s\",\"payload_sha256\":\"%s\",\"source_address\":\"0x%016llx\","
               "\"destination_address\":\"0x%016llx\",\"nonzero_high_address_bits\":%s,\"seed\":%u,\"first_mismatch_word\":%d,"
               "\"actual\":[%u,%u,%u,%u,%u,%u,%u,%u],\"shader_cases_executed\":1}\n",
            optimization, case_index, valid ? "pass" : "fail", physical_readback_source_sha256, shader_hash,
            static_cast<unsigned long long>(root.source), static_cast<unsigned long long>(root.destination),
            (root.source >> 32) != 0 ? "true" : "false", seeds[case_index], mismatch,
            actual[0], actual[1], actual[2], actual[3], actual[4], actual[5], actual[6], actual[7]);
    }
    // U-002: drain every submission before immediate destruction, including the
    // failure-after-readback path. Native fatal/device-loss behavior remains NGAPI's.
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
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--self-test") != 0)) return 2;
    if (!self_test()) return 1;
    if (argc == 2) return 0;
    const gpu::DeviceInit init = gpu::create_device({.timestamp_query_count = 0});
    if (init.error != gpu::Error::none || !init.device) {
        printf("{\"case_id\":\"theta.m2.ngapi.physical_readback.device\",\"status\":\"blocked\",\"error\":%u}\n",
            static_cast<unsigned>(init.error));
        return init.error == gpu::Error::unsupported ? 77 : 1;
    }
    const bool first = run_variant(init.device, physical_readback_opt0, physical_readback_opt0_sha256, 0);
    const bool second = first && run_variant(init.device, physical_readback_opt3, physical_readback_opt3_sha256, 3);
    gpu::wait_idle(init.device);
    gpu::destroy_device(init.device);
    return first && second ? 0 : 1;
}
