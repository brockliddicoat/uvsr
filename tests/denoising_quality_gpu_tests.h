#pragma once

float QualityNoise(uint32_t pixel, uint32_t frame)
{
    uint32_t value = pixel ^ (frame * 0x9e3779b9u);
    value ^= value >> 16u; value *= 0x7feb352du;
    value ^= value >> 15u; value *= 0x846ca68bu;
    value ^= value >> 16u;
    return (float(value >> 8u) / 16777216.f - 0.5f) * 1.6f;
}

float QualityMaximumError(const std::vector<float>& left, const std::vector<float>& right)
{
    Require(left.size() == right.size(), "quality image extent");
    float error = 0.f;
    for (size_t i = 0; i < left.size(); ++i)
    {
        Require(std::isfinite(left[i]) && std::isfinite(right[i]), "nonfinite quality image");
        error = std::max(error, std::abs(left[i] - right[i]));
    }
    return error;
}

void SpatialPlaneQuality(Gpu& gpu)
{
    constexpr uint32_t size = 65;
    Fixture f(gpu, size, size);
    auto geometry = gpu.Texture(size, size, nvrhi::Format::RGBA32_FLOAT);
    f.inputs.geometry = geometry;
    std::vector<float> raw(size * size * 4u), depths(size * size), normals(raw.size()), surfaces(raw.size());
    for (uint32_t p = 0; p < size * size; ++p)
    {
        raw[p * 4] = raw[p * 4 + 1] = raw[p * 4 + 2] = 1.f + QualityNoise(p, 1);
        raw[p * 4 + 3] = 1.f;
    }
    DenoisingPass pass(gpu.device, gpu.shaders);
    for (const auto method : { DenoisingMethodChoice::Bilateral, DenoisingMethodChoice::Svgf })
    for (const float depthSigma : { 0.01f, 1.f })
    {
        DenoisingSignalSettings settings;
        settings.method = method; settings.depthPhi = depthSigma; settings.temporal = false;
        std::vector<float> reference;
        for (uint32_t orientation = 0; orientation < 4; ++orientation)
        {
            const bool jittered = orientation == 3;
            const float nx = orientation == 0 ? 0.f : orientation == 2 ? 0.6f : -0.3f;
            const float ny = orientation == 0 ? 0.f : orientation == 2 ? 0.3f : 0.6f;
            const float nz = -1.f;
            const float inverseLength = 1.f / std::sqrt(nx * nx + ny * ny + nz * nz);
            const float octScale = 1.f / (std::abs(nx) + std::abs(ny) + std::abs(nz));
            const float octX = (1.f - std::abs(ny * octScale)) * (nx >= 0.f ? 1.f : -1.f) * 0.5f + 0.5f;
            const float octY = (1.f - std::abs(nx * octScale)) * (ny >= 0.f ? 1.f : -1.f) * 0.5f + 0.5f;
            for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x)
            {
                const uint32_t p = y * size + x;
                const float jitterX = jittered ? (QualityNoise(p, 19) / 1.6f + 0.5f) : 0.5f;
                const float jitterY = jittered ? (QualityNoise(p, 37) / 1.6f + 0.5f) : 0.5f;
                const float rayX = (2.f * (float(x) + jitterX) / float(size) - 1.f) * std::tan(dm::radians(30.f));
                const float rayY = (1.f - 2.f * (float(y) + jitterY) / float(size)) * std::tan(dm::radians(30.f));
                // intersect a known plane passing through (0, 0, 3). the
                // reverse-depth projection has near distance one and z = 1/t.
                const float distance = nz * 3.f / (nx * rayX + ny * rayY + nz);
                depths[p] = 1.f / distance;
                normals[p * 4] = nx * inverseLength; normals[p * 4 + 1] = ny * inverseLength;
                normals[p * 4 + 2] = nz * inverseLength; normals[p * 4 + 3] = 1.f;
                surfaces[p * 4] = octX; surfaces[p * 4 + 1] = octY;
                surfaces[p * 4 + 2] = jitterX; surfaces[p * 4 + 3] = jitterY;
            }
            pass.ReleaseResources(); f.inputs.frameIndex = 1;
            auto command = gpu.device->createCommandList(); command->open();
            command->writeTexture(f.raw, 0, 0, raw.data(), size * 16);
            command->writeTexture(f.depth, 0, 0, depths.data(), size * 4);
            command->writeTexture(f.normal, 0, 0, normals.data(), size * 16);
            command->writeTexture(geometry, 0, 0, surfaces.data(), size * 16);
            const auto result = jittered ? pass.ProcessPathTracing(command, settings, f.inputs) : pass.ProcessDiffuseGi(command, settings, f.inputs);
            Require(result.denoised, "plane filter dispatch"); gpu.Submit(command);
            const auto output = gpu.Floats(result.texture);
            if (orientation == 0) reference = output;
            else
            {
                const float error = QualityMaximumError(reference, output);
                std::cout << "plane orientation " << orientation << ", depth sigma " << depthSigma << ", max difference " << error << '\n';
                Require(error < 0.005f, "spatial filter depends on plane direction or primary jitter");
            }
        }
    }
}

