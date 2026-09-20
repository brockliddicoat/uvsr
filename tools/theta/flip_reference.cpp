// CPU-only oracle using AGFX's pinned, unchanged NVIDIA FLIP dependency.
// Mirrors test_compare.cpp::RunFlip byte normalization and default parameters.
// No graphics API, driver, shader or Rust native boundary is involved.
#include <FLIP.h>
#include <charconv>
#include <iomanip>
#include <memory>
#include <stdexcept>

static int dimension(const char* text) {
    int value = 0;
    const char* end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value < 1 || value > 4096)
        throw std::runtime_error("invalid image dimension");
    return value;
}

static std::vector<float> read_rgb(const char* path, size_t pixels) {
    std::ifstream stream(path, std::ios::binary);
    std::vector<unsigned char> bytes(pixels * 4);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))
        || stream.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("RGBA8 input length mismatch");
    std::vector<float> rgb(pixels * 3);
    for (size_t i = 0; i < pixels; ++i)
        for (size_t c = 0; c < 3; ++c) rgb[i * 3 + c] = bytes[i * 4 + c] / 255.0f;
    return rgb;
}

int main(int argc, char** argv) {
    try {
        if (argc != 6) throw std::runtime_error("usage: flip_reference WIDTH HEIGHT REFERENCE.rgba8 ACTUAL.rgba8 ERROR.f32");
        const int width = dimension(argv[1]), height = dimension(argv[2]);
        const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        auto reference = read_rgb(argv[3], pixels), actual = read_rgb(argv[4], pixels);
        FLIP::Parameters parameters;
        float mean = 0.0f, maximum = 0.0f;
        float* raw = nullptr;
        FLIP::evaluate(reference.data(), actual.data(), width, height, false, parameters,
                       false, true, mean, &raw);
        std::unique_ptr<float[]> error(raw);
        if (!error || !std::isfinite(mean)) throw std::runtime_error("invalid FLIP result");
        for (size_t i = 0; i < pixels; ++i) {
            if (!std::isfinite(error[i]) || error[i] < 0.0f || error[i] > 1.0f)
                throw std::runtime_error("invalid FLIP error map");
            maximum = std::max(maximum, error[i]);
        }
        std::ofstream output(argv[5], std::ios::binary);
        output.write(reinterpret_cast<const char*>(error.get()), static_cast<std::streamsize>(pixels * sizeof(float)));
        if (!output) throw std::runtime_error("could not save FLIP error map");
        std::cout << std::setprecision(9) << "{\"mean\":" << mean << ",\"maximum\":" << maximum
                  << ",\"ppd\":" << parameters.PPD << ",\"pixels\":" << pixels << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
