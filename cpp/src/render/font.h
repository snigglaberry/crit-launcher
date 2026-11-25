// Crit Launcher Native — TrueType rendering: glyph atlas + text drawing.
#pragma once

#include "render/gfx.h"

namespace crit {

struct TextStyle {
    const char* family = nullptr;  // nullptr = UI font
    float size = 14.f;
    int weight = 400;
    Color color = Color(1, 1, 1, 1);
    float letterSpacing = 0.f;
    bool uppercase = false;
    bool ellipsis = true;
};

struct TextMetrics {
    float width = 0.f;
    float lineHeight = 0.f;
    int lines = 1;
};

class FontAtlas {
public:
    ~FontAtlas();
    // one time: parse embedded TTFs and build atlases
    bool Init(Canvas& canvas);
    void Shutdown();

    // choose the closest weight and pixel size
    float Ascender(int weight, float px) const;
    float Descender(int weight, float px) const;
    float LineHeight(int weight, float px) const;

    TextMetrics Measure(const string& text, const TextStyle& style, float maxWidth = 0.f);
    // draw wrapped text at x,y with top-left origin. Returns the used height.
    float Draw(Canvas& cv, const string& text, const TextStyle& style, float x, float y, float maxWidth = 0.f,
               float maxHeight = 0.f, int* lineCountOut = nullptr);
    // single-line, baseline independent helper used by buttons
    float DrawCenterLine(Canvas& cv, const string& text, const TextStyle& style, const Rect& box);

    float MeasureLine(const string& text, const TextStyle& style);

public:
    struct Face;
    Face* Pick(int weight, float px);
    struct Impl;
    Impl* ImplPtr() { return impl_; }

private:
    Impl* impl_ = nullptr;
};

FontAtlas& Fonts();
// Parses inline SVG paths (as used by the page icon set) into a textured sprite.
std::shared_ptr<Texture> RenderSvgIcon(const string& svgBody, float size, int strokeWidth, bool fill);

}  // namespace crit
