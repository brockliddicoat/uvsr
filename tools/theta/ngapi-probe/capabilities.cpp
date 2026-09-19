// T004 diagnostic consumer of unchanged NGAPI, not the Rust AGFX port.
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <cstdio>
#include <iomanip>
#include <iostream>

int main()
{
    const auto init = gpu::create_device();
    if (init.error != gpu::Error::none || !init.device) {
        std::printf("{\"case_id\":\"theta.m0.ngapi.create_device\",\"status\":\"blocked\",\"error\":%u}\n",
                    static_cast<unsigned>(init.error));
        return init.error == gpu::Error::unsupported ? 77 : 1;
    }
    const auto& caps = gpu::get_device_caps(init.device);
    const bool valid = caps.max_push_data_size >= 256 &&
                       caps.texture_descriptor_size > 0 && caps.sampler_descriptor_size > 0;
    std::cout << "{\"case_id\":\"theta.m0.ngapi.create_device\",\"status\":\""
              << (valid ? "pass" : "fail") << "\",\"adapter\":"
              << std::quoted(caps.device_name ? caps.device_name : "")
              << ",\"max_push_data_size\":" << caps.max_push_data_size
              << ",\"texture_descriptor_size\":" << caps.texture_descriptor_size
              << ",\"sampler_descriptor_size\":" << caps.sampler_descriptor_size
              << ",\"shader_cases_executed\":0}\n";
    gpu::wait_idle(init.device);
    gpu::destroy_device(init.device);
    return valid ? 0 : 1;
}
