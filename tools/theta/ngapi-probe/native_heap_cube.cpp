// Cube geometry adapted from NoGraphicsAPI examples/cube/cube.cpp at d60b10bd.
// Copyright (c) 2026 Sebastian Aaltonen. See ../../../legal/licenses/NoGraphicsAPI-MIT.txt.
#include "native_heap_cube.hpp"
#ifdef THETA_CUBE_TASK_MESH
#include "native_heap_mesh_modules.hpp"
constexpr const char* fixture_id = "theta.m2.ngapi.native_heap_mesh";
constexpr const char* image_prefix = "mesh";
constexpr const char* source_hash = native_heap_mesh_source_sha256;
#else
#include "native_heap_cube_modules.hpp"
constexpr const char* fixture_id = "theta.m2.ngapi.native_heap_cube";
constexpr const char* image_prefix = "cube";
constexpr const char* source_hash = native_heap_cube_source_sha256;
#endif
#include <stdio.h>
#include <string.h>

using gpu::uint16;
using gpu::uint32;
using gpu::uint64;

constexpr uint32 width = 128, height = 128, image_bytes = width * height * 4;
constexpr CubeVertex vertices[]{
    {{-1,-1,-1,1},{0,1}}, {{-1,-1, 1,1},{1,1}}, {{-1, 1, 1,1},{1,0}}, {{-1, 1,-1,1},{0,0}},
    {{-1,-1,-1,1},{1,1}}, {{-1, 1,-1,1},{1,0}}, {{ 1, 1,-1,1},{0,0}}, {{ 1,-1,-1,1},{0,1}},
    {{-1,-1,-1,1},{1,0}}, {{ 1,-1,-1,1},{1,1}}, {{ 1,-1, 1,1},{0,1}}, {{-1,-1, 1,1},{0,0}},
    {{-1, 1,-1,1},{1,0}}, {{-1, 1, 1,1},{0,0}}, {{ 1, 1, 1,1},{0,1}}, {{ 1, 1,-1,1},{1,1}},
    {{ 1, 1,-1,1},{1,0}}, {{ 1, 1, 1,1},{0,0}}, {{ 1,-1, 1,1},{0,1}}, {{ 1,-1,-1,1},{1,1}},
    {{-1,-1, 1,1},{0,1}}, {{ 1,-1, 1,1},{1,1}}, {{ 1, 1, 1,1},{1,0}}, {{-1, 1, 1,1},{0,0}},
};
constexpr uint16 indices[]{0,1,2,2,3,0, 4,5,6,6,7,4, 8,9,10,10,11,8, 12,13,14,14,15,12, 16,17,18,18,19,16, 20,21,22,22,23,20};
constexpr float transforms[2][4][4]{
    {{.3125f,0,.1875f,0}, {-.125f,-.375f,.1875f,0}, {-.125f,.125f,.1875f,.5f}, {0,0,0,1}},
    {{.1875f,0,-.3125f,.0625f}, {-.1875f,-.375f,-.125f,0}, {.1875f,.125f,-.125f,.5f}, {0,0,0,1}},
};
constexpr uint32 pixels[]{0xff0000ff,0xff00ff00,0xffff0000,0xffffffff, 0xffffff00,0xffff00ff,0xff00ffff,0xff000000};
constexpr uint32 index_offset = sizeof(vertices);
constexpr uint32 texture_offset = index_offset + sizeof(indices);
constexpr uint32 input_bytes = texture_offset + sizeof(pixels) + 16;
constexpr uint32 input_readback_offset = image_bytes * 2 + 16;
constexpr uint32 readback_bytes = input_readback_offset + input_bytes;

static bool valid_range(uint64 address, uint64 bytes, uint64 required, uint64 alignment) noexcept
{
    return address && !(address & (alignment - 1)) && bytes >= required && address <= ~uint64(0) - required;
}

static bool valid_indices(const uint16* values, uint32 count) noexcept
{
    if (count != 36) return false;
    for (uint32 index = 0; index < count; ++index) if (values[index] >= 24) return false;
    return true;
}

