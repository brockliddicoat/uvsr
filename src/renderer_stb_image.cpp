// The retained environment loader owns stb_image's sole implementation.
// Keep this translation unit independent from scene/texture-cache lifetime.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Transitional Donut TextureCache callers still export retained screenshots.
// Keep the one implementation here until that loader/cache slice is removed.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
