#include "native_heap_sample.hpp"
#include "native_heap_sample_modules.hpp"
#include <stdio.h>
#include <string.h>

using gpu::uint32;
using gpu::uint64;

constexpr uint32 guard = 0xa5c37e19u;
constexpr uint32 two = 0x40000000u; // IEEE 754 float 2.0.
constexpr uint32 pixels[]{0xff0000ffu, 0xff00ff00u, 0xff000000u, 0xff00ffffu,
                          0xffff0000u, 0xffffffffu, 0xff000000u, 0xffff00ffu};
constexpr uint32 colors[4][4]{{two, 0, 0, two}, {0, two, 0, two}, {0, 0, two, two}, {two, two, two, two}};

static bool valid_output(uint64 address, uint64 bytes) noexcept
{
    return address && !(address & 15u) && bytes >= 32 && address <= ~uint64(0) - 32;
}

static int first_mismatch(const uint32* actual, uint32 case_index) noexcept
{
    for (uint32 index = 0; index < 16; ++index) {
        const uint32 expected = index < 4 ? colors[case_index][index] : index < 8 ? guard : pixels[index - 8];
        if (actual[index] != expected) return static_cast<int>(index);
    }
    return -1;
}

static bool self_test() noexcept
{
    bool valid = true;
    for (uint32 case_index = 0; case_index < 4; ++case_index) {
        uint32 words[16]{};
        memcpy(words, colors[case_index], 16);
        for (uint32 index = 4; index < 8; ++index) words[index] = guard;
        memcpy(words + 8, pixels, sizeof(pixels));
        valid &= first_mismatch(words, case_index) == -1;
        for (uint32 index = 0; index < 16; ++index) {
            words[index] ^= 1u;
            valid &= first_mismatch(words, case_index) == static_cast<int>(index);
            words[index] ^= 1u;
        }
    }
    valid &= valid_output(0x1000, 32) && valid_output(0x1000, 64)
        && !valid_output(0, 32) && !valid_output(0x1004, 32)
        && !valid_output(0x1000, 16) && !valid_output(~uint64(0) - 15, 32);
    printf("{\"case_id\":\"theta.m2.ngapi.native_heap_sample.controls\",\"status\":\"%s\","
           "\"checks\":74,\"shader_cases_executed\":0}\n", valid ? "pass" : "fail");
    return valid;
}