static bool self_test() noexcept
{
    uint16 bad[36];
    memcpy(bad, indices, sizeof(bad));
    bad[7] = 24;
    const bool invalid24 = !valid_indices(bad, 36);
    bad[7] = 65535;
    const bool tests[]{valid_range(0x1000,576,576,4), valid_range(0x100000008ull,576,576,4),
        !valid_range(0,576,576,4), !valid_range(0x1001,576,576,4), !valid_range(0x1000,575,576,4),
        !valid_range(0xffffffffffffff00ull,576,576,4), valid_indices(indices,36), !valid_indices(indices,35),
        invalid24, !valid_indices(bad,36)};
    bool valid = true;
    for (bool result : tests) valid &= result;
    printf("{\"case_id\":\"%s.controls\",\"status\":\"%s\",\"checks\":10,\"shader_cases_executed\":0}\n",
        fixture_id, valid ? "pass" : "fail");
    return valid;
}

static bool write_image(const char* name, const void* bytes, size_t count) noexcept
{
    FILE* file = nullptr;
    if (fopen_s(&file, name, "wb") || !file) return false;
    const bool written = fwrite(bytes, 1, count, file) == count;
    return fclose(file) == 0 && written;
}

static bool run_variant(gpu::Device* device, gpu::Span<const uint32> shader, const char* hash, const char* variant) noexcept
{
    const gpu::DeviceCaps& caps = gpu::get_device_caps(device);
    const gpu::TextureDesc descs[]{
        {.extent = {.x=2,.y=2,.z=1}, .format=gpu::Format::rgba8_unorm,
         .usage=gpu::TextureUsage::sampled | gpu::TextureUsage::transfer_destination | gpu::TextureUsage::transfer_source},
        {.extent = {.x=2,.y=2,.z=1}, .format=gpu::Format::rgba8_unorm,
         .usage=gpu::TextureUsage::sampled | gpu::TextureUsage::transfer_destination | gpu::TextureUsage::transfer_source},
        {.extent = {.x=width,.y=height,.z=1}, .format=gpu::Format::rgba8_unorm,
         .usage=gpu::TextureUsage::color_attachment | gpu::TextureUsage::transfer_source},
        {.extent = {.x=width,.y=height,.z=1}, .format=gpu::Format::d32_float,
         .usage=gpu::TextureUsage::depth_stencil_attachment | gpu::TextureUsage::transfer_source},
    };
    gpu::SizeAlign sizes[4]{};
    bool supported = caps.max_push_data_size >= sizeof(CubeRoot) && caps.texture_descriptor_size && caps.sampler_descriptor_size
        && caps.texture_descriptor_size <= ~uint64(0) / 4 && caps.sampler_descriptor_size <= ~uint64(0) / 4;
    for (uint32 index = 0; index < 4; ++index) {
        if (!gpu::supports_texture_format(device, descs[index].format, descs[index].usage)) { supported = false; break; }
        sizes[index] = gpu::get_texture_size_align(device, descs[index]);
        supported &= sizes[index].size && sizes[index].align && !(sizes[index].align & (sizes[index].align - 1));
    }
    if (!supported) {
        printf("{\"case_id\":\"%s.requirements.%s\",\"status\":\"blocked\"}\n", fixture_id, variant);
        return false;
    }
    gpu::TextureHeap storage[4]{};
    gpu::Texture* textures[4]{};
    for (uint32 index = 0; index < 4; ++index) {
        storage[index] = gpu::create_texture_heap(device, sizes[index].size);
        textures[index] = gpu::create_texture(device, descs[index], storage[index], 0);
    }
    gpu::RenderView* color = gpu::create_render_view(textures[2]);
    gpu::RenderView* depth = gpu::create_render_view(textures[3]);
    const gpu::GpuHeap resources = gpu::create_gpu_heap(device, caps.texture_descriptor_size * 4, gpu::MemoryType::texture_descriptor_heap);
    const gpu::GpuHeap samplers = gpu::create_gpu_heap(device, caps.sampler_descriptor_size * 4, gpu::MemoryType::sampler_descriptor_heap);
    const gpu::GpuHeap upload = gpu::create_gpu_heap(device, input_bytes);
    const gpu::GpuHeap data = gpu::create_gpu_heap(device, input_bytes, gpu::MemoryType::gpu_only);
    const gpu::GpuHeap readback = gpu::create_gpu_heap(device, readback_bytes, gpu::MemoryType::readback);
#ifdef THETA_CUBE_TASK_MESH
    gpu::PSO* pso = gpu::create_mesh_pso(device, {
        .task_spirv=shader, .mesh_spirv=shader, .fragment_spirv=shader,
        .color_targets={{.format=gpu::Format::rgba8_unorm}}, .depth_format=gpu::Format::d32_float,
        .rasterization={.cull=gpu::CullMode::none},
    });
    constexpr gpu::Stage input_stages = gpu::Stage::task | gpu::Stage::mesh | gpu::Stage::fragment;
#else
    gpu::PSO* pso = gpu::create_graphics_pso(device, {
        .vertex_spirv=shader, .fragment_spirv=shader,
        .color_targets={{.format=gpu::Format::rgba8_unorm}}, .depth_format=gpu::Format::d32_float,
        .rasterization={.cull=gpu::CullMode::none},
    });
    constexpr gpu::Stage input_stages = gpu::Stage::vertex | gpu::Stage::index_input | gpu::Stage::fragment;
#endif
    gpu::TimelinePoint completion{.semaphore=gpu::create_timeline_semaphore(device)};
    const uint64 address = reinterpret_cast<uint64>(data.range.gpu);
    bool valid = color && depth && pso && completion.semaphore
        && valid_range(address, data.range.size, input_bytes, 4) && valid_indices(indices, 36)
        && resources.range.cpu && resources.range.gpu && resources.range.size == caps.texture_descriptor_size * 4
        && samplers.range.cpu && samplers.range.gpu && samplers.range.size == caps.sampler_descriptor_size * 4
        && upload.range.cpu && upload.range.gpu && upload.range.size >= input_bytes
        && readback.range.cpu && readback.range.gpu && readback.range.size >= readback_bytes;
    if (valid) {
        for (uint32 slot = 0; slot < 4; ++slot) {
            gpu::write_texture_descriptor(device, resources.range.cpu + slot * caps.texture_descriptor_size,
                textures[slot == 3 ? 1 : 0], gpu::TextureDescriptorType::sampled);
            gpu::write_sampler_descriptor(device, samplers.range.cpu + slot * caps.sampler_descriptor_size, {
                .min_filter=gpu::Filter::nearest, .mag_filter=gpu::Filter::nearest,
                .address_u=slot == 3 ? gpu::AddressMode::clamp_to_edge : gpu::AddressMode::repeat,
                .address_v=slot == 3 ? gpu::AddressMode::clamp_to_edge : gpu::AddressMode::repeat,
                .address_w=gpu::AddressMode::clamp_to_edge,
            });
        }
        memset(upload.range.cpu, 0xa5, input_bytes);
        memcpy(upload.range.cpu, vertices, sizeof(vertices));
        memcpy(upload.range.cpu + index_offset, indices, sizeof(indices));
        memcpy(upload.range.cpu + texture_offset, pixels, sizeof(pixels));
    }
    for (uint32 case_index = 0; valid && case_index < 4; ++case_index) {
        CubeRoot root{.vertices=address, .resource=case_index < 2 ? 1u : 3u, .sampler=2 + case_index % 2};
        memcpy(root.transform, transforms[case_index / 2], sizeof(root.transform));
        memset(readback.range.cpu, 0xcd, readback_bytes);
        // U-008/U-014: complete upload ranges and pending GENERAL transitions are
        // recorded before use. Every begun buffer is submitted and awaited once.
        gpu::CommandBuffer* commands = gpu::begin_commands(device);
        gpu::copy_memory(commands, gpu::gpu_range(upload), gpu::gpu_range(data));
        gpu::copy_memory_to_texture(commands, {.gpu=upload.range.gpu + texture_offset,.size=16}, textures[0]);
        gpu::copy_memory_to_texture(commands, {.gpu=upload.range.gpu + texture_offset + 16,.size=16}, textures[1]);
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write,
            input_stages, gpu::Access::shader_read
#ifndef THETA_CUBE_TASK_MESH
            | gpu::Access::index_read
#endif
        );
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_read,
            gpu::Stage::color_output | gpu::Stage::depth_stencil_tests,
            gpu::Access::color_write | gpu::Access::depth_stencil_read | gpu::Access::depth_stencil_write);
        gpu::set_texture_descriptor_heap(commands, gpu::gpu_range(resources));
        gpu::set_sampler_descriptor_heap(commands, gpu::gpu_range(samplers));
        gpu::begin_render_pass(commands, {
            .colors={{.render_view=color,.load=gpu::LoadOp::clear,.clear={17.0f/255.0f,34.0f/255.0f,51.0f/255.0f,1}}},
            .depth={.render_view=depth,.load=gpu::LoadOp::clear,.store=gpu::StoreOp::store,.clear=1},
        });
        gpu::set_depth_stencil(commands, {.depth_test=true,.depth_write=true,.depth_compare=gpu::CompareOp::less});
        gpu::bind_pso(commands, pso);
