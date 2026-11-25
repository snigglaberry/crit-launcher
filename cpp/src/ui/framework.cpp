// Crit Launcher Native — theme, widgets, overlays.
#include "ui/icons_data.h"
#include "ui/ui.h"

namespace crit {

float kScreenW = 1920.f;
float kScreenH = 1080.f;

// ---------------------------------------------------------------- theme
static Theme g_theme;
static string g_themeName = "green";

static Theme MakeTheme(const Color& accent, const Color& ink, const Color& hover, const Color& light, const Color& brand,
                       const Color& brandLight) {
    Theme t;
    t.bg = Color::hex(0x0b0c0e);
    t.panel = Color::hex(0x111318);
    t.panel2 = Color::hex(0x171a20);
    t.panel3 = Color::hex(0x1e222a);
    t.border = Color::hex(0x242831);
    t.borderSoft = Color::hex(0x1b1e25);
    t.text = Color::hex(0xe8eaed);
    t.dim = Color::hex(0x8b929c);
    t.faint = Color::hex(0x5d646e);
    t.accent = accent;
    t.accentInk = ink;
    t.accentHover = hover;
    t.accentLight = light;
    t.brand = brand;
    t.brandLight = brandLight;
    t.danger = Color::hex(0xf87171);
    t.warn = Color::hex(0xfbbf24);
    t.accentSoft = accent.with(0.12f);
    t.dangerSoft = t.danger.with(0.12f);
    t.run = accent;
    t.accentRgb[0] = accent.r * 255.f;
    t.accentRgb[1] = accent.g * 255.f;
    t.accentRgb[2] = accent.b * 255.f;
    return t;
}

void SetThemeByName(const string& name) {
    g_themeName = name;
    if (name == "blue") g_theme = MakeTheme(Color::hex(0x60a5fa), Color::hex(0x07152b), Color::hex(0x7fb6fb), Color::hex(0x93c5fd), Color::hex(0x4b8fe0), Color::hex(0xdce8f5));
    else if (name == "purple") g_theme = MakeTheme(Color::hex(0xa78bfa), Color::hex(0x140b2e), Color::hex(0xb9a2fc), Color::hex(0xc4b5fd), Color::hex(0x8b6ee0), Color::hex(0xe6e0f7));
    else if (name == "red") g_theme = MakeTheme(Color::hex(0xf05252), Color::hex(0x2a0707), Color::hex(0xf36b6b), Color::hex(0xf8a0a0), Color::hex(0xd64545), Color::hex(0xf7e0e0));
    else if (name == "orange") g_theme = MakeTheme(Color::hex(0xfb923c), Color::hex(0x2a1204), Color::hex(0xfdac6a), Color::hex(0xfdba74), Color::hex(0xe8792a), Color::hex(0xf7e8d8));
    else if (name == "pink") g_theme = MakeTheme(Color::hex(0xf472b6), Color::hex(0x2a0a1a), Color::hex(0xf68fc4), Color::hex(0xf9a8d4), Color::hex(0xe0559e), Color::hex(0xf7dfec));
    else if (name == "white") g_theme = MakeTheme(Color::hex(0xe8e8ea), Color::hex(0x0d0d10), Color::hex(0xf7f7f9), Color::hex(0xffffff), Color::hex(0xececee), Color::hex(0xffffff));
    else g_theme = MakeTheme(Color::hex(0x4ade80), Color::hex(0x06180d), Color::hex(0x6ee7a0), Color::hex(0x86efac), Color::hex(0x5dbb3a), Color::hex(0xe8f0dc));
}

const Theme& GetTheme() {
    static bool init = [] {
        SetThemeByName("green");
        return true;
    }();
    (void)init;
    return g_theme;
}
string CurrentThemeName() { return g_themeName; }

// ---------------------------------------------------------------- animator
float Animator::Animate(const string& key, float target, float speed) {
    auto it = values_.find(key);
    if (it == values_.end()) {
        values_[key] = target;
        return target;
    }
    float v = it->second;
    float k = 1.f - std::exp(-speed * dt_);
    v += (target - v) * k;
    if (std::fabs(target - v) < 0.0015f) v = target;
    else active_.insert(key);
    it->second = v;
    return v;
}

float Animator::Get(const string& key) const {
    auto it = values_.find(key);
    return it == values_.end() ? 0.f : it->second;
}

bool Animator::Hover(const string& key, float) { return Get(key) > 0.5f; }
float Animator::PressFade(const string& key, bool pressed) { return Animate(key + "#press", pressed ? 1.f : 0.f, 30.f); }

// ---------------------------------------------------------------- icons
IconAtlas& Icons() {
    static IconAtlas atlas;
    return atlas;
}

bool IconAtlas::Init(Canvas& cv) {
    for (size_t i = 0; i < kIconCount; i++) {
        float px = i < 70 ? 24.f : 24.f;
        bool filled = strcmp(kIcons[i].name, "play") == 0 || strcmp(kIcons[i].name, "square") == 0;
        auto tex = RenderSvgIcon(kIcons[i].svg, px, 2, filled);
        ids[kIcons[i].name] = cv.AddTexture(tex);
    }
    Log("icon atlas: %d icons", (int)ids.size());
    return true;
}

int IconAtlas::Get(const string& name) const {
    auto it = ids.find(name);
    return it == ids.end() ? -1 : it->second;
}

int UIContext::Icon(const string& name, float size, int strokeWidth, bool filled) {
    (void)strokeWidth;
    (void)filled;
    int id = Icons().Get(name);
    return id;
}

// ---------------------------------------------------------------- uictx plumbing
bool UIContext::Hit(const Rect& r) const {
    Rect clip = Clip();
    if (!clip.empty() && !clip.intersects(r)) return false;
    return r.contains(in.mouse);
}

bool UIContext::Clicked(const Rect& r) {
    bool inside = Hit(r);
    if (!inside) return false;
    if (in.pressed) root->active = "click";
    return in.released && root->active == "click" && inside;
}

bool UIContext::Pressed(const Rect& r) const { return in.down && Hit(r); }

void UIContext::PushClip(const Rect& r) {
    Rect cur = Clip();
    Rect next = cur.empty() ? r : cur.intersect(r);
    clipStack_.push_back(next);
    cv->SetClip(next);
}

void UIContext::PopClip() {
    if (!clipStack_.empty()) clipStack_.pop_back();
    cv->SetClip(Clip());
}

void UIContext::Tip(const string& text, const Rect& anchor) {
    if (!text.empty() && Hit(anchor)) root->QueueTip(text, anchor);
}

void UIContext::Text(const string& s, float x, float y, float size, int weight, const Color& c, float maxW) {
    TextStyle st;
    st.size = size;
    st.weight = weight;
    st.color = c;
    Fonts().Draw(cv, s, st, x, y, maxW);
}

float UIContext::TextW(const string& s, float size, int weight) {
    TextStyle st;
    st.size = size;
    st.weight = weight;
    return Fonts().MeasureLine(s, st);
}

void UIContext::TextCenter(const string& s, const Rect& r, float size, int weight, const Color& c) {
    TextStyle st;
    st.size = size;
    st.weight = weight;
    st.color = c;
    Fonts().DrawCenterLine(cv, s, st, r);
}

void UIContext::TextRight(const string& s, const Rect& r, float size, int weight, const Color& c, float padRight) {
    float w = TextW(s, size, weight);
    TextStyle st;
    st.size = size;
    st.weight = weight;
    st.color = c;
    float lh = Fonts().LineHeight(weight, size) * 1.45f;
    Fonts().Draw(cv, s, st, r.r() - padRight - w, r.y + (r.h - lh) * 0.5f);
}

void UIContext::WrapText(const string& s, const Rect& r, float size, int weight, const Color& c, float lh) {
    TextStyle st;
    st.size = size;
    st.weight = weight;
    st.color = c;
    float lineH = Fonts().LineHeight(weight, size) * lh;
    auto lines = Fonts().Measure(s, st, r.w);
    (void)lines;
    // re-layout line by line so we can use lh precisely
    float y = r.y;
    for (auto& para : Split(s, '\n')) {
        string cur;
        for (auto& word : Split(para, ' ')) {
            string cand = cur.empty() ? word : cur + " " + word;
            if (TextW(cand, size, weight) <= r.w || cur.empty()) cur = cand;
            else {
                Text(cur, r.x, y, size, weight, c);
                y += lineH;
                cur = word;
            }
        }
        Text(cur, r.x, y, size, weight, c);
        y += lineH;
        if (y > r.b()) break;
    }
}

float UIContext::ScrollArea(const string& id, const Rect& viewport, float contentHeight, float& offset) {
    float maxOff = std::max(0.f, contentHeight - viewport.h);
    if (in.wheel != 0.f && Hit(viewport) && root->active.empty()) {
        offset -= in.wheel * 54.f;
    }
    offset = clampf(offset, 0.f, maxOff);
    root->scrollOffsets[id] = offset;
    if (maxOff > 0.5f) {
        float trackH = viewport.h - 8.f;
        float thumbH = std::max(36.f, trackH * (viewport.h / contentHeight));
        float t = maxOff > 0 ? offset / maxOff : 0.f;
        float thumbY = viewport.y + 4.f + (trackH - thumbH) * t;
        bool hovering = Hit(Rect(viewport.r() - 16, viewport.y, 16, viewport.h));
        Color c = hovering ? GetTheme().accent.with(0.55f) : Color(1, 1, 1, 0.13f);
        cv->FillRounded(Rect(viewport.r() - 9, thumbY, 7, thumbH), 3.5f, c);
    }
    return offset;
}

float UIContext::ScrollOffset(const string& id) const {
    auto it = root->scrollOffsets.find(id);
    return it == root->scrollOffsets.end() ? 0.f : it->second;
}

// ---------------------------------------------------------------- buttons
static void DrawIconIn(Canvas& cv, int tex, const Rect& box, const Color& tint) {
    if (tex < 0) return;
    cv.Image(tex, box, tint);
}

bool UIContext::Button(const string& id, const Rect& r, const string& label, bool primary, bool small, bool disabled,
                       int icon, const string& tip) {
    const Theme& th = GetTheme();
    bool hover = !disabled && Hit(r);
    float hov = anim.HoverFade(id, hover);
    float press = disabled ? 0.f : anim.PressFade(id, in.down && hover);
    Rect rr = r.inset(press * 1.2f);
    float radius = small ? 9.f : 10.f;
    Color bg = primary ? th.accent.mix(th.accentHover, hov) : th.panel2.mix(th.panel3, hov);
    Color border = primary ? th.accent.mix(th.accentHover, hov) : th.border.mix(Color::hex(0x323845), hov);
    Color fg = primary ? th.accentInk : th.text;
    if (disabled) {
        bg = bg.with(bg.a * 0.55f);
        fg = fg.with(fg.a * 0.55f);
        border = border.with(border.a * 0.55f);
    }
    if (primary && hover && !disabled) {
        cv->Shadow(rr, radius, 22.f, th.accent.with(0.35f * hov), 6.f);
    }
    cv->FillRounded(rr, radius, bg);
    cv->StrokeRounded(rr.inset(0.5f), radius - 0.5f, 1.f, border);
    TextStyle st;
    st.size = small ? 13.f : 14.f;
    st.weight = primary ? 600 : 500;
    st.color = fg;
    float tw = Fonts().MeasureLine(label, st);
    float iconW = icon >= 0 ? (small ? 13.f : 15.f) + (label.empty() ? 0.f : 8.f) : 0.f;
    float total = tw + iconW;
    float cx = rr.x + (rr.w - total) * 0.5f;
    if (icon >= 0) {
        float isz = small ? 13.f : 15.f;
        DrawIconIn(cv, icon, Rect(cx, rr.y + (rr.h - isz) * 0.5f, isz, isz), fg);
        cx += isz + (label.empty() ? 0.f : 8.f);
    }
    if (!label.empty()) {
        float lh = Fonts().LineHeight(st.weight, st.size) * 1.45f;
        Fonts().Draw(cv, label, st, cx, rr.y + (rr.h - lh) * 0.5f);
    }
    if (hover && !tip.empty()) root->QueueTip(tip, r);
    if (disabled) return false;
    if (hover && in.pressed) root->active = id;
    bool click = hover && in.released && root->active == id;
    if (click) root->active.clear();
    return click;
}

bool UIContext::GhostButton(const string& id, const Rect& r, const string& label, int icon) {
    const Theme& th = GetTheme();
    bool hover = Hit(r);
    float hov = anim.HoverFade(id, hover);
    if (hov > 0.01f) cv->FillRounded(r, 10.f, th.panel2.with(hov));
    TextStyle st;
    st.size = 14.f;
    st.weight = 500;
    st.color = th.dim.mix(th.text, hov);
    float tw = Fonts().MeasureLine(label, st);
    float iconW = icon >= 0 ? 15.f + (label.empty() ? 0.f : 8.f) : 0.f;
    float cx = r.x + (r.w - tw - iconW) * 0.5f;
    if (icon >= 0) {
        DrawIconIn(cv, icon, Rect(cx, r.y + (r.h - 15.f) * 0.5f, 15.f, 15.f), st.color);
        cx += iconW;
    }
    if (!label.empty()) {
        float lh = Fonts().LineHeight(14.f, 14.f) * 1.45f;
        Fonts().Draw(cv, label, st, cx, r.y + (r.h - lh) * 0.5f);
    }
    if (hover && in.pressed) root->active = id;
    bool click = hover && in.released && root->active == id;
    if (click) root->active.clear();
    return click;
}

bool UIContext::IconButton(const string& id, const Rect& r, int icon, float iconSize, const string& tip) {
    const Theme& th = GetTheme();
    bool hover = Hit(r);
    float hov = anim.HoverFade(id, hover);
    if (hov > 0.01f) cv->FillRounded(r, 9.f, th.panel2.with(0.9f * hov));
    Color fg = th.dim.mix(th.text, hov);
    if (icon >= 0) DrawIconIn(cv, icon, Rect(r.x + (r.w - iconSize) * 0.5f, r.y + (r.h - iconSize) * 0.5f, iconSize, iconSize), fg);
    if (hover && !tip.empty()) root->QueueTip(tip, r);
    if (hover && in.pressed) root->active = id;
    bool click = hover && in.released && root->active == id;
    if (click) root->active.clear();
    return click;
}

bool UIContext::NavItem(const string& id, const Rect& r, int icon, const string& label, bool active, bool collapsed) {
    const Theme& th = GetTheme();
    bool hover = Hit(r);
    float hov = anim.HoverFade(id, hover);
    float sel = anim.Animate(id + "#sel", active ? 1.f : 0.f, 14.f);
    Color bg = Color(0, 0, 0, 0);
    if (hov > 0.01f) bg = th.panel2.with(hov * (1.f - sel));
    if (sel > 0.01f) bg = th.accentSoft.mix(bg, 1.f - sel);
    if (bg.a > 0.004f) cv->FillRounded(r, 10.f, bg);
    // active indicator bar
    if (sel > 0.01f) {
        float bh = 18.f * sel;
        cv->FillRounded(Rect(r.x, r.y + r.h * 0.5f - bh * 0.5f, 3.f, bh), 1.5f, th.accent);
    }
    Color fg = th.dim.mix(th.text, hov);
    fg = fg.mix(th.accent, sel);
    float iconSize = 22.f;
    float ix = collapsed ? r.x + (r.w - iconSize) * 0.5f : r.x + 13.f;
    if (icon >= 0) DrawIconIn(cv, icon, Rect(ix, r.y + (r.h - iconSize) * 0.5f, iconSize, iconSize), fg);
    if (!collapsed) {
        TextStyle st;
        st.size = 14.f;
        st.weight = 500;
        st.color = fg;
        float lh = Fonts().LineHeight(14.f, 14.f) * 1.45f;
        Fonts().Draw(cv, label, st, r.x + 47.f, r.y + (r.h - lh) * 0.5f, std::max(0.f, r.w - 55.f));
    }
    if (collapsed && hover) root->QueueTip(label, r);
    if (hover && in.pressed) root->active = id;
    bool click = hover && in.released && root->active == id;
    if (click) root->active.clear();
    return click;
}

bool UIContext::Switch(const string& id, const Rect& r, bool& value) {
    const Theme& th = GetTheme();
    bool hover = Hit(r);
    float hov = anim.HoverFade(id, hover);
    float on = anim.Animate(id + "#on", value ? 1.f : 0.f, 20.f);
    Color bg = th.panel3.mix(th.accentSoft, on);
    Color border = th.border.mix(th.accent.with(0.5f), on);
    cv->FillRounded(r, r.h * 0.5f, bg);
    cv->StrokeRounded(r.inset(0.5f), r.h * 0.5f - 0.5f, 1.f, hover ? border.lighten(hov * 0.08f) : border);
    float knob = r.h - 8.f;
    float kx = r.x + 4.f + (r.w - knob - 8.f) * on;
    cv->FillCircle({kx + knob * 0.5f, r.y + r.h * 0.5f}, knob * 0.5f, th.dim.mix(th.accent, on));
    if (hover && in.released && root->active == id) {
        value = !value;
        root->active.clear();
        return true;
    }
    if (hover && in.pressed) root->active = id;
    return false;
}

bool UIContext::Slider(const string& id, const Rect& r, float& value, float minV, float maxV, int decimals,
                       const string& suffix) {
    const Theme& th = GetTheme();
    bool hover = Hit(r.inset(-4));
    bool changed = false;
    if (hover && in.pressed) root->active = id;
    if (root->active == id) {
        if (in.down) {
            float t = clamp01((in.mouse.x - r.x) / std::max(1.f, r.w));
            float nv = minV + (maxV - minV) * t;
            if (decimals == 0) nv = std::round(nv);
            if (nv != value) {
                value = nv;
                changed = true;
            }
        } else {
            root->active.clear();
        }
    }
    float t = clamp01((value - minV) / std::max(0.0001f, maxV - minV));
    Rect track(r.x, r.y + r.h * 0.5f - 3.f, r.w, 6.f);
    cv->FillRounded(track, 3.f, th.panel3);
    if (t > 0.f) cv->FillRounded(Rect(track.x, track.y, track.w * t, track.h), 3.f, th.accent);
    float knob = 16.f;
    Vec2 kc(track.x + track.w * t, track.y + track.h * 0.5f);
    cv->FillCircle(kc, knob * 0.5f + 2.f, th.panel);
    cv->FillCircle(kc, knob * 0.5f, hover || root->active == id ? th.accentLight : th.text);
    return changed;
}

bool UIContext::Segmented(const string& id, const Rect& r, const std::vector<string>& options, int& index) {
    const Theme& th = GetTheme();
    cv->FillRounded(r, 11.f, th.panel2);
    cv->StrokeRounded(r.inset(0.5f), 10.5f, 1.f, th.border);
    float segW = (r.w - 6.f) / std::max<size_t>(1, options.size());
    float pill = anim.Animate(id + "#idx", (float)index, 18.f);
    cv->FillRounded(Rect(r.x + 3.f + pill * segW, r.y + 3.f, segW, r.h - 6.f), 8.f, th.accent);
    bool changed = false;
    for (size_t i = 0; i < options.size(); i++) {
        Rect seg(r.x + 3.f + segW * (float)i, r.y + 3.f, segW, r.h - 6.f);
        bool on = (int)i == index;
        Color fg = on ? th.accentInk : (Hit(seg) ? th.text : th.dim);
        TextStyle st;
        st.size = 13.5f;
        st.weight = 550;
        st.color = fg;
        float lh = Fonts().LineHeight(550, 13.5f) * 1.45f;
        float tw = Fonts().MeasureLine(options[i], st);
        Fonts().Draw(cv, options[i], st, seg.x + (seg.w - tw) * 0.5f, seg.y + (seg.h - lh) * 0.5f);
        if (Hit(seg) && in.released && root->active == id + std::to_string(i)) {
            if (index != (int)i) {
                index = (int)i;
                changed = true;
            }
        }
        if (Hit(seg) && in.pressed) root->active = id + std::to_string(i);
    }
    return changed;
}

bool UIContext::ThemeSwatch(const string& id, const Rect& r, const Color& col, bool on) {
    const Theme& th = GetTheme();
    bool hover = Hit(r);
    float hov = anim.HoverFade(id, hover);
    float scale = 1.f + hov * 0.1f;
    Rect rr(r.x + r.w * (1 - scale) * 0.5f, r.y + r.h * (1 - scale) * 0.5f, r.w * scale, r.h * scale);
    cv->FillCircle(rr.center(), rr.w * 0.5f, col);
    cv->FillCircle(rr.center(), rr.w * 0.5f - 2.f, col);
    if (on) {
        DrawCheck(cv, Rect(rr.x + rr.w * 0.28f, rr.y + rr.h * 0.36f, rr.w * 0.44f, rr.h * 0.34f), Color(1, 1, 1, 0.95f), 2.2f);
    }
    if (hover && in.pressed) root->active = id;
    bool click = hover && in.released && root->active == id;
    if (click) root->active.clear();
    return click;
}

// ---------------------------------------------------------------- text field
void UIRoot::SetFocus(const string& key) { focus = key; }
bool UIRoot::HasFocus(const string& key) const { return focus == key; }
void UIRoot::Blur() { focus.clear(); }

bool UIContext::TextField(const string& key, const Rect& r, string& value, const string& placeholder, bool password,
                           float fontSize) {
    const Theme& th = GetTheme();
    bool hover = Hit(r);
    bool focused = root->HasFocus(key);
    if (in.pressed && hover) root->focus = key;
    else if (in.pressed && !hover && focused && root->active.empty()) {
        // click outside keeps focus for single-field forms unless a modal closes
    }
    Color border = th.border.mix(Color::hex(0x323845), hover ? 1.f : 0.f);
    if (focused) border = th.accent;
    cv->FillRounded(r, 10.f, th.panel2);
    cv->StrokeRounded(r.inset(0.5f), 9.5f, 1.f, border);
    if (focused) {
        cv->StrokeRounded(r.inset(-2.5f), 12.5f, 3.f, th.accentSoft);
    }
    bool changed = false;
    if (focused) {
        for (uint32_t cp : in.textInput) {
            if (cp < 32) continue;
            if (password) value.push_back('*');
            else {
                char buf[8];
                int n = 0;
                if (cp < 0x80) buf[n++] = (char)cp;
                else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
                else { buf[n++] = (char)(0xF0 | (cp >> 18)); buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
                value.append(buf, n);
            }
            changed = true;
        }
        if (in.keyBackspace && !value.empty()) {
            value = Utf8Sub(value, 0, Utf8Len(value) - 1);
            changed = true;
        }
        if (in.keyEscape) root->focus.clear();
    }
    TextStyle st;
    st.size = fontSize;
    st.weight = 400;
    st.color = value.empty() && !focused ? th.faint : th.text;
    string shown = value.empty() && !focused ? placeholder : value;
    float lh = Fonts().LineHeight(400, fontSize) * 1.45f;
    float ty = r.y + (r.h - lh) * 0.5f;
    PushClip(r.inset(10));
    bool emptyNow = value.empty();
    float tw = Fonts().MeasureLine(emptyNow ? "" : value, st);
    if (emptyNow && focused) {
        st.color = th.text;
        Fonts().Draw(cv, placeholder, st, r.x + 12.f, ty, r.w - 24.f);
    } else if (emptyNow) {
        st.color = th.faint;
        Fonts().Draw(cv, placeholder, st, r.x + 12.f, ty, r.w - 24.f);
    } else {
        st.color = th.text;
        Fonts().Draw(cv, value, st, r.x + 12.f, ty, r.w - 20.f);
    }
    if (focused && std::fmod((float)(NowSeconds() - root->caretBlink), 1.f) < 0.55f) {
        float cx = r.x + 12.f + std::min(tw, r.w - 24.f) + 1.f;
        cv->FillRect(Rect(cx, r.y + 10.f, 1.5f, r.h - 20.f), th.accent);
    }
    PopClip();
    if (hover) root->hot = key;
    return changed;
}

bool UIContext::TextArea(const string& key, const Rect& r, string& value, const string& placeholder) {
    return TextField(key, r, value, placeholder);
}

// ---------------------------------------------------------------- overlays
UIRoot::UIRoot() = default;


void UIRoot::BeginFrame(Canvas& cv, InputState& in, float dt) {
    anim.BeginFrame(dt);
    ctx.cv = cv;
    ctx.in = in;
    ctx.anim = anim;
    ctx.dt = dt;
    ctx.root = this;
    ctx.SetBaseClip(cv.Clip());
    tooltipText.clear();
    EscapePressed = in.keyEscape;
    if (in.pressed) {
        if (dropdown.open && !dropdown.panel.contains(in.mouse) && !dropdown.anchor.contains(in.mouse)) CloseDropdown();
    }
}

void UIRoot::EndFrame() {
    ctx.clipStack_.clear();
}

void UIRoot::FlushDeferred() {
    // overlays recorded while a page was clipped; drawing them now keeps the
    // dropdown/tooltip card from being cut off by the page viewport.
    if (deferred) {
        auto fn = deferred;
        ClearDeferred();
        fn();
    }
}


void UIRoot::QueueTip(const string& text, const Rect& anchor) {
    if (tooltipText.empty()) {
        tooltipText = text;
        tooltipAnchor = anchor;
    }
}

void UIRoot::Toast_(const string& message, bool error, bool success, const string& title) {
    Toast t;
    t.id = Format("toast%d", (int)toasts.size());
    t.title = title;
    t.message = message;
    t.error = error;
    t.success = success;
    t.born = NowSeconds();
    t.leavingAt = 0.0;
    toasts.push_back(t);
    if (toasts.size() > 5) toasts.erase(toasts.begin());
}

void UIRoot::OpenModal(const string& id, const string& title, const string& subtitle, bool wide) {
    modal.id = id;
    modal.title = title;
    modal.subtitle = subtitle;
    modal.open = true;
    modal.wide = wide;
    modal.openedAt = NowSeconds();
    focus.clear();
}

void UIRoot::CloseModal() {
    modal.open = false;
    modal.id.clear();
    focus.clear();
}

Rect UIRoot::ModalPanel(float w, float h, bool wide) {
    float t = (float)clamp01((NowSeconds() - modal.openedAt) / 0.28);
    float e = EaseCss(t);
    (void)wide;
    Rect bounds(0, 0, kScreenW, kScreenH);
    Rect panel(0, 0, w, h);
    panel.x = bounds.center().x - w * 0.5f;
    panel.y = bounds.center().y - h * 0.5f + (1.f - e) * 14.f;
    modal.lastPanel = panel;
    return panel;
}

void UIRoot::DrawModalChrome(const Rect& panel) {
    const Theme& th = GetTheme();
    float t = (float)clamp01((NowSeconds() - modal.openedAt) / 0.28);
    float e = EaseCss(t);
    Rect full = ctx.cv->Clip();
    ctx.cv->FillRect(full, Color(0, 0, 0, 0.62f * e));
    ctx.cv->Shadow(panel, 18.f, 60.f, Color(0, 0, 0, 0.55f * e), 24.f);
    ctx.cv->FillRounded(panel, 18.f, th.panel);
    ctx.cv->StrokeRounded(panel.inset(0.5f), 17.5f, 1.f, th.border);
}

bool UIRoot::Select(const string& key, const Rect& r, const std::vector<string>& items, int& selected, int icon) {
    const Theme& th = GetTheme();
    bool hover = r.contains(ctx.in.mouse);
    float hov = anim.HoverFade("sel:" + key, hover);
    ctx.cv->FillRounded(r, 10.f, th.panel2.mix(th.panel3, hov));
    ctx.cv->StrokeRounded(r.inset(0.5f), 9.5f, 1.f, th.border.mix(Color::hex(0x323845), hov));
    if (icon >= 0) ctx.cv->Image(icon, Rect(r.x + 12.f, r.y + (r.h - 18.f) * 0.5f, 18.f, 18.f), th.dim);
    TextStyle st;
    st.size = 14.f;
    st.weight = 500;
    st.color = th.text;
    if (selected >= 0 && selected < (int)items.size()) {
        float lh = Fonts().LineHeight(500, 14.f) * 1.45f;
        Fonts().Draw(ctx.cv, items[(size_t)selected], st, r.x + 12.f + (icon >= 0 ? 26.f : 0.f), r.y + (r.h - lh) * 0.5f,
                     r.w - 50.f);
    }
    DrawChevron(ctx.cv, Rect(r.r() - 26.f, r.y + r.h * 0.5f - 5.f, 10.f, 10.f), th.dim, 2.f, 0);
    if (hover && ctx.in.pressed) {
        dropdown.open = true;
        dropdown.key = key;
        dropdown.anchor = r;
        dropdown.items = items;
        dropdown.selected = selected;
        dropdown.openedAt = NowSeconds();
        dropdown.hoverIndex = -1;
        dropdown.panel = Rect();
    }
    bool changed = false;
    if (dropdown.open && dropdown.key == key && !items.empty()) {
        float itemH = 36.f;
        float ph = std::min(320.f, itemH * (float)items.size() + 12.f);
        Rect pr(r.x, r.b() + 6.f, std::max(r.w, 180.f), ph);
        if (pr.b() > kScreenH - 10.f) pr.y = std::max(10.f, r.y - ph - 6.f);
        dropdown.panel = pr;
        // the list floats over the page, so record it and paint after the page clip is gone
        int* selPtr = &selected;
        std::vector<string> itemCopy = items;
        bool* changedPtr = &changed;
        deferred = [this, pr, itemCopy, selPtr, changedPtr, itemH, th]() {
            int sel = *selPtr;
            ctx.cv->Shadow(pr, 12.f, 34.f, Color(0, 0, 0, 0.5f), 10.f);
            ctx.cv->FillRounded(pr, 12.f, th.panel);
            ctx.cv->StrokeRounded(pr.inset(0.5f), 11.5f, 1.f, th.border);
            ctx.PushClip(pr.inset(6));
            for (size_t i = 0; i < itemCopy.size(); i++) {
                Rect ir(pr.x + 6.f, pr.y + 6.f + itemH * (float)i, pr.w - 12.f, itemH);
                bool ih = ir.contains(ctx.in.mouse) && pr.contains(ctx.in.mouse);
                Color bg = ih ? th.panel2 : Color(0, 0, 0, 0);
                if (bg.a > 0.f) ctx.cv->FillRounded(ir, 8.f, bg);
                TextStyle ist;
                ist.size = 14.f;
                ist.weight = (int)i == sel ? 600 : 400;
                ist.color = (int)i == sel ? th.accent : (ih ? th.text : th.text.with(0.85f));
                float lh = Fonts().LineHeight(ist.weight, 14.f) * 1.45f;
                Fonts().Draw(ctx.cv, itemCopy[i], ist, ir.x + 10.f, ir.y + (ir.h - lh) * 0.5f, ir.w - 40.f);
                if ((int)i == sel) DrawCheck(ctx.cv, Rect(ir.r() - 24.f, ir.y + ir.h * 0.5f - 5.f, 11.f, 10.f), th.accent, 2.f);
                if (ih && ctx.in.pressed) {
                    *selPtr = (int)i;
                    *changedPtr = true;
                }
            }
            ctx.PopClip();
        };
        if (ctx.in.released && !pr.contains(ctx.in.mouse) && !r.contains(ctx.in.mouse)) CloseDropdown();
    }
    if (dropdown.open && dropdown.key == key && changed) CloseDropdown();
    return changed;
}

void UIRoot::CloseDropdown() {
    dropdown.open = false;
    dropdown.items.clear();
    dropdown.panel = Rect();
}

}  // namespace crit

namespace crit {

// ---------------------------------------------------------------- small vector drawings
void DrawBrandMark(Canvas& cv, const Rect& r, const Color& a, const Color& b, float rotation, float alpha) {
    // 9x9 pixel cross, drawn on a 3x3 unit grid
    float u = r.w / 3.f;
    Vec2 c = r.center();
    auto cell = [&](float gx, float gy, const Color& col) {
        float x = r.x + gx * u, y = r.y + gy * u;
        float px = x + u * 0.5f - c.x, py = y + u * 0.5f - c.y;
        float cs = std::cos(rotation), sn = std::sin(rotation);
        Vec2 rc(c.x + px * cs - py * sn, c.y + px * sn + py * cs);
        cv.FillRect(Rect(rc.x - u * 0.5f, rc.y - u * 0.5f, u, u), col.with(col.a * alpha));
    };
    (void)b;
    // plus shape: row 1 all, col 1 all, center block
    cell(0, 1, a);
    cell(1, 0, a);
    cell(2, 1, a);
    cell(1, 2, a);
    cell(1, 1, a);
    // highlight pixels
    Color hl = Color(1, 1, 1, 0.35f * alpha);
    cell(1, 1, hl);
    (void)b;
}

void DrawRing(Canvas& cv, Vec2 center, float radius, float thickness, const Color& c, float arc, float rot) {
    (void)arc;
    (void)rot;
    cv.FillCircle(center, radius, c);
    (void)thickness;
}

void DrawDot(Canvas& cv, Vec2 c, float radius, const Color& col, bool glow, float t) {
    if (glow) {
        float pulse = 0.5f + 0.5f * std::sin(t * 3.1416f);
        cv.FillCircle(c, radius + 3.f + pulse * 1.f, col.with(0.12f));
    }
    cv.FillCircle(c, radius, col);
}

void DrawChevron(Canvas& cv, const Rect& r, const Color& c, float thickness, int dir) {
    float w = r.w, h = r.h;
    Vec2 p0, p1, p2;
    switch (dir) {
        case 0: p0 = {r.x, r.y + h * 0.2f}; p1 = {r.x + w * 0.5f, r.y + h * 0.85f}; p2 = {r.x + w, r.y + h * 0.2f}; break;
        case 1: p0 = {r.x + w * 0.8f, r.y}; p1 = {r.x + w * 0.15f, r.y + h * 0.5f}; p2 = {r.x + w * 0.8f, r.y + h}; break;
        case 2: p0 = {r.x + w * 0.2f, r.y}; p1 = {r.x + w * 0.85f, r.y + h * 0.5f}; p2 = {r.x + w * 0.2f, r.y + h}; break;
        default: p0 = {r.x, r.y + h * 0.8f}; p1 = {r.x + w * 0.5f, r.y + h * 0.15f}; p2 = {r.x + w, r.y + h * 0.8f}; break;
    }
    cv.Line(p0, p1, thickness, c);
    cv.Line(p1, p2, thickness, c);
}

void DrawClose(Canvas& cv, const Rect& r, const Color& c, float thickness) {
    cv.Line({r.x, r.y}, {r.r(), r.b()}, thickness, c);
    cv.Line({r.r(), r.y}, {r.x, r.b()}, thickness, c);
}

void DrawPlus(Canvas& cv, const Rect& r, const Color& c, float thickness) {
    cv.Line({r.x, r.y + r.h * 0.5f}, {r.r(), r.y + r.h * 0.5f}, thickness, c);
    cv.Line({r.x + r.w * 0.5f, r.y}, {r.x + r.w * 0.5f, r.b()}, thickness, c);
}

void DrawCheck(Canvas& cv, const Rect& r, const Color& c, float thickness) {
    Vec2 a(r.x, r.y + r.h * 0.55f);
    Vec2 b(r.x + r.w * 0.4f, r.b());
    Vec2 d(r.r(), r.y);
    cv.Line(a, b, thickness, c);
    cv.Line(b, d, thickness, c);
}

void DrawSpinner(Canvas& cv, const Rect& r, const Color& c, float t) {
    // arc approximated by circle of dots fading around
    Vec2 center = r.center();
    float radius = std::min(r.w, r.h) * 0.5f - 1.5f;
    const int n = 8;
    for (int i = 0; i < n; i++) {
        float a = t * 6.28318f + (float)i / n * 6.28318f;
        float alpha = 0.15f + 0.85f * ((float)i / n);
        Vec2 p(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius);
        cv.FillCircle(p, std::max(1.f, radius * 0.18f), c.with(c.a * alpha));
    }
}

void DrawFace(Canvas& cv, int skinTex, const Rect& r, float radius) {
    const Theme& th = GetTheme();
    cv.FillRounded(r, radius, th.panel3);
    if (skinTex >= 0) {
        // the head occupies the top-left 8x8 of a 64x64 skin, with hat layer on 40,8
        cv.Image(skinTex, r, Color(1, 1, 1, 1), 8.f / 64.f, 8.f / 64.f, 16.f / 64.f, 16.f / 64.f);
        cv.Image(skinTex, r, Color(1, 1, 1, 1), 40.f / 64.f, 8.f / 64.f, 48.f / 64.f, 16.f / 64.f);
    } else {
        int iconId = Icons().Get("user");
        float sz = r.w * 0.55f;
        cv.Image(iconId, Rect(r.center().x - sz * 0.5f, r.center().y - sz * 0.5f, sz, sz), th.faint);
    }
}

}  // namespace crit
