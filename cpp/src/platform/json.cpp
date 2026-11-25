// Crit Launcher Native — dependency-free JSON.
#include "platform/platform.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace crit {

static const Json kNullJson;

static void SkipWs(const string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
}

static bool ParseValue(const string& s, size_t& i, Json& out);

static void AppendUtf8(string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

static bool ParseString(const string& s, size_t& i, string& out) {
    if (s[i] != '"') return false;
    i++;
    while (i < s.size() && s[i] != '"') {
        char c = s[i];
        if (c == '\\') {
            i++;
            if (i >= s.size()) return false;
            char e = s[i];
            switch (e) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': {
                    if (i + 4 >= s.size()) return false;
                    uint32_t cp = (uint32_t)std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                        uint32_t lo = (uint32_t)std::strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16);
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: out.push_back(e); break;
            }
            i++;
        } else {
            out.push_back(c);
            i++;
        }
    }
    if (i >= s.size()) return false;
    i++;
    return true;
}

static bool ParseNumber(const string& s, size_t& i, double& out) {
    size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
    while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                            s[i] == '-' || s[i] == '+')) {
        if ((s[i] == '-' || s[i] == '+') && i > start + 1 && s[i - 1] != 'e' && s[i - 1] != 'E') break;
        i++;
    }
    if (i == start) return false;
    out = std::strtod(s.substr(start, i - start).c_str(), nullptr);
    return true;
}

static bool ParseValue(const string& s, size_t& i, Json& out) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '{') {
        i++;
        out.type = Json::Object;
        SkipWs(s, i);
        if (i < s.size() && s[i] == '}') {
            i++;
            return true;
        }
        while (i < s.size()) {
            SkipWs(s, i);
            string key;
            if (!ParseString(s, i, key)) return false;
            SkipWs(s, i);
            if (i >= s.size() || s[i] != ':') return false;
            i++;
            Json v;
            if (!ParseValue(s, i, v)) return false;
            out.obj.emplace_back(key, std::move(v));
            SkipWs(s, i);
            if (i < s.size() && s[i] == ',') {
                i++;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                i++;
                return true;
            }
            return false;
        }
        return false;
    }
    if (c == '[') {
        i++;
        out.type = Json::Array;
        SkipWs(s, i);
        if (i < s.size() && s[i] == ']') {
            i++;
            return true;
        }
        while (i < s.size()) {
            Json v;
            if (!ParseValue(s, i, v)) return false;
            out.arr.push_back(std::move(v));
            SkipWs(s, i);
            if (i < s.size() && s[i] == ',') {
                i++;
                continue;
            }
            if (i < s.size() && s[i] == ']') {
                i++;
                return true;
            }
            return false;
        }
        return false;
    }
    if (c == '"') {
        out.type = Json::String;
        return ParseString(s, i, out.str);
    }
    if (s.compare(i, 4, "true") == 0) {
        out.type = Json::Bool;
        out.b = true;
        i += 4;
        return true;
    }
    if (s.compare(i, 5, "false") == 0) {
        out.type = Json::Bool;
        out.b = false;
        i += 5;
        return true;
    }
    if (s.compare(i, 4, "null") == 0) {
        out.type = Json::Null;
        i += 4;
        return true;
    }
    out.type = Json::Number;
    return ParseNumber(s, i, out.num);
}

Json Json::parse(const string& text) {
    Json j;
    size_t i = 0;
    if (!ParseValue(text, i, j)) return Json();
    return j;
}

static void DumpString(string& out, const string& s) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", (unsigned char)c);
                    out += buf;
                } else out.push_back(c);
        }
    }
    out.push_back('"');
}

static void DumpValue(string& out, const Json& j, bool pretty, int depth) {
    auto indent = [&](int d) {
        if (!pretty) return;
        out.push_back('\n');
        out.append((size_t)d * 2, ' ');
    };
    switch (j.type) {
        case Json::Null: out += "null"; break;
        case Json::Bool: out += j.b ? "true" : "false"; break;
        case Json::Number: {
            char buf[64];
            if (j.num == (double)(int64_t)j.num) snprintf(buf, sizeof buf, "%lld", (long long)j.num);
            else snprintf(buf, sizeof buf, "%.17g", j.num);
            out += buf;
            break;
        }
        case Json::String: DumpString(out, j.str); break;
        case Json::Array:
            out.push_back('[');
            for (size_t i = 0; i < j.arr.size(); i++) {
                if (i) out.push_back(',');
                indent(depth + 1);
                DumpValue(out, j.arr[i], pretty, depth + 1);
            }
            if (!j.arr.empty()) indent(depth);
            out.push_back(']');
            break;
        case Json::Object:
            out.push_back('{');
            for (size_t i = 0; i < j.obj.size(); i++) {
                if (i) out.push_back(',');
                indent(depth + 1);
                DumpString(out, j.obj[i].first);
                out.push_back(':');
                if (pretty) out.push_back(' ');
                DumpValue(out, j.obj[i].second, pretty, depth + 1);
            }
            if (!j.obj.empty()) indent(depth);
            out.push_back('}');
            break;
    }
}

string Json::dump(bool pretty) const {
    string out;
    DumpValue(out, *this, pretty, 0);
    return out;
}

const Json& Json::operator[](size_t i) const {
    if (type != Array || i >= arr.size()) return kNullJson;
    return arr[i];
}

const Json& Json::operator[](const string& key) const {
    if (type == Object) {
        for (auto& kv : obj)
            if (kv.first == key) return kv.second;
    }
    return kNullJson;
}

Json& Json::set(const string& key, Json v) {
    if (type != Object) type = Object;
    for (auto& kv : obj) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return *this;
        }
    }
    obj.emplace_back(key, std::move(v));
    return *this;
}

Json& Json::push(Json v) {
    if (type != Array) type = Array;
    arr.push_back(std::move(v));
    return *this;
}

string Json::getString(const string& key, const string& def) const {
    const Json& v = (*this)[key];
    if (v.type == String) return v.str;
    if (v.type == Number) {
        char buf[64];
        snprintf(buf, sizeof buf, "%.0f", v.num);
        return buf;
    }
    if (v.type == Bool) return v.b ? "true" : "false";
    return def;
}

double Json::getNumber(const string& key, double def) const {
    const Json& v = (*this)[key];
    if (v.type == Number) return v.num;
    if (v.type == String) return std::strtod(v.str.c_str(), nullptr);
    if (v.type == Bool) return v.b ? 1 : 0;
    return def;
}

bool Json::getBool(const string& key, bool def) const {
    const Json& v = (*this)[key];
    if (v.type == Bool) return v.b;
    if (v.type == Number) return v.num != 0;
    return def;
}

bool Json::has(const string& key) const {
    if (type != Object) return false;
    for (auto& kv : obj)
        if (kv.first == key) return true;
    return false;
}

}  // namespace crit