#ifdef THETA_CUBE_TASK_MESH
        // One task invocation emits six mesh groups, each with four vertices/two triangles.
        // Its 80-byte payload and output are below VK_EXT_mesh_shader minima.
        gpu::draw_meshlets(commands, root, {.x=1,.y=1,.z=1});
#else
        gpu::draw_indexed(commands, root, {.gpu=data.range.gpu + index_offset,.size=sizeof(indices)}, gpu::IndexType::uint16, 36);
#endif
        gpu::end_render_pass(commands);
        gpu::barrier(commands, gpu::Stage::color_output | gpu::Stage::depth_stencil_tests | gpu::Stage::transfer,
            gpu::Access::color_write | gpu::Access::depth_stencil_write | gpu::Access::transfer_write,
            gpu::Stage::transfer, gpu::Access::transfer_read);
        gpu::copy_texture_to_memory(commands, textures[2], {.gpu=readback.range.gpu,.size=image_bytes});
        gpu::copy_texture_to_memory(commands, textures[3], {.gpu=readback.range.gpu + image_bytes,.size=image_bytes});
        gpu::copy_memory(commands, {.gpu=data.range.gpu,.size=texture_offset},
            {.gpu=readback.range.gpu + input_readback_offset,.size=texture_offset});
        constexpr uint32 input_guard_offset = texture_offset + sizeof(pixels);
        gpu::copy_memory(commands, {.gpu=data.range.gpu + input_guard_offset,.size=16},
            {.gpu=readback.range.gpu + input_readback_offset + input_guard_offset,.size=16});
        // These disjoint ranges reconstruct all source bytes, taking texture
        // contents from the actual images rather than their upload copy.
        gpu::copy_texture_to_memory(commands, textures[0], {.gpu=readback.range.gpu + input_readback_offset + texture_offset,.size=16});
        gpu::copy_texture_to_memory(commands, textures[1], {.gpu=readback.range.gpu + input_readback_offset + texture_offset + 16,.size=16});
        gpu::barrier(commands, gpu::Stage::transfer, gpu::Access::transfer_write, gpu::Stage::host, gpu::Access::host_read);
        ++completion.value;
        gpu::submit({commands}, completion);
        gpu::wait_timeline(completion);
        int mismatch = -1;
        for (uint32 index = 0; index < input_bytes; ++index) {
            if (readback.range.cpu[input_readback_offset + index] != upload.range.cpu[index]) { mismatch = static_cast<int>(index); break; }
        }
        bool guard = true;
        for (uint32 index = image_bytes * 2; index < input_readback_offset; ++index) guard &= readback.range.cpu[index] == 0xcd;
        char color_file[80], depth_file[80];
        snprintf(color_file, sizeof(color_file), "%s.%s.case%u.rgba", image_prefix, variant, case_index);
        snprintf(depth_file, sizeof(depth_file), "%s.%s.case%u.depth", image_prefix, variant, case_index);
        valid = mismatch == -1 && guard && write_image(color_file, readback.range.cpu, image_bytes)
            && write_image(depth_file, readback.range.cpu + image_bytes, image_bytes);
        printf("{\"case_id\":\"%s.%s.case%u\",\"status\":\"%s\","
            "\"source_sha256\":\"%s\",\"payload_sha256\":\"%s\",\"view\":%u,\"resource_index\":%u,\"sampler_index\":%u,"
            "\"vertex_address\":\"0x%016llx\",\"nonzero_high_address_bits\":%s,\"vertex_count\":24,\"vertex_stride\":24,\"index_count\":36,"
            "\"root_bytes\":80,\"width\":128,\"height\":128,\"image_descriptor_bytes\":%llu,\"sampler_descriptor_bytes\":%llu,"
            "\"heap_slots\":4,\"first_input_mismatch_byte\":%d,\"guard_intact\":%s,\"shader_cases_executed\":1,"
            "\"completion_value\":%llu,\"color_file\":\"%s\",\"depth_file\":\"%s\""
#ifdef THETA_CUBE_TASK_MESH
            ",\"task_group_count\":[1,1,1],\"mesh_group_count\":[6,1,1],\"mesh_output_vertices\":4,\"mesh_output_primitives\":2"
#endif
            "}\n",
            fixture_id,variant,case_index,valid ? "pass" : "fail",source_hash,hash,case_index/2,root.resource,root.sampler,
            static_cast<unsigned long long>(address),(address>>32) ? "true" : "false",
            static_cast<unsigned long long>(caps.texture_descriptor_size),static_cast<unsigned long long>(caps.sampler_descriptor_size),
            mismatch,guard ? "true" : "false",static_cast<unsigned long long>(completion.value),color_file,depth_file);
    }
    // U-008/U-014: all submissions completed. Drain before immediate teardown,
    // including failures of the byte checks or output writes.
    gpu::wait_idle(device);
    gpu::destroy_timeline_semaphore(completion.semaphore);
    gpu::destroy_pso(pso);
    gpu::destroy_gpu_heap(readback);
    gpu::destroy_gpu_heap(data);
    gpu::destroy_gpu_heap(upload);
    gpu::destroy_gpu_heap(samplers);
    gpu::destroy_gpu_heap(resources);
    gpu::destroy_render_view(depth);
    gpu::destroy_render_view(color);
    for (uint32 index = 0; index < 4; ++index) {
        gpu::destroy_texture(textures[index]);
        gpu::destroy_texture_heap(storage[index]);
    }
    return valid;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef THETA_CUBE_TASK_MESH
    if (argc == 2 && !strcmp(argv[1], "--identity")) {
        printf("{\"profile\":\"ngapi-native-heap-mesh\",\"source_sha256\":\"%s\",\"host_sha256\":\"%s\",\"abi_sha256\":\"%s\","
            "\"payload_sha256\":[\"%s\",\"%s\",\"%s\",\"%s\"]}\n", source_hash,native_heap_mesh_host_sha256,native_heap_mesh_abi_sha256,
            native_heap_mesh_default_opt0_sha256,native_heap_mesh_default_opt3_sha256,
            native_heap_mesh_qptr_opt0_sha256,native_heap_mesh_qptr_opt3_sha256);
        return 0;
    }
