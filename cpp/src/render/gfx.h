// Crit Launcher Native — GPU abstraction shared by the D3D11 backend and the preview rasterizer.
#pragma once

#include <memory>

#include "core/base.h"

namespace crit {

// ------------------------------------------------------------------ textures
// A texture owns CPU pixels (RGBA8) and, on Windows, a D3D11 SRV. Keeping the
// CPU copy lets the software preview render the exact same frame.
struct Texture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // w*h*4
    void* gpu = nullptr;        // ID3D11ShaderResourceView* on Windows
    bool keepCpu = false;
    bool dirty = false;         // CPU pixels changed after upload

    uint32_t SampleRgba(float u, float v) const;  // bilinear, clamp
    uint8_t SampleAlpha(float u, float v) const;  // bilinear single channel in rgba[0]
    void Clear() { w = h = 0; rgba.clear(); }
};

enum class BatchKind { Sdf = 0, Text = 1, Image = 2 };

struct Vertex {
    float x, y;          // device pixels
    float u, v;          // texture uv (atlas)
    float r, g, b, a;    // straight color
    float lx, ly;        // local coords for SDF shapes
    float hw, hh;        // half extents for SDF shapes
    float radius;        // corner radius
    float param;         // shadow blur / border width
};

struct Batch {
    BatchKind kind = BatchKind::Sdf;
    int shapeType = 0;   // 0 fill, 1 shadow, 2 border
    int texture = -1;
    Rect clip;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

class TextureRegistry {
public:
    int Add(std::shared_ptr<Texture> tex);
    Texture* Get(int id);
    const std::shared_ptr<Texture>& Ref(int id);
    std::vector<std::shared_ptr<Texture>>& Items() { return items_; }
    void Clear();

private:
    std::vector<std::shared_ptr<Texture>> items_;
};

// Canvas records one frame as textured triangles grouped into batches.
class Canvas {
public:
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    std::vector<Batch> batches;

    TextureRegistry* textures = nullptr;
    Texture* white = nullptr;  // 1x1 white texture used for untextured shapes

    void Reset(const Rect& clip) {
        verts.clear();
        indices.clear();
        batches.clear();
        clip_ = clip;
        tex_ = -1;
        kind_ = BatchKind::Sdf;
        shapeType_ = 0;
    }
    void SetClip(const Rect& r) {
        clip_ = r;
        tex_ = -1;  // force new batch
    }
    const Rect& Clip() const { return clip_; }

    // ---- shapes (SDF, pixel-perfect corners)
    void FillRect(const Rect& r, const Color& c);
    void FillRounded(const Rect& r, float radius, const Color& c);
    void StrokeRounded(const Rect& r, float radius, float width, const Color& c);
    void Shadow(const Rect& r, float radius, float blur, const Color& c, float dy = 0.f);
    void FillCircle(const Vec2& center, float radius, const Color& c);
    void GradientV(const Rect& r, const Color& top, const Color& bottom);
    void GradientH(const Rect& r, const Color& left, const Color& right);
    void GradientQuad(const Rect& r, const Color& tl, const Color& tr, const Color& bl, const Color& br);
    void Triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& col);
    void Line(const Vec2& a, const Vec2& b, float width, const Color& c);

    // ---- textured
    void Image(int textureId, const Rect& dst, const Color& tint = Color(1, 1, 1, 1), float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);
    void ImageSub(int textureId, const Rect& dst, const Rect& uv, const Color& tint = Color(1, 1, 1, 1));

    // ---- helpers
    void Begin(BatchKind kind, int shapeType, int texture);
    void PushQuad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d);
    int AddTexture(std::shared_ptr<Texture> t) { return textures->Add(std::move(t)); }

private:
    Rect clip_;
    int tex_ = -1;
    BatchKind kind_ = BatchKind::Sdf;
    int shapeType_ = 0;
    void FlushIfChanged(BatchKind kind, int shapeType, int texture);
};

// uploads CPU pixels to a GPU texture (no-op off Windows)
void GpuUploadTexture(Texture& tex);
void GpuReleaseTexture(Texture& tex);
void GpuForgetAllTextures();
// the GPU needs this to know a texture changed after an async load
void GpuMarkDirty();

}  // namespace crit
