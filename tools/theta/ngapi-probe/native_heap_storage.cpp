// U-013. Actual NGAPI caller for the generic RustGPU storage-image fixture.
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include "native_heap_storage_modules.hpp"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

using gpu::uint32;
using gpu::uint64;

constexpr uint32 guard = 0xa5c37e19u;
constexpr uint64 output_bytes = 32, texture_bytes = 64, readback_bytes = 160;
struct StorageRoot { uint64 output; uint32 source, destination; };
static_assert(sizeof(StorageRoot) == 16 && alignof(StorageRoot) == 8);
static_assert(offsetof(StorageRoot, output) == 0 && offsetof(StorageRoot, source) == 8 && offsetof(StorageRoot, destination) == 12);

static bool valid_output(uint64 address, uint64 size) noexcept
{
    return address && !(address & 3u) && size >= output_bytes && address <= ~uint64(0) - output_bytes;
}

static void initial_words(uint32* words, uint32 phase) noexcept
{
    for (uint32 i = 0; i < 8; ++i) words[i] = guard;
    const uint32 seed = phase < 2 ? 0x12345678u : 0xfffffff0u;
    for (uint32 texture = 0; texture < 2; ++texture)
        for (uint32 i = 0; i < 16; ++i) words[8 + 16 * texture + i] = seed + 0x100u * texture + 3u * i;
}

static void expected_words(uint32* words, uint32 phase) noexcept
{
    initial_words(words, phase);
    const uint32 source = phase & 1u, destination = source ^ 1u;
    for (uint32 i = 0; i < 4; ++i) {
        const uint32 value = words[8 + 16 * source + 4 + i] + i + 1u;
        words[i] = value;
        words[8 + 16 * destination + 8 + i] = value;
    }
}

static int first_mismatch(const uint32* actual, uint32 phase) noexcept
{
    uint32 expected[40]{};
    expected_words(expected, phase);
    for (int i = 0; i < 40; ++i) if (actual[i] != expected[i]) return i;
    return -1;
}

static bool self_test() noexcept
{
    constexpr uint64 address = 0x1234567800000040ull;
    const bool controls[]{valid_output(address, 32), !valid_output(0, 32), !valid_output(address + 1, 32),
        !valid_output(address, 31), !valid_output(~uint64(0) - 3, 32)};
    bool valid = true;
    for (bool control : controls) valid &= control;
    for (uint32 phase = 0; phase < 4; ++phase) {
        uint32 words[40]{};
        expected_words(words, phase);
        valid &= first_mismatch(words, phase) == -1;
        for (int i = 0; i < 40; ++i) {
            words[i] ^= 1u;
            valid &= first_mismatch(words, phase) == i;
            words[i] ^= 1u;
        }
    }
    printf("{\"case_id\":\"theta.m2.ngapi.native_heap_storage.controls\",\"status\":\"%s\","
           "\"checks\":169,\"shader_cases_executed\":0}\n", valid ? "pass" : "fail");
    fflush(stdout);
    return valid;
}

