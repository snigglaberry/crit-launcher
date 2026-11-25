// Crit Launcher Native — small platform-independent helpers.
#include "core/base.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace crit {

// ---------------------------------------------------------------- utf8/utf16
string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = (int)w.size();
#ifdef _WIN32
    int need = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
    string out(need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), n, out.data(), need, nullptr, nullptr);
    return out;
#else
    string out;
    for (wchar_t wc : w) {
        uint32_t c = (uint32_t)wc;
        if (c < 0x80) out.push_back((char)c);
        else if (c < 0x800) { out.push_back((char)(0xC0 | (c >> 6))); out.push_back((char)(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { out.push_back((char)(0xE0 | (c >> 12))); out.push_back((char)(0x80 | ((c >> 6) & 0x3F))); out.push_back((char)(0x80 | (c & 0x3F))); }
        else { out.push_back((char)(0xF0 | (c >> 18))); out.push_back((char)(0x80 | ((c >> 12) & 0x3F))); out.push_back((char)(0x80 | ((c >> 6) & 0x3F))); out.push_back((char)(0x80 | (c & 0x3F))); }
    }
    return out;
#endif
}

std::wstring ToWide(const string& s) {
    if (s.empty()) return {};
    int n = (int)s.size();
#ifdef _WIN32
    int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), n, nullptr, 0);
    std::wstring out(need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), n, out.data(), need);
    return out;
#else
    // decode UTF-8 into wchar_t (4-byte on linux)
    std::wstring out;
    auto d = Utf8Decode(s);
    for (uint32_t c : d) out.push_back((wchar_t)c);
    return out;
#endif
}

std::vector<uint32_t> Utf8Decode(string_view s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { i++; out.push_back(0xFFFD); continue; }
        if (i + extra >= s.size()) { out.push_back(0xFFFD); break; }
        for (int k = 1; k <= extra; k++) cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
        i += extra + 1;
        out.push_back(cp);
    }
    return out;
}

int Utf8Len(string_view s) {
    int n = 0;
    for (char c : s) if (((uint8_t)c & 0xC0) != 0x80) n++;
    return n;
}

string Utf8Sub(string_view s, int start, int count) {
    string out;
    int idx = 0, taken = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t len = 1;
        uint8_t c = (uint8_t)s[i];
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (idx >= start && (count < 0 || taken < count)) { out.append(s.substr(i, len)); taken++; }
        idx++;
        i += len;
    }
    return out;
}

