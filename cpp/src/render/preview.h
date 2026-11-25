// Crit Launcher Native — software rasterizer used by the headless preview tool.
// It renders the exact same Canvas display list the D3D11 backend consumes,
// so screens can be checked on machines without a GPU.
#pragma once

#include "render/gfx.h"

namespace crit {

struct RasterTarget {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // straight alpha
};

void Rasterize(Canvas& canvas, RasterTarget& target);
bool PngWrite(const string& path, const RasterTarget& target);
bool PngRead(const string& path, RasterTarget& target);  // 8-bit RGB/RGBA, no interlace

}  // namespace crit