void MaterialQuality(Gpu& gpu)
{
    Fixture f(gpu, 65, 65);
    auto modulation = gpu.Texture(f.width, f.height, nvrhi::Format::RGBA32_FLOAT);
    auto emission = gpu.Texture(f.width, f.height, nvrhi::Format::RGBA32_FLOAT);
    f.inputs.modulation = modulation; f.inputs.emission = emission;
    const uint32_t pixels = f.width * f.height;
    std::vector<float> reflectance(pixels * 4), emissive(pixels * 4), target(pixels * 4), raw(pixels * 4);
    const float light[] = { 2.f, 1.f, 0.5f };
    for (uint32_t y = 0; y < f.height; ++y) for (uint32_t x = 0; x < f.width; ++x)
    {
        const uint32_t p = y * f.width + x;
        const bool blue = ((x / 4 + y / 4) & 1u) != 0u;
        reflectance[p * 4] = blue ? 0.04f : 0.9f;
        reflectance[p * 4 + 1] = blue ? 0.2f : 0.05f;
        reflectance[p * 4 + 2] = blue ? 0.8f : 0.1f;
        for (uint32_t c = 0; c < 3; ++c)
        {
            emissive[p * 4 + c] = x == f.width / 2 && y == f.height / 2 ? 8.f : 0.f;
            target[p * 4 + c] = reflectance[p * 4 + c] * light[c] + emissive[p * 4 + c];
        }
        reflectance[p * 4 + 3] = emissive[p * 4 + 3] = target[p * 4 + 3] = 1.f;
    }
    auto upload = gpu.device->createCommandList(); upload->open();
    upload->writeTexture(modulation, 0, 0, reflectance.data(), f.width * 16);
    upload->writeTexture(emission, 0, 0, emissive.data(), f.width * 16); gpu.Submit(upload);
    for (const auto method : { DenoisingMethodChoice::Bilateral, DenoisingMethodChoice::Svgf })
    for (bool noisy : { false, true })
    {
        DenoisingPass pass(gpu.device, gpu.shaders);
        DenoisingSignalSettings settings; settings.method = method;
        const uint32_t frames = noisy && method == DenoisingMethodChoice::Svgf ? 32u : 1u;
        nvrhi::ITexture* filtered = nullptr;
        double inputError = 0.;
        for (uint32_t frame = 1; frame <= frames; ++frame)
        {
            inputError = 0.;
            for (uint32_t p = 0; p < pixels; ++p)
            {
                const float noise = noisy ? QualityNoise(p, frame) : 0.f;
                for (uint32_t c = 0; c < 3; ++c)
                {
                    raw[p * 4 + c] = reflectance[p * 4 + c] * light[c] * (1.f + noise) + emissive[p * 4 + c];
                    inputError += double(noise) * noise;
                }
                raw[p * 4 + 3] = 1.f;
            }
            f.inputs.frameIndex = frame;
            auto command = gpu.device->createCommandList(); command->open();
            command->writeTexture(f.raw, 0, 0, raw.data(), f.width * 16);
            const auto result = pass.ProcessPathTracing(command, settings, f.inputs);
            Require(result.denoised, "material filter dispatch"); gpu.Submit(command); filtered = result.texture;
        }
        const auto output = gpu.Floats(filtered);
        if (!noisy)
            Require(QualityMaximumError(output, target) < 3e-5f, "material texture or primary emission was blurred");
        else
        {
            double error = 0., bias = 0.;
            for (uint32_t p = 0; p < pixels; ++p) for (uint32_t c = 0; c < 3; ++c)
            {
                const double residual = (output[p * 4 + c] - target[p * 4 + c]) / (reflectance[p * 4 + c] * light[c]);
                Require(std::isfinite(residual), "nonfinite material reconstruction");
                error += residual * residual; bias += residual;
            }
            const double ratio = std::sqrt(error / inputError);
            bias /= double(pixels) * 3.;
            std::cout << "material method " << int(method) << ", frames " << frames << ", RMSE/raw " << ratio << ", relative mean bias " << bias << '\n';
            Require(ratio < (method == DenoisingMethodChoice::Svgf ? 0.1 : 0.35), "denoising did not reduce lighting noise on textured surfaces");
            Require(std::abs(bias) < 0.02, "denoising changed mean illumination");
        }
    }
}