static bool run_variant(gpu::Device* device, gpu::Span<const uint32> shader, const char* shader_hash, uint32 optimization) noexcept
{
    const gpu::DeviceCaps& caps = gpu::get_device_caps(device);
    constexpr gpu::TextureDesc desc{
        .extent = {.x = 2, .y = 2, .z = 1},
        .format = gpu::Format::rgba8_unorm,
        .usage = gpu::TextureUsage::sampled | gpu::TextureUsage::transfer_source | gpu::TextureUsage::transfer_destination,
    };
    const bool format_supported = gpu::supports_texture_format(device, desc.format, desc.usage);
    if (!format_supported || caps.max_push_data_size < sizeof(NativeHeapSampleRoot)
        || !caps.texture_descriptor_size || !caps.sampler_descriptor_size
        || caps.texture_descriptor_size > ~uint64(0) / 4 || caps.sampler_descriptor_size > ~uint64(0) / 4) {
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_sample.requirements.opt%u\",\"status\":\"blocked\","
               "\"format_supported\":%s,\"max_push_data_size\":%llu,\"image_descriptor_bytes\":%llu,\"sampler_descriptor_bytes\":%llu}\n",
            optimization, format_supported ? "true" : "false", static_cast<unsigned long long>(caps.max_push_data_size),
            static_cast<unsigned long long>(caps.texture_descriptor_size), static_cast<unsigned long long>(caps.sampler_descriptor_size));
        return false;
    }
    const gpu::SizeAlign required = gpu::get_texture_size_align(device, desc);
    if (!required.size || !required.align || (required.align & (required.align - 1)) || required.align > ~uint64(0) / 2
        || required.size > ~uint64(0) / 2 - required.align) {
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_sample.placement.opt%u\",\"status\":\"fail\","
               "\"texture_bytes\":%llu,\"texture_alignment\":%llu}\n", optimization,
            static_cast<unsigned long long>(required.size), static_cast<unsigned long long>(required.align));
        return false;
    }
    const uint64 texture_stride = (required.size + required.align - 1) & ~(required.align - 1);
    const gpu::TextureHeap storage = gpu::create_texture_heap(device, texture_stride * 2);
    gpu::Texture* textures[]{gpu::create_texture(device, desc, storage, 0), gpu::create_texture(device, desc, storage, texture_stride)};
    const gpu::GpuHeap resources = gpu::create_gpu_heap(device, caps.texture_descriptor_size * 4, gpu::MemoryType::texture_descriptor_heap);
    const gpu::GpuHeap samplers = gpu::create_gpu_heap(device, caps.sampler_descriptor_size * 4, gpu::MemoryType::sampler_descriptor_heap);
    const gpu::GpuHeap upload = gpu::create_gpu_heap(device, 64);
    const gpu::GpuHeap destination = gpu::create_gpu_heap(device, 32, gpu::MemoryType::gpu_only);
    const gpu::GpuHeap readback = gpu::create_gpu_heap(device, 64, gpu::MemoryType::readback);
    gpu::PSO* pso = gpu::create_compute_pso(device, shader);
    gpu::TimelinePoint completion{.semaphore = gpu::create_timeline_semaphore(device)};
    const uint64 output_address = reinterpret_cast<uint64>(destination.range.gpu);
    bool valid = textures[0] && textures[1] && resources.range.cpu && resources.range.gpu && samplers.range.cpu && samplers.range.gpu
        && resources.range.size == caps.texture_descriptor_size * 4 && samplers.range.size == caps.sampler_descriptor_size * 4
        && upload.range.cpu && upload.range.size >= 64 && readback.range.cpu && readback.range.size >= 64
        && valid_output(output_address, destination.range.size) && pso && completion.semaphore;
    if (!valid)
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_sample.preconditions.opt%u\",\"status\":\"fail\"}\n", optimization);
    if (valid) {
        // U-005: every slot is initialized. A wrong in-range index yields a
        // valid competing descriptor, whose color can fail the numeric oracle.
        for (uint32 slot = 0; slot < 4; ++slot) {
            gpu::write_texture_descriptor(device, resources.range.cpu + slot * caps.texture_descriptor_size,
                textures[slot == 3 ? 1 : 0], gpu::TextureDescriptorType::sampled);
            gpu::write_sampler_descriptor(device, samplers.range.cpu + slot * caps.sampler_descriptor_size, {
                .min_filter = gpu::Filter::nearest,
                .mag_filter = gpu::Filter::nearest,
                .mip_filter = gpu::Filter::nearest,
                .address_u = slot == 3 ? gpu::AddressMode::clamp_to_edge : gpu::AddressMode::repeat,
                .address_v = gpu::AddressMode::clamp_to_edge,
                .address_w = gpu::AddressMode::clamp_to_edge,
            });
        }
        uint32 initialized[16]{};
        memcpy(initialized, pixels, sizeof(pixels));
        for (uint32 index = 8; index < 16; ++index) initialized[index] = guard;
        memcpy(upload.range.cpu, initialized, sizeof(initialized));
    }
    for (uint32 case_index = 0; valid && case_index < 4; ++case_index) {
        const NativeHeapSampleRoot root{
            .output = output_address,
            .resource = case_index < 2 ? 1u : 3u,
            .sampler = case_index % 2 ? 3u : 2u,
        };
        memset(readback.range.cpu, 0xcc, 64);
        // The first command buffer owns NGAPI's pending GENERAL transitions.
        // Every begun buffer below is submitted exactly once and completed.
        gpu::CommandBuffer* commands = gpu::begin_commands(device);
        gpu::copy_memory_to_texture(commands, {.gpu = upload.range.gpu, .size = 16}, textures[0]);
        gpu::copy_memory_to_texture(commands, {.gpu = upload.range.gpu + 16, .size = 16}, textures[1]);
        gpu::copy_memory(commands, {.gpu = upload.range.gpu + 32, .size = 32}, gpu::gpu_range(destination));
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write,
            gpu::Stage::compute, gpu::Access::shader_read | gpu::Access::shader_write);
        gpu::set_texture_descriptor_heap(commands, gpu::gpu_range(resources));
        gpu::set_sampler_descriptor_heap(commands, gpu::gpu_range(samplers));
        gpu::bind_pso(commands, pso);
        // U-005: four validated uniform index pairs, one exclusive Vec4 write.
        gpu::dispatch(commands, root, {.x = 1, .y = 1, .z = 1});
        gpu::barrier(commands, gpu::Stage::compute | gpu::Stage::transfer,
            gpu::Access::shader_write | gpu::Access::transfer_write, gpu::Stage::transfer, gpu::Access::transfer_read);
        gpu::copy_memory(commands, gpu::gpu_range(destination), {.gpu = readback.range.gpu, .size = 32});
        gpu::copy_texture_to_memory(commands, textures[0], {.gpu = readback.range.gpu + 32, .size = 16});
        gpu::copy_texture_to_memory(commands, textures[1], {.gpu = readback.range.gpu + 48, .size = 16});
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write, gpu::Stage::host, gpu::Access::host_read);
        ++completion.value;
        gpu::submit({commands}, completion);
        gpu::wait_timeline(completion);
        uint32 actual[16]{};
        memcpy(actual, readback.range.cpu, sizeof(actual));
        const int mismatch = first_mismatch(actual, case_index);
        valid = mismatch == -1;
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_sample.opt%u.resource%u.sampler%u\",\"status\":\"%s\","
               "\"source_sha256\":\"%s\",\"payload_sha256\":\"%s\",\"output_address\":\"0x%016llx\","
               "\"nonzero_high_address_bits\":%s,\"resource_index\":%u,\"sampler_index\":%u,"
               "\"image_descriptor_bytes\":%llu,\"sampler_descriptor_bytes\":%llu,\"heap_slots\":4,\"root_bytes\":16,"
               "\"first_mismatch_word\":%d,\"shader_cases_executed\":1,\"actual\":[",
            optimization, root.resource, root.sampler, valid ? "pass" : "fail", native_heap_sample_source_sha256, shader_hash,
            static_cast<unsigned long long>(output_address), (output_address >> 32) != 0 ? "true" : "false", root.resource, root.sampler,
            static_cast<unsigned long long>(caps.texture_descriptor_size), static_cast<unsigned long long>(caps.sampler_descriptor_size), mismatch);
        for (uint32 index = 0; index < 16; ++index) printf("%s%u", index ? "," : "", actual[index]);
        printf("]}\n");
    }
    // U-005: completion precedes immediate destruction, including oracle failure.
    gpu::wait_idle(device);
    gpu::destroy_timeline_semaphore(completion.semaphore);
    gpu::destroy_pso(pso);
    gpu::destroy_gpu_heap(readback);
    gpu::destroy_gpu_heap(destination);
    gpu::destroy_gpu_heap(upload);
    gpu::destroy_gpu_heap(samplers);
    gpu::destroy_gpu_heap(resources);
    gpu::destroy_texture(textures[1]);
    gpu::destroy_texture(textures[0]);
    gpu::destroy_texture_heap(storage);
    return valid;
}

int main(int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--self-test") != 0)) return 2;
    if (!self_test()) return 1;
    if (argc == 2) return 0;
    const gpu::DeviceInit init = gpu::create_device({.timestamp_query_count = 0});
    if (init.error != gpu::Error::none || !init.device) {
        printf("{\"case_id\":\"theta.m2.ngapi.native_heap_sample.device\",\"status\":\"blocked\",\"error\":%u}\n",
            static_cast<unsigned>(init.error));
        return init.error == gpu::Error::unsupported ? 77 : 1;
    }
    const bool first = run_variant(init.device, native_heap_sample_opt0, native_heap_sample_opt0_sha256, 0);
    const bool second = first && run_variant(init.device, native_heap_sample_opt3, native_heap_sample_opt3_sha256, 3);
    gpu::wait_idle(init.device);
    gpu::destroy_device(init.device);
    return first && second ? 0 : 1;
}