// ---------------------------------------------------------------- strings
string Lower(string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

string Trim(const string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    return s.substr(a, b - a);
}

bool StartsWith(string_view s, string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
bool EndsWith(string_view s, string_view p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

bool IEquals(string_view a, string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::vector<string> Split(const string& s, char sep) {
    std::vector<string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

string Join(const std::vector<string>& parts, const string& sep) {
    string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

string ReplaceAll(string s, string_view from, string_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

string Format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    string out;
    if (n > 0) {
        out.resize((size_t)n);
        vsnprintf(out.data(), (size_t)n + 1, fmt, ap2);
    }
    va_end(ap2);
    return out;
}

string DecodeEntities(const string& s) {
    string out = s;
    out = ReplaceAll(out, "&amp;", "&");
    out = ReplaceAll(out, "&lt;", "<");
    out = ReplaceAll(out, "&gt;", ">");
    out = ReplaceAll(out, "&quot;", "\"");
    out = ReplaceAll(out, "&#39;", "'");
    out = ReplaceAll(out, "&apos;", "'");
    out = ReplaceAll(out, "&nbsp;", " ");
    return out;
}

string UrlEncode(const string& s) {
    static const char* hex = "0123456789ABCDEF";
    string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

string QueryParam(const string& url, const string& key) {
    size_t q = url.find('?');
    if (q == string::npos) return {};
    auto parts = Split(url.substr(q + 1), '&');
    for (auto& p : parts) {
        size_t eq = p.find('=');
        string k = eq == string::npos ? p : p.substr(0, eq);
        if (k == key) return eq == string::npos ? "" : p.substr(eq + 1);
    }
    return {};
}

// ---------------------------------------------------------------- curves
static float Bez(float p1, float p2, float t) {
    float mt = 1.f - t;
    return 3.f * mt * mt * t * p1 + 3.f * mt * t * t * p2 + t * t * t;
}

float CubicBezier(float x1, float y1, float x2, float y2, float x) {
    if (x <= 0.f) return 0.f;
    if (x >= 1.f) return 1.f;
    // solve for t given x (Newton + fallback bisection)
    float t = x;
    for (int i = 0; i < 8; i++) {
        float cx = Bez(x1, x2, t) - x;
        if (std::fabs(cx) < 1e-5f) break;
        float mt = 1.f - t;
        float dx = 3.f * mt * mt * x1 + 6.f * mt * t * (x2 - x1) + 3.f * t * t * (1.f - x2);
        if (std::fabs(dx) < 1e-6f) break;
        t -= cx / dx;
        t = clampf(t, 0.f, 1.f);
    }
    float lo = 0.f, hi = 1.f;
    if (std::fabs(Bez(x1, x2, t) - x) > 1e-4f) {
        t = x;
        for (int i = 0; i < 24; i++) {
            float cx = Bez(x1, x2, t);
            if (std::fabs(cx - x) < 1e-5f) break;
            if (cx < x) lo = t; else hi = t;
            t = (lo + hi) * 0.5f;
        }
    }
    return Bez(y1, y2, t);
}

// ---------------------------------------------------------------- time
double NowSeconds() {
#ifdef _WIN32
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

uint64_t UnixMillis() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000ULL) / 10000ULL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
#endif
}

string FormatDurationLong(int64_t seconds) {
    if (seconds < 60) return Format("%llds", (long long)seconds);
    int64_t m = seconds / 60;
    if (m < 60) return Format("%lldm", (long long)m);
    int64_t h = m / 60;
    m %= 60;
    if (h < 24) return m ? Format("%lldh %lldm", (long long)h, (long long)m) : Format("%lldh", (long long)h);
    int64_t d = h / 24;
    h %= 24;
    return h ? Format("%lldd %lldh", (long long)d, (long long)h) : Format("%lldd", (long long)d);
}

string FormatDurationShort(int64_t seconds) {
    if (seconds < 60) return Format("%llds", (long long)seconds);
    int64_t m = seconds / 60;
    if (m < 60) return Format("%lldm", (long long)m);
    int64_t h = m / 60;
    if (h < 24) return Format("%lldh", (long long)h);
    return Format("%lldd", (long long)(h / 24));
}

string FormatDateShort(uint64_t unixSeconds) {
    time_t t = (time_t)unixSeconds;
    tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    static const char* mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return Format("%s %d, %d", mon[tmv.tm_mon % 12], tmv.tm_mday, tmv.tm_year + 1900);
}

string FormatClockNow() {
    time_t t = time(nullptr);
    tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return Format("%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

string RelativeAgo(uint64_t thenMs) {
    uint64_t now = UnixMillis();
    if (thenMs > now) return "just now";
    int64_t s = (int64_t)((now - thenMs) / 1000);
    if (s < 60) return "just now";
    int64_t m = s / 60;
    if (m < 60) return Format("%lld minute%s ago", (long long)m, m == 1 ? "" : "s");
    int64_t h = m / 60;
    if (h < 24) return Format("%lld hour%s ago", (long long)h, h == 1 ? "" : "s");
    int64_t d = h / 24;
    return Format("%lld day%s ago", (long long)d, d == 1 ? "" : "s");
}

// ---------------------------------------------------------------- logging
static std::mutex g_logMutex;
static string g_logPath;
static std::vector<string> g_logTail;

void LogInit(const string& dataDir, const string& fileName) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    g_logPath = dataDir + "/" + fileName;
#ifdef _WIN32
    // truncate old log
    FILE* f = nullptr;
    fopen_s(&f, g_logPath.c_str(), "w");
    if (f) fclose(f);
#endif
}

static void LogWrite(bool isError, const char* fmt, va_list ap) {
    char buf[4096];
    vsnprintf(buf, sizeof buf, fmt, ap);
    string line = Format("[%s] %s", FormatClockNow().c_str(), buf);
    std::lock_guard<std::mutex> lk(g_logMutex);
    g_logTail.push_back((isError ? "ERROR " : "") + line);
    if (g_logTail.size() > 4000) g_logTail.erase(g_logTail.begin(), g_logTail.begin() + 1000);
    if (!g_logPath.empty()) {
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, g_logPath.c_str(), "a");
#else
        f = fopen(g_logPath.c_str(), "a");
#endif
        if (f) {
            fprintf(f, "%s%s\n", isError ? "ERROR " : "", line.c_str());
            fclose(f);
        }
    }
}

void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    LogWrite(false, fmt, ap);
    va_end(ap);
}

void LogError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    LogWrite(true, fmt, ap);
    va_end(ap);
}

string LogFilePath() {
    std::lock_guard<std::mutex> lk(g_logMutex);
    return g_logPath;
}

std::vector<string> LogTail(size_t maxLines) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logTail.size() <= maxLines) return g_logTail;
    return std::vector<string>(g_logTail.end() - maxLines, g_logTail.end());
}

}  // namespace crit
