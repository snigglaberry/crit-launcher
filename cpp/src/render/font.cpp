// Crit Launcher Native — TrueType parser + glyph atlas + text layout.
#include "render/font.h"

#include <map>
#include <unordered_map>

#include "core/fontdata.h"

#ifdef CRIT_USE_STB
#ifndef STB_TRUETYPE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#endif
#include "stb_truetype.h"
#endif

namespace crit {

struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // px relative to pen origin / baseline
    float advance = 0;
    int page = 0;
};

struct FaceInfo {
    std::vector<uint8_t> data;
    stbtt_fontinfo info{};
};

struct SizeInfo {
    float scale = 1.f;
    float ascent = 0.f, descent = 0.f, lineGap = 0.f;
    float advanceExtra = 0.f;
};

struct FontAtlas::Impl {
    bool ready = false;
    std::map<uint64_t, Glyph> glyphs;               // key: weight|px|cp
    std::map<uint64_t, SizeInfo> sizes;             // key: weight|px
    std::map<int, FaceInfo> faces;                  // key: weight
    std::vector<std::shared_ptr<Texture>> pages;    // 1024x1024 each, RGBA
    std::vector<int> pageIds;                       // texture ids registered on the canvas
    int cursorX = 0, cursorY = 0, rowH = 0;
    int pageW = 1024, pageH = 1024;
    std::shared_ptr<Texture> white;
    int whiteId = 0;
    uint64_t generation = 1;
};

FontAtlas::~FontAtlas() { Shutdown(); }

static int NearestWeight(int w) {
    if (w <= 400) return 400;
    if (w <= 500) return 500;
    if (w <= 600) return 600;
    if (w <= 700) return 700;
    if (w <= 800) return 800;
    return 900;
}

static FaceInfo* PickFace(FontAtlas::Impl* impl, int weight) {
    weight = NearestWeight(weight);
    auto it = impl->faces.find(weight);
    if (it == impl->faces.end() && !impl->faces.empty()) it = impl->faces.begin();
    return it == impl->faces.end() ? nullptr : &it->second;
}

bool FontAtlas::Init(Canvas& canvas) {
    if (impl_) return impl_->ready;
    impl_ = new Impl();
    impl_->white = std::make_shared<Texture>();
    impl_->white->w = impl_->white->h = 1;
    impl_->white->rgba = {255, 255, 255, 255};
    canvas.white = impl_->white.get();
    impl_->whiteId = canvas.AddTexture(impl_->white);
    // three atlas pages are reserved up-front so texture ids never change
    for (int i = 0; i < 3; i++) {
        auto tex = std::make_shared<Texture>();
        tex->w = impl_->pageW;
        tex->h = impl_->pageH;
        tex->rgba.assign((size_t)impl_->pageW * impl_->pageH * 4, 0);
        impl_->pages.push_back(tex);
        impl_->pageIds.push_back(canvas.AddTexture(tex));
    }
    for (size_t i = 0; i < kEmbeddedFontCount; i++) {
        FaceInfo f;
        f.data.assign(kEmbeddedFonts[i].data, kEmbeddedFonts[i].data + kEmbeddedFonts[i].size);
        if (!stbtt_InitFont(&f.info, f.data.data(), stbtt_GetFontOffsetForIndex(f.data.data(), 0))) continue;
        impl_->faces[NearestWeight(kEmbeddedFonts[i].weight)] = std::move(f);
    }
    impl_->ready = !impl_->faces.empty();
    Log("font atlas ready (%d faces, %zu pages)", (int)impl_->faces.size(), impl_->pages.size());
    return impl_->ready;
}

void FontAtlas::Shutdown() {
    delete impl_;
    impl_ = nullptr;
}

FontAtlas::Face* FontAtlas::Pick(int weight, float px) { return (Face*)PickFace(impl_, weight); }

static SizeInfo* GetSize(FontAtlas::Impl* impl, FaceInfo* face, int weight, int px) {
    uint64_t key = ((uint64_t)weight << 16) | (uint32_t)px;
    auto it = impl->sizes.find(key);
    if (it != impl->sizes.end()) return &it->second;
    SizeInfo s;
    s.scale = stbtt_ScaleForPixelHeight(&face->info, (float)px);
    int a, d, l;
    stbtt_GetFontVMetrics(&face->info, &a, &d, &l);
    s.ascent = a * s.scale;
    s.descent = -d * s.scale;
    s.lineGap = l * s.scale;
    auto res = impl->sizes.emplace(key, s);
    return &res.first->second;
}