void MovingEdgeQuality(Gpu& gpu)
{
    Fixture f(gpu, 65, 65);
    std::vector<float> raw(f.width * f.height * 4u), depths(f.width * f.height), motion(raw.size());
    DenoisingPass pass(gpu.device, gpu.shaders);
    DenoisingSignalSettings settings; settings.method = DenoisingMethodChoice::Svgf;
    for (uint32_t frame = 1; frame <= 17; ++frame)
    {
        const uint32_t shift = frame == 17 ? 2u : 0u;
        for (uint32_t y = 0; y < f.height; ++y) for (uint32_t x = 0; x < f.width; ++x)
        {
            const uint32_t p = y * f.width + x;
            const bool foreground = x >= 20u + shift && x < 35u + shift;
            raw[p * 4] = raw[p * 4 + 1] = raw[p * 4 + 2] = foreground ? 4.f : 1.f;
            raw[p * 4 + 3] = 1.f;
            depths[p] = foreground ? 0.5f : 0.25f;
            motion[p * 4] = foreground ? -float(shift) : 0.f;
            motion[p * 4 + 1] = motion[p * 4 + 2] = 0.f; motion[p * 4 + 3] = 1.f;
        }
        f.inputs.frameIndex = frame;
        auto command = gpu.device->createCommandList(); command->open();
        command->writeTexture(f.raw, 0, 0, raw.data(), f.width * 16);
        command->writeTexture(f.depth, 0, 0, depths.data(), f.width * 4);
        command->writeTexture(f.motion, 0, 0, motion.data(), f.width * 16);
        const auto result = pass.ProcessDiffuseGi(command, settings, f.inputs);
        Require(result.denoised, "moving edge dispatch"); gpu.Submit(command);
        const float error = QualityMaximumError(gpu.Floats(result.texture), raw);
        Require(error < 0.001f, "moving depth edge leaked bright history or spatial lighting");
    }
    std::cout << "moving foreground and first-frame disocclusion preserved the lighting boundary\n";
}

void ProducerGridQuality(Gpu& gpu)
{
    Fixture f(gpu, 65, 67);
    const uint32_t width = 17, height = 17;
    f.raw = gpu.Texture(width, height, nvrhi::Format::RGBA32_FLOAT);
    f.inputs.rawSignal = f.raw; f.inputs.sourceSize = { width, height };
    std::vector<float> raw(width * height * 4u);
    for (uint32_t p = 0; p < width * height; ++p)
    {
        raw[p * 4] = raw[p * 4 + 1] = raw[p * 4 + 2] = 1.f + QualityNoise(p, 1);
        raw[p * 4 + 3] = 1.f;
    }
    auto upload = gpu.device->createCommandList(); upload->open();
    upload->writeTexture(f.raw, 0, 0, raw.data(), width * 16); gpu.Submit(upload);
    for (auto method : { DenoisingMethodChoice::Bilateral, DenoisingMethodChoice::Svgf })
    {
        DenoisingPass full(gpu.device, gpu.shaders), quarter(gpu.device, gpu.shaders);
        DenoisingSignalSettings settings; settings.method = method;
        f.inputs.frameIndex = 1;
        auto command = gpu.device->createCommandList(); command->open();
        const auto a = full.ProcessDiffuseGi(command, settings, f.inputs);
        settings.resolution = DenoisingResolution::Quarter;
        const auto b = quarter.ProcessDiffuseGi(command, settings, f.inputs);
        Require(a.denoised && b.denoised, "producer grid dispatch"); gpu.Submit(command);
        Require(QualityMaximumError(gpu.Floats(a.texture), gpu.Floats(b.texture)) < 1e-6f,
            "full denoising counted repeated reduced-resolution samples");
    }
    std::cout << "quarter-resolution producer retains one observation per sample on odd extents\n";
}