#endif
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--self-test"))) return 2;
    if (!self_test()) return 1;
    if (argc == 2) return 0;
    const gpu::DeviceInit init = gpu::create_device({.timestamp_query_count=0});
    if (init.error != gpu::Error::none || !init.device) {
        printf("{\"case_id\":\"%s.device\",\"status\":\"blocked\",\"error\":%u}\n",
            fixture_id,static_cast<unsigned>(init.error));
        return init.error == gpu::Error::unsupported ? 77 : 1;
    }
    struct Variant { gpu::Span<const uint32> shader; const char* hash; const char* name; };
#ifdef THETA_CUBE_TASK_MESH
    const Variant variants[]{
        {native_heap_mesh_default_opt0,native_heap_mesh_default_opt0_sha256,"default_opt0"},
        {native_heap_mesh_default_opt3,native_heap_mesh_default_opt3_sha256,"default_opt3"},
        {native_heap_mesh_qptr_opt0,native_heap_mesh_qptr_opt0_sha256,"qptr_opt0"},
        {native_heap_mesh_qptr_opt3,native_heap_mesh_qptr_opt3_sha256,"qptr_opt3"},
    };
#else
    const Variant variants[]{
        {native_heap_cube_opt0,native_heap_cube_opt0_sha256,"opt0"},
        {native_heap_cube_opt3,native_heap_cube_opt3_sha256,"opt3"},
    };
#endif
    bool valid = true;
    for (const Variant& variant : variants) {
        if (!run_variant(init.device, variant.shader, variant.hash, variant.name)) { valid = false; break; }
    }
    gpu::wait_idle(init.device);
    gpu::destroy_device(init.device);
    return valid ? 0 : 1;
}
