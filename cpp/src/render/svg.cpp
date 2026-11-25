// Crit Launcher Native — tiny SVG (path/line/circle/rect/polygon) rasterizer for UI icons.
#include <cctype>

#include "render/font.h"

namespace crit {

namespace {

struct SubPath {
    std::vector<Vec2> pts;
    bool closed = false;
};

struct Shape {
    std::vector<SubPath> subs;
};

struct Scanner {
    const string& s;
    size_t i = 0;
    explicit Scanner(const string& src) : s(src) {}
    void SkipWs() {
        while (i < s.size() && (unsigned char)s[i] <= ' ') i++;
        // commas act as separators too
        while (i < s.size() && (s[i] == ',' || (unsigned char)s[i] <= ' ')) i++;
    }
    bool Eof() const { return i >= s.size(); }
    char Peek() const { return i < s.size() ? s[i] : '\0'; }
    bool ReadFloat(float& out) {
        SkipWs();
        if (i >= s.size()) return false;
        char* end = nullptr;
        out = std::strtof(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        i = (size_t)(end - s.c_str());
        return true;
    }
    bool ReadFlag(float& out) {
        SkipWs();
        if (i >= s.size()) return false;
        if (s[i] == '0' || s[i] == '1') {
            out = s[i] == '1' ? 1.f : 0.f;
            i++;
            return true;
        }
        return false;
    }
    bool ReadPair(float& x, float& y) {
        if (!ReadFloat(x)) return false;
        // allow compact "10-5" style negative numbers
        if (!ReadFloat(y)) return false;
        return true;
    }
    bool NumberNext() {
        SkipWs();
        if (i >= s.size()) return false;
        char c = s[i];
        return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
    }
};

void AddPoly(Shape& sh, SubPath sp) { sh.subs.push_back(std::move(sp)); }

void FlattenArc(SubPath& sp, Vec2 from, float rx, float ry, float rotDeg, bool largeArc, bool sweep, Vec2 to) {
    if (rx == 0.f || ry == 0.f) {
        sp.pts.push_back(to);
        return;
    }
    float phi = rotDeg * kPi / 180.f;
    float cosP = std::cos(phi), sinP = std::sin(phi);
    float dx2 = (from.x - to.x) * 0.5f, dy2 = (from.y - to.y) * 0.5f;
    float x1p = cosP * dx2 + sinP * dy2;
    float y1p = -sinP * dx2 + cosP * dy2;
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.f) {
        float sl = std::sqrt(lambda);
        rx *= sl;
        ry *= sl;
    }
    float sign = (largeArc != sweep) ? 1.f : -1.f;
    float num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    float co = den == 0.f ? 0.f : sign * std::sqrt(std::max(0.f, num / den));
    float cxp = co * (rx * y1p / ry);
    float cyp = co * (-ry * x1p / rx);
    float cx = cosP * cxp - sinP * cyp + (from.x + to.x) * 0.5f;
    float cy = sinP * cxp + cosP * cyp + (from.y + to.y) * 0.5f;
    auto angle = [](float ux, float uy, float vx, float vy) {
        float dot = ux * vx + uy * vy;
        float len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        float a = std::acos(clampf(len == 0.f ? 0.f : dot / len, -1.f, 1.f));
        return (ux * vy - uy * vx) < 0.f ? -a : a;
    };
    float theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dTheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dTheta > 0.f) dTheta -= 2.f * kPi;
    if (sweep && dTheta < 0.f) dTheta += 2.f * kPi;
    int steps = std::max(6, (int)(std::fabs(dTheta) / (kPi / 12.f)));
    for (int k = 1; k <= steps; k++) {
        float th = theta1 + dTheta * (float)k / (float)steps;
        float ex = rx * std::cos(th), ey = ry * std::sin(th);
        float px = cosP * ex - sinP * ey + cx;
        float py = sinP * ex + cosP * ey + cy;
        sp.pts.push_back({px, py});
    }
}

// parse a path "d" attribute into polylines
void ParsePath(const string& d, Shape& out) {
    Scanner sc(d);
    SubPath cur;
    Vec2 pos{0, 0}, start{0, 0}, lastCtrl{0, 0};
    char cmd = 0, prevCmd = 0;
    bool haveSub = false;
    auto flush = [&](bool closed) {
        if (haveSub && cur.pts.size() >= 2) {
            cur.closed = closed;
            AddPoly(out, cur);
        }
        cur = SubPath{};
        haveSub = false;
    };
    while (!sc.Eof()) {
        sc.SkipWs();
        if (sc.Eof()) break;
        char c = sc.Peek();
        if (std::isalpha((unsigned char)c)) {
            cmd = c;
            sc.i++;
            if (cmd == 'Z' || cmd == 'z') {
                if (haveSub) {
                    cur.pts.push_back(start);
                    flush(true);
                }
                pos = start;
                prevCmd = cmd;
                continue;
            }
        } else if (cmd == 0) {
            break;
        } else if (cmd == 'M') {
            cmd = 'L';
        } else if (cmd == 'm') {
            cmd = 'l';
        }
        bool rel = std::islower((unsigned char)cmd) != 0;
        char u = (char)std::toupper((unsigned char)cmd);
        float a, b, cc, dd, e, f, g, h;
        auto P = [&](Vec2 v) { return rel ? Vec2(pos.x + v.x, pos.y + v.y) : v; };
        switch (u) {
            case 'M': {
                if (!sc.ReadPair(a, b)) return;
                Vec2 p = P({a, b});
                flush(false);
                cur.pts.push_back(p);
                haveSub = true;
                pos = p;
                start = p;
                break;
            }
            case 'L': {
                if (!sc.ReadPair(a, b)) return;
                Vec2 p = P({a, b});
                if (!haveSub) { cur.pts.push_back(pos); haveSub = true; }
                cur.pts.push_back(p);
                pos = p;
                break;
            }
            case 'H': {
                if (!sc.ReadFloat(a)) return;
                Vec2 p(rel ? pos.x + a : a, pos.y);
                if (!haveSub) { cur.pts.push_back(pos); haveSub = true; }
                cur.pts.push_back(p);
                pos = p;
                break;
            }
            case 'V': {
                if (!sc.ReadFloat(a)) return;
                Vec2 p(pos.x, rel ? pos.y + a : a);
                if (!haveSub) { cur.pts.push_back(pos); haveSub = true; }
                cur.pts.push_back(p);
                pos = p;
                break;
            }
            case 'C': {
                if (!(sc.ReadPair(a, b) && sc.ReadPair(cc, dd) && sc.ReadPair(e, f))) return;
                Vec2 c1 = P({a, b}), c2 = P({cc, dd}), p = P({e, f});
                if (!haveSub) { cur.pts.push_back(pos); haveSub = true; }
                int steps = 24;
                for (int k = 1; k <= steps; k++) {
                    float t = (float)k / steps, mt = 1 - t;
                    Vec2 q = pos * (mt * mt * mt) + c1 * (3 * mt * mt * t) + c2 * (3 * mt * t * t) + p * (t * t * t);
                    cur.pts.push_back(q);
                }
                lastCtrl = c2;
                pos = p;
                break;
            }
            case 'S': {
                if (!(sc.ReadPair(cc, dd) && sc.ReadPair(e, f))) return;
                Vec2 c1 = pos;
                if (prevCmd == 'C' || prevCmd == 'c' || prevCmd == 'S' || prevCmd == 's') c1 = pos * 2.f - lastCtrl;
                Vec2 c2 = P({cc, dd}), p = P({e, f});
                if (!haveSub) { cur.pts.push_back(pos); haveSub = true; }
                int steps = 24;
                for (int k = 1; k <= steps; k++) {
                    float t = (float)k / steps, mt = 1 - t;
                    Vec2 q = pos * (mt * mt * mt) + c1 * (3 * mt * mt * t) + c2 * (3 * mt * t * t) + p * (t * t * t);
                    cur.pts.push_back(q);
                }
                lastCtrl = c2;
                pos = p;
                break;
            }
            case 'Q': {
                if (!(sc.ReadPair(a, b) && sc.ReadPair(e, f))) return;
                Vec2 q1 = P({a, b}), p = P({e, f});
                if (!haveSub) { cur.pts.push_back(pos); haveSub = true; }
                int steps = 20;
                for (int k = 1; k <= steps; k++) {
                    float t = (float)k / steps, mt = 1 - t;
                    Vec2 q = pos * (mt * mt) + q1 * (2 * mt * t) + p * (t * t);
                    cur.pts.push_back(q);
                }
                lastCtrl = q1;
                pos = p;
                break;
            }
            case 'T': {
                if (!sc.ReadPair(e, f)) return;
                Vec2 q1 = pos;
                if (prevCmd == 'Q' || prevCmd == 'q' || prevCmd == 'T' || prevCmd == 't') q1 = pos * 2.f - lastCtrl;
                Vec2 p = P({e, f});
                if (!haveSub) { cur.pts.push_back(pos); haveSub = true; }
                int steps = 20;
                for (int k = 1; k <= steps; k++) {
                    float t = (float)k / steps, mt = 1 - t;
                    Vec2 q = pos * (mt * mt) + q1 * (2 * mt * t) + p * (t * t);
                    cur.pts.push_back(q);
                }
                lastCtrl = q1;
                pos = p;
                break;
            }
            case 'A': {
                float rx, ry, rot, la, sw;
                if (!sc.ReadFloat(rx)) return;
                if (!sc.ReadFloat(ry)) return;
                if (!sc.ReadFloat(rot)) return;
                if (!sc.ReadFlag(la)) return;
                if (!sc.ReadFlag(sw)) return;
                if (!sc.ReadPair(e, f)) return;
                Vec2 p = P({e, f});
                if (!haveSub) { cur.pts.push_back(pos); haveSub = true; }
                FlattenArc(cur, pos, rx, ry, rot, la > 0.5f, sw > 0.5f, p);
                pos = p;
                break;
            }
            default:
                return;
        }
        prevCmd = cmd;
        g = h = 0;
    }
    flush(false);
}

float AttrFloat(const string& tag, const char* name, float def) {
    string key = string(name) + "=";
    size_t p = tag.find(key);
    if (p == string::npos) return def;
    p += key.size();
    char q = tag[p] == '"' || tag[p] == '\'' ? tag[p] : '\0';
    if (q) p++;
    return std::strtof(tag.c_str() + p, nullptr);
}

string AttrStr(const string& tag, const char* name) {
    string key = string(name) + "=";
    size_t p = tag.find(key);
    if (p == string::npos) return {};
    p += key.size();
    char q = tag[p];
    size_t e = tag.find(q, p + 1);
    if (e == string::npos) return {};
    return tag.substr(p + 1, e - p - 1);
}

void CirclePoly(Shape& sh, float cx, float cy, float r) {
    SubPath sp;
    int steps = 64;
    for (int i = 0; i < steps; i++) {
        float a = (float)i / steps * 2.f * kPi;
        sp.pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    sp.closed = true;
    AddPoly(sh, sp);
}

void RoundRectPoly(Shape& sh, float x, float y, float w, float h, float r) {
    if (r < 0.01f) {
        SubPath sp;
        sp.pts = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
        sp.closed = true;
        AddPoly(sh, sp);
        return;
    }
    SubPath sp;
    r = std::min(r, std::min(w, h) * 0.5f);
    int seg = 8;
    struct Corner { float cx, cy, a0; };
    Corner corners[4] = {
        {x + w - r, y + r, -kPi * 0.5f},
        {x + w - r, y + h - r, 0.f},
        {x + r, y + h - r, kPi * 0.5f},
        {x + r, y + r, kPi},
    };
    for (auto& c : corners) {
        for (int i = 0; i <= seg; i++) {
            float a = c.a0 + (float)i / seg * (kPi * 0.5f);
            sp.pts.push_back({c.cx + r * std::cos(a), c.cy + r * std::sin(a)});
        }
    }
    sp.closed = true;
    AddPoly(sh, sp);
}

// scanline coverage: union fill of all closed polygons (nonzero winding)
void RasterizeShape(const Shape& sh, std::vector<float>& cov, int size, float scale, float strokeW, bool fill,
                    bool stroke) {
    struct Edge { float x0, y0, x1, y1; int dir; };
    std::vector<Edge> edges;
    auto addContour = [&](const std::vector<Vec2>& pts, bool closed, bool asStroke) {
        size_t n = pts.size();
        if (n < 2) return;
        if (asStroke) {
            for (size_t i = 0; i + 1 < n; i++) {
                Vec2 a = pts[i], b = pts[i + 1];
                Vec2 d = b - a;
                float len = std::sqrt(d.x * d.x + d.y * d.y);
                if (len < 1e-4f) continue;
                Vec2 nrm(-d.y / len * strokeW * 0.5f, d.x / len * strokeW * 0.5f);
                Vec2 quad[4] = {a + nrm, b + nrm, b - nrm, a - nrm};
                for (int k = 0; k < 4; k++) {
                    Vec2 p0 = quad[k], p1 = quad[(k + 1) % 4];
                    if (p0.y == p1.y) continue;
                    edges.push_back({p0.x * scale, p0.y * scale, p1.x * scale, p1.y * scale, p0.y < p1.y ? 1 : -1});
                }
            }
        } else {
            for (size_t i = 0; i < n; i++) {
                Vec2 p0 = pts[i], p1 = pts[(i + 1) % n];
                if (i + 1 == n && !closed) break;
                if (p0.y == p1.y) continue;
                edges.push_back({p0.x * scale, p0.y * scale, p1.x * scale, p1.y * scale, p0.y < p1.y ? 1 : -1});
            }
        }
    };
    for (auto& sp : sh.subs) addContour(sp.pts, sp.closed, stroke);
    if (fill) {
        for (auto& sp : sh.subs) addContour(sp.pts, sp.closed, false);
    }
    if (edges.empty()) return;
    // 4x vertical supersampling, exact horizontal coverage per span
    const int kSub = 4;
    std::vector<float> xs;
    for (int y = 0; y < size; y++) {
        float covRow = 0.f;
        for (int s = 0; s < kSub; s++) {
            float sy = (float)y + ((float)s + 0.5f) / kSub;
            xs.clear();
            for (auto& e : edges) {
                float y0 = std::min(e.y0, e.y1), y1 = std::max(e.y0, e.y1);
                if (sy < y0 || sy >= y1) continue;
                float t = (sy - e.y0) / (e.y1 - e.y0);
                xs.push_back(e.x0 + (e.x1 - e.x0) * t);
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());
            for (size_t i = 0; i + 1 < xs.size(); i += 2) {
                float xa = xs[i], xb = xs[i + 1];
                if (xb <= xa) continue;
                for (int x = (int)std::floor(xa); x <= (int)std::ceil(xb); x++) {
                    if (x < 0 || x >= size) continue;
                    float l = std::max(xa, (float)x), r = std::min(xb, (float)x + 1.f);
                    if (r > l) cov[(size_t)y * size + x] += (r - l) / kSub;
                }
            }
            covRow += 1.f;
        }
        (void)covRow;
    }
    for (auto& v : cov) v = std::min(1.f, v);
}

}  // namespace

std::shared_ptr<Texture> RenderSvgIcon(const string& svgBody, float size, int strokeWidth, bool fill) {
    int px = std::max(4, (int)std::lround(size * 2.f));  // supersample x2 for crisp strokes
    Shape shape;
    size_t pos = 0;
    while (pos < svgBody.size()) {
        size_t lt = svgBody.find('<', pos);
        if (lt == string::npos) break;
        size_t gt = svgBody.find('>', lt);
        if (gt == string::npos) break;
        string tag = svgBody.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        if (tag.empty() || tag[0] == '/') continue;
        bool selfClosing = tag.back() == '/';
        string name = tag.substr(0, tag.find_first_of(" \t/"));
        if (name == "path") {
            string d = AttrStr(tag, "d");
            ParsePath(d, shape);
        } else if (name == "line") {
            SubPath sp;
            sp.pts = {{AttrFloat(tag, "x1", 0), AttrFloat(tag, "y1", 0)}, {AttrFloat(tag, "x2", 0), AttrFloat(tag, "y2", 0)}};
            AddPoly(shape, sp);
        } else if (name == "circle") {
            CirclePoly(shape, AttrFloat(tag, "cx", 12), AttrFloat(tag, "cy", 12), AttrFloat(tag, "r", 10));
        } else if (name == "rect") {
            RoundRectPoly(shape, AttrFloat(tag, "x", 0), AttrFloat(tag, "y", 0), AttrFloat(tag, "width", 0),
                          AttrFloat(tag, "height", 0), AttrFloat(tag, "rx", 0));
        } else if (name == "polygon" || name == "polyline") {
            string pts = AttrStr(tag, "points");
            SubPath sp;
            Scanner sc(pts);
            float x, y;
            while (sc.ReadPair(x, y)) sp.pts.push_back({x, y});
            sp.closed = name == "polygon";
            AddPoly(shape, sp);
        }
        (void)selfClosing;
    }
    std::vector<float> cov((size_t)px * px, 0.f);
    float scale = (float)px / 24.f;
    float stroke = (float)strokeWidth * scale;
    if (stroke < 1.2f) stroke = 1.2f;
    RasterizeShape(shape, cov, px, scale, stroke, fill, true);
    auto tex = std::make_shared<Texture>();
    tex->w = px;
    tex->h = px;
    tex->rgba.assign((size_t)px * px * 4, 0);
    for (size_t i = 0; i < cov.size(); i++) {
        uint8_t a = (uint8_t)std::lround(clamp01(cov[i]) * 255.f);
        tex->rgba[i * 4 + 0] = 255;
        tex->rgba[i * 4 + 1] = 255;
        tex->rgba[i * 4 + 2] = 255;
        tex->rgba[i * 4 + 3] = a;
    }
    return tex;
}

}  // namespace crit
