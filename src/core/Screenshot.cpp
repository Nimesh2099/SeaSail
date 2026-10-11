#include "Screenshot.h"

#include <glad/gl.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace sea {

// Uncompressed 24-bit BMP. glReadPixels returns rows bottom-up, which is
// exactly BMP's native row order, so no flipping is needed.
bool saveBmp(const std::string& path, int w, int h)
{
    const int rowBytes = (w * 3 + 3) & ~3; // rows padded to 4 bytes
    std::vector<unsigned char> pixels(static_cast<size_t>(rowBytes) * h);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t dataSize = static_cast<uint32_t>(pixels.size());
    const uint32_t fileSize = 54 + dataSize;
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[off + i] = (v >> (8 * i)) & 0xFF; };
    put32(2, fileSize);
    put32(10, 54);          // pixel data offset
    put32(14, 40);          // BITMAPINFOHEADER size
    put32(18, static_cast<uint32_t>(w));
    put32(22, static_cast<uint32_t>(h));
    hdr[26] = 1;            // planes
    hdr[28] = 24;           // bits per pixel
    put32(34, dataSize);
    std::fwrite(hdr, 1, 54, f);
    std::fwrite(pixels.data(), 1, pixels.size(), f);
    std::fclose(f);
    return true;
}

} // namespace sea
