// Screenshot.h - save the current framebuffer as an uncompressed BMP.
#pragma once

#include <string>

namespace sea {

// Reads the bound framebuffer (w x h) and writes a 24-bit .bmp file.
bool saveBmp(const std::string& path, int w, int h);

} // namespace sea