float FontAtlas::Ascender(int weight, float px) const {
    FontAtlas::Impl* impl = impl_;
    FaceInfo* f = PickFace(impl, weight);
    if (!f) return px * 0.8f;
    return GetSize(impl, f, NearestWeight(weight), std::max(1, (int)std::lround(px)))->ascent;
}

float FontAtlas::Descender(int weight, float px) const {
    FontAtlas::Impl* impl = impl_;
    FaceInfo* f = PickFace(impl, weight);
    if (!f) return px * 0.2f;
    return GetSize(impl, f, NearestWeight(weight), std::max(1, (int)std::lround(px)))->descent;
}

float FontAtlas::LineHeight(int weight, float px) const {
    FontAtlas::Impl* impl = impl_;
    FaceInfo* f = PickFace(impl, weight);
    if (!f) return px * 1.2f;
    SizeInfo* s = GetSize(impl, f, NearestWeight(weight), std::max(1, (int)std::lround(px)));
    return s->ascent + s->descent + s->lineGap;
}

static Glyph* FetchGlyph(FontAtlas::Impl* impl, int weight, int px, uint32_t cp) {
    weight = NearestWeight(weight);
    uint64_t key = ((uint64_t)weight << 42) ^ ((uint64_t)px << 32) ^ (cp & 0x1FFFFF);
    auto it = impl->glyphs.find(key);
    if (it != impl->glyphs.end()) return &it->second;
    FaceInfo* face = PickFace(impl, weight);
    if (!face) return nullptr;
    SizeInfo* sz = GetSize(impl, face, weight, px);
    int gi = stbtt_FindGlyphIndex(&face->info, (int)cp);
    if (gi == 0 && cp != ' ' && cp != 0) gi = stbtt_FindGlyphIndex(&face->info, '?');
    int advW = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&face->info, gi, &advW, &lsb);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&face->info, gi, sz->scale, sz->scale, 0.f, 0.f, &x0, &y0, &x1, &y1);
    Glyph g;
    g.advance = advW * sz->scale;
    g.x0 = (float)x0;
    g.y0 = (float)y0;
    g.x1 = (float)x1;
    g.y1 = (float)y1;
    int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
        if (impl->cursorX + gw + 2 > impl->pageW) {
            impl->cursorX = 0;
            impl->cursorY += impl->rowH + 2;
            impl->rowH = 0;
        }
        if (impl->cursorY + gh + 2 > impl->pageH) {
            // overflow: start over on page 0 (sizes used by the launcher fit easily)
            impl->cursorX = impl->cursorY = impl->rowH = 0;
            std::fill(impl->pages[0]->rgba.begin(), impl->pages[0]->rgba.end(), 0);
            impl->glyphs.clear();
        }
        int ax = impl->cursorX, ay = impl->cursorY;
        int pageIdx = 0;
        std::vector<uint8_t> bmp((size_t)gw * gh);
        stbtt_MakeGlyphBitmapSubpixel(&face->info, bmp.data(), gw, gh, gw, sz->scale, sz->scale, 0.f, 0.f, gi);
        Texture& page = *impl->pages[(size_t)pageIdx];
        for (int y = 0; y < gh; y++) {
            for (int x = 0; x < gw; x++) {
                uint8_t a = bmp[(size_t)y * gw + x];
                if (!a) continue;
                size_t dst = ((size_t)(ay + y) * impl->pageW + (ax + x)) * 4;
                page.rgba[dst + 0] = 255;
                page.rgba[dst + 1] = 255;
                page.rgba[dst + 2] = 255;
                page.rgba[dst + 3] = a;
            }
        }
        g.u0 = (float)ax / impl->pageW;
        g.v0 = (float)ay / impl->pageH;
        g.u1 = (float)(ax + gw) / impl->pageW;
        g.v1 = (float)(ay + gh) / impl->pageH;
        g.page = pageIdx;
        impl->cursorX = ax + gw + 2;
        impl->rowH = std::max(impl->rowH, gh);
        impl->generation++;
    } else {
        g.page = 0;
    }
    auto res = impl->glyphs.emplace(key, g);
    return &res.first->second;
}

static string UpperAscii(const string& in) {
    string u = in;
    for (char& c : u)
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return u;
}

float FontAtlas::MeasureLine(const string& text, const TextStyle& style) {
    if (!impl_ || !impl_->ready) return 0.f;
    int px = std::max(1, (int)std::lround(style.size));
    float w = 0.f;
    auto cps = Utf8Decode(text);
    for (uint32_t cp : cps) {
        Glyph* g = FetchGlyph(impl_, style.weight, px, cp);
        if (!g) continue;
        w += g->advance + style.letterSpacing;
    }
    if (!cps.empty()) w -= style.letterSpacing;
    return w;
}

