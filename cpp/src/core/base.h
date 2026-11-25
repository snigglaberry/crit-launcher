// Crit Launcher Native — shared basics: strings, math, colors, logging.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace crit {

// ---------------------------------------------------------------- vectors
struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2() = default;
    Vec2(float X, float Y) : x(X), y(Y) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    Rect() = default;
    Rect(float X, float Y, float W, float H) : x(X), y(Y), w(W), h(H) {}
    float r() const { return x + w; }
    float b() const { return y + h; }
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    bool empty() const { return w <= 0.f || h <= 0.f; }
    bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    bool intersects(const Rect& o) const { return x < o.r() && o.x < r() && y < o.b() && o.y < b(); }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    Rect intersect(const Rect& o) const {
        float nx = std::max(x, o.x), ny = std::max(y, o.y);
        float nr = std::min(r(), o.r()), nb = std::min(b(), o.b());
        return {nx, ny, std::max(0.f, nr - nx), std::max(0.f, nb - ny)};
    }
    static Rect tlwh(float x, float y, float w, float h) { return {x, y, w, h}; }
    static Rect ltrb(float l, float t, float rr, float bb) { return {l, t, rr - l, bb - t}; }
    static Rect xyxy(float l, float t, float rr, float bb) { return {l, t, rr - l, bb - t}; }
};

// ---------------------------------------------------------------- color
struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
    Color() = default;
    Color(float R, float G, float B, float A = 1.f) : r(R), g(G), b(B), a(A) {}
    static Color rgb(int R, int G, int B, float A = 1.f) {
        return {R / 255.f, G / 255.f, B / 255.f, A};
    }
    // #rrggbb or #rgb
    static Color hex(uint32_t v, float a = 1.f) {
        return {((v >> 16) & 0xff) / 255.f, ((v >> 8) & 0xff) / 255.f, (v & 0xff) / 255.f, a};
    }
    Color with(float A) const { return {r, g, b, A}; }
    Color alpha(float mult) const { return {r, g, b, a * mult}; }
    // mix toward o by t in [0,1]
    Color mix(const Color& o, float t) const {
        return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t};
    }
    Color darken(float f) const { return {r * (1 - f), g * (1 - f), b * (1 - f), a}; }
    Color lighten(float f) const { return mix(Color(1, 1, 1, a), f); }
    uint32_t rgba8() const {
        auto q = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.f, 1.f) * 255.f); };
        return (q(r) << 24) | (q(g) << 16) | (q(b) << 8) | q(a);
    }
    Color premul() const { return {r * a, g * a, b * a, a}; }
};

inline Color mix(const Color& a, const Color& b, float t) { return a.mix(b, t); }

// ---------------------------------------------------------------- math
constexpr float kPi = 3.14159265358979f;
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// CSS cubic-bezier(x1,y1,x2,y2) progress curve, solved like a browser does.
float CubicBezier(float x1, float y1, float x2, float y2, float x);
inline float EaseOut(float t) { return CubicBezier(0.f, 0.f, 0.58f, 1.f, t); }
inline float EaseInOut(float t) { return CubicBezier(0.42f, 0.f, 0.58f, 1.f, t); }
inline float EaseCss(float t) { return CubicBezier(0.22f, 0.8f, 0.24f, 1.f, t); }  // --ease

// ---------------------------------------------------------------- strings
using std::string;
using std::string_view;

string ToUtf8(const std::wstring& w);
std::wstring ToWide(const string& s);
string Lower(string s);
string Trim(const string& s);
bool StartsWith(string_view s, string_view p);
bool EndsWith(string_view s, string_view p);
std::vector<string> Split(const string& s, char sep);
string Join(const std::vector<string>& parts, const string& sep);
string ReplaceAll(string s, string_view from, string_view to);
string Format(const char* fmt, ...);
// UTF-8 helpers
std::vector<uint32_t> Utf8Decode(string_view s);
int Utf8Len(string_view s);
string Utf8Sub(string_view s, int startCodepoints, int countCodepoints);

// ASCII case-insensitive compare
bool IEquals(string_view a, string_view b);
// Collapse HTML-ish entities used by remote text (&amp; &lt; ...)
string DecodeEntities(const string& s);

// URL helpers
string UrlEncode(const string& s);
string QueryParam(const string& url, const string& key);

// ---------------------------------------------------------------- time
double NowSeconds();       // monotonic seconds
uint64_t UnixMillis();
string FormatDurationLong(int64_t seconds);   // "3h 12m"
string FormatDurationShort(int64_t seconds);  // "12m"
string FormatDateShort(uint64_t unixSeconds); // "Nov 25, 2026"
string FormatClockNow();                      // "17:42"
string RelativeAgo(uint64_t unixMsSec);       // "3 minutes ago"

// ---------------------------------------------------------------- logging
void LogInit(const string& dataDir, const string& fileName);
void Log(const char* fmt, ...);
void LogError(const char* fmt, ...);
string LogFilePath();
// last N log lines (for the Console page)
std::vector<string> LogTail(size_t maxLines);

// ---------------------------------------------------------------- misc
inline uint32_t Hash32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
inline uint32_t HashStr(string_view s) { return Hash32(s.data(), s.size()); }

}  // namespace crit