void InvalidSampleQuality(Gpu& gpu)
{
    Fixture f(gpu, 17, 19); DenoisingPass pass(gpu.device, gpu.shaders);
    DenoisingSignalSettings settings; settings.method = DenoisingMethodChoice::Svgf;
    for (uint32_t frame = 1; frame <= 9; ++frame)
    {
        f.inputs.frameIndex = frame;
        auto command = gpu.device->createCommandList(); command->open();
        command->clearTextureFloat(f.raw, nvrhi::AllSubresources, nvrhi::Color(frame == 9 ? 0.f : 1.f));
        const auto result = pass.ProcessPathTracing(command, settings, f.inputs);
        Require(result.denoised, "invalid sample dispatch"); gpu.Submit(command);
        for (float value : gpu.Floats(result.texture)) Require(std::abs(value - 1.f) < 2e-6f, "rejected path was accumulated as black");
    }
    std::cout << "rejected path samples retain valid lighting history\n";
}

void ReducedFilterQuality(Gpu& gpu)
{
    for (bool thinSurface : { false, true })
    {
        Fixture f(gpu, 65, 65);
        std::vector<float> raw(f.width * f.height * 4u), depth(f.width * f.height);
        for (uint32_t y = 0; y < f.height; ++y) for (uint32_t x = 0; x < f.width; ++x)
        {
            const uint32_t p = y * f.width + x;
            float color = thinSurface ? (x == 4u ? 1.f : 4.f) : (x % 4u == 0u && y % 4u == 0u ? 16.f : 0.f);
            if (!thinSurface && (x == 64u || y == 64u)) color = 1.f;
            raw[p * 4] = raw[p * 4 + 1] = raw[p * 4 + 2] = color;
            raw[p * 4 + 3] = 1.f;
            depth[p] = thinSurface && x == 4u ? 0.5f : 0.25f;
        }
        auto upload = gpu.device->createCommandList(); upload->open();
        upload->writeTexture(f.raw, 0, 0, raw.data(), f.width * 16);
        upload->writeTexture(f.depth, 0, 0, depth.data(), f.width * 4); gpu.Submit(upload);
        for (const auto method : { DenoisingMethodChoice::Bilateral, DenoisingMethodChoice::Svgf })
        {
            DenoisingPass pass(gpu.device, gpu.shaders);
            DenoisingSignalSettings settings; settings.method = method;
            settings.resolution = DenoisingResolution::Quarter;
            settings.temporal = settings.variance = settings.atrous = false;
            auto command = gpu.device->createCommandList(); command->open();
            const auto result = pass.ProcessDiffuseGi(command, settings, f.inputs);
            Require(result.denoised, "reduced filter dispatch"); gpu.Submit(command);
            const auto output = gpu.Floats(result.texture);
            if (thinSurface)
                Require(QualityMaximumError(output, raw) < 0.001f, "coarse resolve borrowed lighting across an unsampled thin surface");
            else
                for (float value : output) Require(std::abs(value - 1.f) < 1e-5f, "coarse filtering discarded source observations");
        }
    }
    std::cout << "reduced filtering averages source observations and preserves unsampled thin surfaces\n";
}

void DenoisingQuality(Gpu& gpu)
{
    SpatialPlaneQuality(gpu);
    MaterialQuality(gpu);
    MovingEdgeQuality(gpu);
    ProducerGridQuality(gpu);
    InvalidSampleQuality(gpu);
    ReducedFilterQuality(gpu);
}