struct LayoutLine {
    string text;
    float width = 0.f;
};

static std::vector<LayoutLine> LayoutText(FontAtlas& fonts, const string& in, const TextStyle& st, float maxWidth) {
    std::vector<LayoutLine> lines;
    string text = st.uppercase ? UpperAscii(in) : in;
    auto paras = Split(text, '\n');
    for (auto& para : paras) {
        if (maxWidth <= 1.f) {
            lines.push_back({para, fonts.MeasureLine(para, st)});
            continue;
        }
        string cur;
        float curW = 0.f;
        auto words = Split(para, ' ');
        for (size_t i = 0; i < words.size(); i++) {
            string cand = cur.empty() ? words[i] : cur + " " + words[i];
            float w = fonts.MeasureLine(cand, st);
            if (w <= maxWidth || cur.empty()) {
                cur = cand;
                curW = w;
            } else {
                lines.push_back({cur, curW});
                cur = words[i];
                curW = fonts.MeasureLine(cur, st);
            }
        }
        lines.push_back({cur, curW});
    }
    if (lines.empty()) lines.push_back({"", 0.f});
    return lines;
}

TextMetrics FontAtlas::Measure(const string& text, const TextStyle& style, float maxWidth) {
    TextMetrics m;
    float lh = LineHeight(style.weight, style.size);
    m.lineHeight = lh * 1.45f;
    auto lines = LayoutText(*this, text, style, maxWidth);
    m.lines = (int)lines.size();
    for (auto& l : lines) m.width = std::max(m.width, l.width);
    return m;
}

float FontAtlas::Draw(Canvas& cv, const string& text, const TextStyle& style, float x, float y, float maxWidth,
                      float maxHeight, int* lineCountOut) {
    if (!impl_ || !impl_->ready) return 0.f;
    auto lines = LayoutText(*this, text, style, maxWidth);
    float lh = LineHeight(style.weight, style.size);
    float lineBox = lh * 1.45f;
    float asc = Ascender(style.weight, style.size);
    float baselineOffset = asc + (lineBox - lh) * 0.5f;
    int px = std::max(1, (int)std::lround(style.size));
    int pageId0 = impl_->pageIds.empty() ? -1 : impl_->pageIds[0];
    float maxAvail = maxHeight > 0.f ? maxHeight : 1e9f;
    int drawn = 0;
    for (size_t li = 0; li < lines.size(); li++) {
        if ((float)li * lineBox + lineBox > maxAvail + 0.5f) break;
        string draw = lines[li].text;
        if (style.ellipsis && maxWidth > 0.f && lines[li].width > maxWidth) {
            int guard = 0;
            while (!draw.empty() && MeasureLine(draw + "\xE2\x80\xA6", style) > maxWidth && guard++ < 4000) {
                draw = Utf8Sub(draw, 0, Utf8Len(draw) - 1);
            }
            draw += "\xE2\x80\xA6";
        }
        float penX = x;
        float baseline = y + (float)li * lineBox + baselineOffset;
        for (uint32_t cp : Utf8Decode(draw)) {
            Glyph* g = FetchGlyph(impl_, style.weight, px, cp);
            if (!g) continue;
            if (g->u1 > g->u0 && style.color.a > 0.001f) {
                Rect dst(penX + g->x0, baseline + g->y0, g->x1 - g->x0, g->y1 - g->y0);
                int texId = pageId0 + g->page;
                cv.Image(texId, dst, style.color, g->u0, g->v0, g->u1, g->v1);
            }
            penX += g->advance + style.letterSpacing;
        }
        drawn++;
    }
    if (lineCountOut) *lineCountOut = drawn;
    return lineBox * (float)drawn;
}

float FontAtlas::DrawCenterLine(Canvas& cv, const string& text, const TextStyle& style, const Rect& box) {
    string shown = text;
    if (style.ellipsis && box.w > 0.f) {
        int guard = 0;
        while (!shown.empty() && MeasureLine(shown, style) > box.w && guard++ < 4000) {
            shown = Utf8Sub(shown, 0, Utf8Len(shown) - 1);
        }
        if (shown != text) shown += "\xE2\x80\xA6";
    }
    float w = MeasureLine(shown, style);
    float lh = LineHeight(style.weight, style.size);
    float x = box.x + (box.w - w) * 0.5f;
    float y = box.y + (box.h - lh * 1.45f) * 0.5f;
    Draw(cv, shown, style, x, y, 0.f, 0.f, nullptr);
    return w;
}

FontAtlas& Fonts() {
    static FontAtlas atlas;
    return atlas;
}

}  // namespace crit