static bool run_variant(gpu::Device* device, gpu::Span<const uint32> shader, const char* hash, const char* variant) noexcept
{
    const gpu::DeviceCaps& caps = gpu::get_device_caps(device);
    constexpr gpu::TextureDesc desc{
        .extent = {.x = 2, .y = 2, .z = 1}, .format = gpu::Format::rgba32_uint,
        .usage = gpu::TextureUsage::storage | gpu::TextureUsage::transfer_source | gpu::TextureUsage::transfer_destination,
    };
    const bool format_supported = gpu::supports_texture_format(device, desc.format, desc.usage);
    if (!format_supported || caps.max_push_data_size < sizeof(StorageRoot)
        || !caps.texture_descriptor_size || caps.texture_descriptor_size > ~uint64(0) / 4) {
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_storage.requirements.%s\",\"status\":\"blocked\","
               "\"format_supported\":%s,\"image_descriptor_bytes\":%llu,\"max_push_data_size\":%llu}\n",
            variant, format_supported ? "true" : "false", static_cast<unsigned long long>(caps.texture_descriptor_size),
            static_cast<unsigned long long>(caps.max_push_data_size));
        return false;
    }
    const gpu::SizeAlign required = gpu::get_texture_size_align(device, desc);
    if (!required.size || !required.align || (required.align & (required.align - 1))
        || required.align > ~uint64(0) / 2 || required.size > ~uint64(0) / 2 - required.align) {
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_storage.placement.%s\",\"status\":\"fail\"}\n", variant);
        return false;
    }
    const uint64 stride = (required.size + required.align - 1) & ~(required.align - 1);
    const gpu::TextureHeap storage = gpu::create_texture_heap(device, stride * 2);
    gpu::Texture* textures[]{gpu::create_texture(device, desc, storage, 0), gpu::create_texture(device, desc, storage, stride)};
    const gpu::GpuHeap descriptors = gpu::create_gpu_heap(device, caps.texture_descriptor_size * 4, gpu::MemoryType::texture_descriptor_heap);
    const gpu::GpuHeap upload = gpu::create_gpu_heap(device, readback_bytes);
    const gpu::GpuHeap output = gpu::create_gpu_heap(device, output_bytes, gpu::MemoryType::gpu_only);
    const gpu::GpuHeap readback = gpu::create_gpu_heap(device, readback_bytes, gpu::MemoryType::readback);
    gpu::PSO* pso = gpu::create_compute_pso(device, shader);
    gpu::TimelinePoint completion{.semaphore = gpu::create_timeline_semaphore(device)};
    const uint64 address = reinterpret_cast<uint64>(output.range.gpu);
    bool valid = textures[0] && textures[1] && descriptors.range.cpu && descriptors.range.gpu
        && descriptors.range.size == caps.texture_descriptor_size * 4 && upload.range.cpu && readback.range.cpu
        && upload.range.size >= readback_bytes && readback.range.size >= readback_bytes
        && valid_output(address, output.range.size) && pso && completion.semaphore;
    if (!valid)
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_storage.preconditions.%s\",\"status\":\"fail\"}\n", variant);
    if (valid)
        for (uint32 slot = 0; slot < 4; ++slot)
            gpu::write_texture_descriptor(device, descriptors.range.cpu + slot * caps.texture_descriptor_size,
                textures[slot == 3 ? 1 : 0], gpu::TextureDescriptorType::storage);
    for (uint32 phase = 0; valid && phase < 4; ++phase) {
        const StorageRoot root{address, phase & 1u ? 3u : 1u, phase & 1u ? 1u : 3u};
        uint32 initialized[40]{};
        initial_words(initialized, phase);
        memcpy(upload.range.cpu, initialized, sizeof(initialized));
        memset(readback.range.cpu, 0xcc, readback_bytes);
        // U-013: allocations/descriptors remain live through this completed
        // submission. Every command buffer is submitted once. No map escapes.
        gpu::CommandBuffer* commands = gpu::begin_commands(device);
        gpu::copy_memory(commands, {.gpu = upload.range.gpu, .size = output_bytes}, gpu::gpu_range(output));
        gpu::copy_memory_to_texture(commands, {.gpu = upload.range.gpu + output_bytes, .size = texture_bytes}, textures[0]);
        gpu::copy_memory_to_texture(commands, {.gpu = upload.range.gpu + output_bytes + texture_bytes, .size = texture_bytes}, textures[1]);
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write,
            gpu::Stage::compute, gpu::Access::shader_read | gpu::Access::shader_write);
        gpu::set_texture_descriptor_heap(commands, gpu::gpu_range(descriptors));
        gpu::bind_pso(commands, pso);
        gpu::dispatch(commands, root, {.x = 1, .y = 1, .z = 1});
        gpu::barrier(commands, gpu::Stage::compute | gpu::Stage::transfer,
            gpu::Access::shader_write | gpu::Access::transfer_write, gpu::Stage::transfer, gpu::Access::transfer_read);
        gpu::copy_memory(commands, gpu::gpu_range(output), {.gpu = readback.range.gpu, .size = output_bytes});
        gpu::copy_texture_to_memory(commands, textures[0], {.gpu = readback.range.gpu + output_bytes, .size = texture_bytes});
        gpu::copy_texture_to_memory(commands, textures[1], {.gpu = readback.range.gpu + output_bytes + texture_bytes, .size = texture_bytes});
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write, gpu::Stage::host, gpu::Access::host_read);
        ++completion.value;
        gpu::submit({commands}, completion);
        gpu::wait_timeline(completion);
        uint32 actual[40]{};
        memcpy(actual, readback.range.cpu, sizeof(actual));
        const int mismatch = first_mismatch(actual, phase);
        valid = mismatch == -1;
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_storage.%s.phase%u\",\"status\":\"%s\","
               "\"source_sha256\":\"%s\",\"payload_sha256\":\"%s\",\"output_address\":\"0x%016llx\","
               "\"source_index\":%u,\"destination_index\":%u,\"image_descriptor_bytes\":%llu,\"heap_slots\":4,"
               "\"completion\":%llu,\"first_mismatch_word\":%d,\"shader_cases_executed\":1,\"actual\":[",
            variant, phase, valid ? "pass" : "fail", native_heap_storage_source_sha256, hash,
            static_cast<unsigned long long>(address), root.source, root.destination,
            static_cast<unsigned long long>(caps.texture_descriptor_size), static_cast<unsigned long long>(completion.value), mismatch);
        for (uint32 i = 0; i < 40; ++i) printf("%s%u", i ? "," : "", actual[i]);
        printf("]}\n");
        fflush(stdout);
    }
    gpu::wait_idle(device);
    gpu::destroy_timeline_semaphore(completion.semaphore);
    gpu::destroy_pso(pso);
    gpu::destroy_gpu_heap(readback);
    gpu::destroy_gpu_heap(output);
    gpu::destroy_gpu_heap(upload);
    gpu::destroy_gpu_heap(descriptors);
    gpu::destroy_texture(textures[1]);
    gpu::destroy_texture(textures[0]);
    gpu::destroy_texture_heap(storage);
    return valid;
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--identity") == 0) {
        printf("{\"source_sha256\":\"%s\",\"host_sha256\":\"%s\",\"payloads\":{"
               "\"default_opt0\":\"%s\",\"default_opt3\":\"%s\",\"qptr_opt0\":\"%s\",\"qptr_opt3\":\"%s\"}}\n",
            native_heap_storage_source_sha256, native_heap_storage_host_sha256,
            native_heap_storage_default_opt0_sha256, native_heap_storage_default_opt3_sha256,
            native_heap_storage_qptr_opt0_sha256, native_heap_storage_qptr_opt3_sha256);
        return 0;
    }
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--self-test") != 0)) return 2;
    if (!self_test()) return 1;
    if (argc == 2) return 0;
    const gpu::DeviceInit init = gpu::create_device({.timestamp_query_count = 0});
    if (init.error != gpu::Error::none || !init.device) {
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_storage.device\",\"status\":\"blocked\",\"error\":%u}\n",
            static_cast<unsigned>(init.error));
        return init.error == gpu::Error::unsupported ? 77 : 1;
    }
    const bool valid = run_variant(init.device, native_heap_storage_default_opt0, native_heap_storage_default_opt0_sha256, "default_opt0")
        && run_variant(init.device, native_heap_storage_default_opt3, native_heap_storage_default_opt3_sha256, "default_opt3")
        && run_variant(init.device, native_heap_storage_qptr_opt0, native_heap_storage_qptr_opt0_sha256, "qptr_opt0")
        && run_variant(init.device, native_heap_storage_qptr_opt3, native_heap_storage_qptr_opt3_sha256, "qptr_opt3");
    gpu::wait_idle(init.device);
    gpu::destroy_device(init.device);
    return valid ? 0 : 1;
}
