// Sole stb_image implementation TU: vendor/stb_image.h is v2.30 from nothings/stb@2c980bb5
// (MIT / public domain), unmodified; only the PNG + BMP decoders TWiLight Menu++ box art uses are built.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#pragma GCC diagnostic ignored "-Wunused-function"
#include "vendor/stb_image.h"
