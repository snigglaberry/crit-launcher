// Crit Launcher Native — display-list builder.
#include "render/gfx.h"

namespace crit {

uint32_t Texture::SampleRgba(float u, float v) const {
    if (w <= 0 || h <= 0 || rgba.empty()) return 0;
    float fx = std::clamp(u, 0.f, 1.f) * (w - 1);
    float fy = std::clamp(v, 0.f, 1.f) * (h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float tx = fx - x0, ty = fy - y0;
    auto px = [&](int x, int y) -> uint32_t {
        const uint8_t* p = &rgba[((size_t)y * w + x) * 4];
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    };
    uint32_t c00 = px(x0, y0), c10 = px(x1, y0), c01 = px(x0, y1), c11 = px(x1, y1);
    auto mixc = [](uint32_t a, uint32_t b, float t) {
        uint32_t out = 0;
        for (int s = 0; s < 32; s += 8) {
            float va = (float)((a >> s) & 0xff), vb = (float)((b >> s) & 0xff);
            out |= (uint32_t)std::lround(va + (vb - va) * t) << s;
        }
        return out;
    };
    return mixc(mixc(c00, c10, tx), mixc(c01, c11, tx), ty);
}

uint8_t Texture::SampleAlpha(float u, float v) const {
    if (w <= 0 || h <= 0 || rgba.empty()) return 0;
    float fx = std::clamp(u, 0.f, 1.f) * (w - 1);
    float fy = std::clamp(v, 0.f, 1.f) * (h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float tx = fx - x0, ty = fy - y0;
    auto px = [&](int x, int y) { return (float)rgba[((size_t)y * w + x) * 4]; };
    float top = px(x0, y0) + (px(x1, y0) - px(x0, y0)) * tx;
    float bot = px(x0, y1) + (px(x1, y1) - px(x0, y1)) * tx;
    return (uint8_t)std::lround(std::clamp(top + (bot - top) * ty, 0.f, 255.f));
}

// ---------------------------------------------------------------- textures
int TextureRegistry::Add(std::shared_ptr<Texture> tex) {
    items_.push_back(std::move(tex));
    return (int)items_.size() - 1;
}
Texture* TextureRegistry::Get(int id) {
    if (id < 0 || id >= (int)items_.size()) return nullptr;
    return items_[(size_t)id].get();
}
const std::shared_ptr<Texture>& TextureRegistry::Ref(int id) {
    static std::shared_ptr<Texture> null_;
    if (id < 0 || id >= (int)items_.size()) return null_;
    return items_[(size_t)id];
}
void TextureRegistry::Clear() { items_.clear(); }

// ---------------------------------------------------------------- canvas
void Canvas::FlushIfChanged(BatchKind kind, int shapeType, int texture) {
    if (batches.empty() || kind != kind_ || shapeType != shapeType_ || texture != tex_) {
        Batch b;
        b.kind = kind;
        b.shapeType = shapeType;
        b.texture = texture;
        b.clip = clip_;
        b.firstIndex = (uint32_t)indices.size();
        b.indexCount = 0;
        batches.push_back(b);
        kind_ = kind;
        shapeType_ = shapeType;
        tex_ = texture;
    }
}

void Canvas::Begin(BatchKind kind, int shapeType, int texture) {
    FlushIfChanged(kind, shapeType, texture);
}

void Canvas::PushQuad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
    uint32_t base = (uint32_t)verts.size();
    verts.push_back(a);
    verts.push_back(b);
    verts.push_back(c);
    verts.push_back(d);
    const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    for (uint32_t i : idx) indices.push_back(base + i);
    batches.back().indexCount += 6;
}

static Vertex ShapeVert(float px, float py, float lx, float ly, float hw, float hh, float radius, float param, const Color& c) {
    Vertex v;
    v.x = px; v.y = py;
    v.u = 0.5f; v.v = 0.5f;
    v.r = c.r; v.g = c.g; v.b = c.b; v.a = c.a;
    v.lx = lx; v.ly = ly;
    v.hw = hw; v.hh = hh;
    v.radius = radius;
    v.param = param;
    return v;
}

// Rounded rect via SDF. The quad covers the whole rect; the shader discards
// outside. For crisp AA no padding is needed.
static void PushSdfRect(Canvas& cv, const Rect& r, float radius, const Color& c,
                        BatchKind kind, int shapeType, const Rect* pad = nullptr, float param = 0.f) {
    if (r.w <= 0.f || r.h <= 0.f || c.a <= 0.001f) return;
    Rect q = r;
    if (pad) q = {r.x - pad->x, r.y - pad->y, r.w + pad->x + pad->r(), r.h + pad->y + pad->b()};
    cv.Begin(kind, shapeType, -1);
    float hw = q.w * 0.5f, hh = q.h * 0.5f;
    Vertex a = ShapeVert(q.x, q.y, 0.f, 0.f, hw, hh, radius, param, c);
    Vertex b = ShapeVert(q.x + q.w, q.y, 1.f, 0.f, hw, hh, radius, param, c);
    Vertex cc = ShapeVert(q.x + q.w, q.y + q.h, 1.f, 1.f, hw, hh, radius, param, c);
    Vertex d = ShapeVert(q.x, q.y + q.h, 0.f, 1.f, hw, hh, radius, param, c);
    cv.PushQuad(a, b, cc, d);
}

void Canvas::FillRect(const Rect& r, const Color& c) { PushSdfRect(*this, r, 0.f, c, BatchKind::Sdf, 0); }
void Canvas::FillRounded(const Rect& r, float radius, const Color& c) {
    radius = std::clamp(radius, 0.f, std::min(r.w, r.h) * 0.5f);
    PushSdfRect(*this, r, radius, c, BatchKind::Sdf, 0);
}
void Canvas::StrokeRounded(const Rect& r, float radius, float width, const Color& c) {
    radius = std::clamp(radius, 0.f, std::min(r.w, r.h) * 0.5f);
    PushSdfRect(*this, r, radius, c, BatchKind::Sdf, 2, nullptr, width);
}
void Canvas::Shadow(const Rect& r, float radius, float blur, const Color& c, float dy) {
    if (c.a <= 0.001f || blur <= 0.f) return;
    if (blur > 64.f) blur = 64.f;
    Rect sh(r.x, r.y + dy, r.w, r.h);
    // The shader spreads alpha by up to ~64px; the quad is padded so the blur is not clipped.
    float padXY = 68.f;
    Rect pad(padXY, padXY, padXY, padXY);
    radius = std::clamp(radius, 0.f, std::min(r.w, r.h) * 0.5f);
    PushSdfRect(*this, sh, radius, c, BatchKind::Sdf, 1, &pad, blur);
}

void Canvas::FillCircle(const Vec2& center, float radius, const Color& c) {
    Rect r(center.x - radius, center.y - radius, radius * 2, radius * 2);
    PushSdfRect(*this, r, radius, c, BatchKind::Sdf, 0);
}

void Canvas::GradientQuad(const Rect& r, const Color& tl, const Color& tr, const Color& bl, const Color& br) {
    if (r.w <= 0.f || r.h <= 0.f) return;
    Begin(BatchKind::Sdf, 0, -1);
    Vertex a = ShapeVert(r.x, r.y, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, tl);
    Vertex b = ShapeVert(r.x + r.w, r.y, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, tr);
    Vertex c = ShapeVert(r.x + r.w, r.y + r.h, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, br);
    Vertex d = ShapeVert(r.x, r.y + r.h, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, bl);
    PushQuad(a, b, c, d);
}

void Canvas::GradientV(const Rect& r, const Color& top, const Color& bottom) { GradientQuad(r, top, top, bottom, bottom); }
void Canvas::GradientH(const Rect& r, const Color& left, const Color& right) { GradientQuad(r, left, right, left, right); }

void Canvas::Triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& col) {
    Begin(BatchKind::Sdf, 0, -1);
    uint32_t base = (uint32_t)verts.size();
    verts.push_back(ShapeVert(a.x, a.y, 0, 0, 0, 0, 0, 0, col));
    verts.push_back(ShapeVert(b.x, b.y, 0, 0, 0, 0, 0, 0, col));
    verts.push_back(ShapeVert(c.x, c.y, 0, 0, 0, 0, 0, 0, col));
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    batches.back().indexCount += 3;
}

void Canvas::Line(const Vec2& a, const Vec2& b, float width, const Color& c) {
    Vec2 d = b - a;
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 0.001f) return;
    Vec2 n(-d.y / len * width * 0.5f, d.x / len * width * 0.5f);
    Begin(BatchKind::Sdf, 0, -1);
    Vertex v0 = ShapeVert(a.x + n.x, a.y + n.y, 0, 0, 0, 0, 0, 0, c);
    Vertex v1 = ShapeVert(b.x + n.x, b.y + n.y, 0, 0, 0, 0, 0, 0, c);
    Vertex v2 = ShapeVert(b.x - n.x, b.y - n.y, 0, 0, 0, 0, 0, 0, c);
    Vertex v3 = ShapeVert(a.x - n.x, a.y - n.y, 0, 0, 0, 0, 0, 0, c);
    PushQuad(v0, v1, v2, v3);
}

void Canvas::Image(int textureId, const Rect& dst, const Color& tint, float u0, float v0, float u1, float v1) {
    if (dst.w <= 0.f || dst.h <= 0.f) return;
    Begin(BatchKind::Image, 0, textureId);
    Vertex a = ShapeVert(dst.x, dst.y, 0, 0, 0, 0, 0, 0, tint);
    a.u = u0; a.v = v0;
    Vertex b = ShapeVert(dst.x + dst.w, dst.y, 0, 0, 0, 0, 0, 0, tint);
    b.u = u1; b.v = v0;
    Vertex c = ShapeVert(dst.x + dst.w, dst.y + dst.h, 0, 0, 0, 0, 0, 0, tint);
    c.u = u1; c.v = v1;
    Vertex d = ShapeVert(dst.x, dst.y + dst.h, 0, 0, 0, 0, 0, 0, tint);
    d.u = u0; d.v = v1;
    PushQuad(a, b, c, d);
}

void Canvas::ImageSub(int textureId, const Rect& dst, const Rect& uv, const Color& tint) {
    Image(textureId, dst, tint, uv.x, uv.y, uv.r(), uv.b());
}

}  // namespace crit
