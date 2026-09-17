// one codec implementation serves the renderer and its comparison fixtures.
// scene and texture owners hold decoded storage, not codec state.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
