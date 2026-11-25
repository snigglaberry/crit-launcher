// Crit Launcher Native — retained-state UI framework (immediate mode layout).
#pragma once

#include <map>
#include <memory>
#include <set>

#include "render/font.h"
#include "render/gfx.h"

namespace crit {

// current DPI-adjusted window size, set once per frame by the app
extern float kScreenW;
extern float kScreenH;

// ---------------------------------------------------------------- theme
struct Theme {
    Color bg, panel, panel2, panel3, border, borderSoft, text, dim, faint;
    Color accent, accentInk, accentHover, accentLight, brand, brandLight, danger, warn;
    Color accentSoft;   // rgba(accent-rgb, 0.12)
    Color dangerSoft;
    Color run;          // accent
    bool light = false;
    float accentRgb[3] = {74, 222, 128};
};
const Theme& GetTheme();
void SetThemeByName(const string& name);  // green, blue, purple, red, orange, pink, white, custom
string CurrentThemeName();

// ---------------------------------------------------------------- input
struct InputState {
    Vec2 mouse;
    Vec2 prevMouse;
    bool down = false;
    bool pressed = false;    // went down this frame
    bool released = false;   // went up this frame
    bool doubleClick = false;
    float wheel = 0.f;
    std::vector<uint32_t> textInput;  // chars typed this frame
    bool keyBackspace = false;
    bool keyEnter = false;
    bool keyEscape = false;
    bool keyTab = false;
    bool ctrlDown = false;
    bool shiftDown = false;
    Vec2 delta() const { return mouse - prevMouse; }
    bool moved() const { return mouse.x != prevMouse.x || mouse.y != prevMouse.y; }
};

// ---------------------------------------------------------------- animation
class Animator {
public:
    void BeginFrame(float dt) {
        dt_ = dt;
        active_.clear();
    }
    // exponential approach toward target; returns eased value
    float Animate(const string& key, float target, float speed = 16.f);
    void Set(const string& key, float value) { values_[key] = value; }
    float Get(const string& key) const;
    bool Hover(const string& key, float dt);
    float HoverFade(const string& key, bool hovered, float speed = 18.f) {
        return Animate(key + "#hover", hovered ? 1.f : 0.f, speed);
    }
    float PressFade(const string& key, bool pressed);
    bool AnyActive() const { return !active_.empty(); }
    void Clear() { values_.clear(); }

private:
    std::map<string, float> values_;
    std::set<string> active_;
    float dt_ = 0.016f;
};

// ---------------------------------------------------------------- ui context
struct UIRoot;
struct App;
class UIContext;


struct TooltipRequest {
    string text;
    Rect anchor;
};

// points at a Canvas owned elsewhere; assignment rebinds instead of copying
struct CanvasView {
    Canvas* target = nullptr;
    CanvasView() = default;
    CanvasView(Canvas* c) : target(c) {}
    CanvasView& operator=(Canvas& c) {
        target = &c;
        return *this;
    }
    CanvasView& operator=(Canvas* c) {
        target = c;
        return *this;
    }
    Canvas* operator->() const { return target; }
    Canvas& operator*() const { return *target; }
    operator Canvas&() const { return *target; }
};

// binds a reference-like member that can be re-pointed every frame
template <class T>
struct RefProxy : public T {
    RefProxy() = default;
    using T::operator=;
    RefProxy(const T& other) : T(other) {}
};

class UIContext {
public:
    UIContext() = default;
    // the canvas is owned by App::render; this proxy must stay a *reference*
    // wrapper (a plain copy would silently detach every context-drawn widget)
    CanvasView cv;
    RefProxy<InputState> in;
    UIRoot* root = nullptr;
    RefProxy<Animator> anim;
    float dt = 0.016f;

    // -------- primitives
    bool Hit(const Rect& r) const;   // mouse inside (clip aware set by caller)
    bool Hovered(const Rect& r) const { return Hit(r); }
    bool Clicked(const Rect& r);     // completes a click (press+release inside)
    bool Pressed(const Rect& r) const;

    void PushClip(const Rect& r);
    void PopClip();
    Rect Clip() const { return clipStack_.empty() ? baseClip_ : clipStack_.back(); }
    void SetBaseClip(const Rect& r) { baseClip_ = r; }

    // -------- widgets (return true on activation)
    bool Button(const string& id, const Rect& r, const string& label, bool primary = false, bool small = false,
                bool disabled = false, int icon = -1, const string& tip = "");
    bool GhostButton(const string& id, const Rect& r, const string& label, int icon = -1);
    bool IconButton(const string& id, const Rect& r, int icon, float iconSize = 18, const string& tip = "");
    bool NavItem(const string& id, const Rect& r, int icon, const string& label, bool active, bool collapsed);
    bool Switch(const string& id, const Rect& r, bool& value);
    bool Slider(const string& id, const Rect& r, float& value, float minV, float maxV, int decimals = 0,
                const string& suffix = "");
    bool Segmented(const string& id, const Rect& r, const std::vector<string>& options, int& index);
    bool ThemeSwatch(const string& id, const Rect& r, const Color& col, bool on);
    void Tip(const string& text, const Rect& anchor);  // queue tooltip on hover

    // -------- text helpers
    void Text(const string& s, float x, float y, float size, int weight, const Color& c, float maxW = 0.f);
    float TextW(const string& s, float size, int weight);
    void TextCenter(const string& s, const Rect& r, float size, int weight, const Color& c);
    void TextRight(const string& s, const Rect& r, float size, int weight, const Color& c, float padRight = 0.f);
    void WrapText(const string& s, const Rect& r, float size, int weight, const Color& c, float lh = 1.45f);

    // -------- icons
    int Icon(const string& name, float size, int strokeWidth = 2, bool filled = false);  // texture id

    // -------- text input
    bool TextField(const string& key, const Rect& r, string& value, const string& placeholder, bool password = false,
                   float fontSize = 14.f);
    bool TextArea(const string& key, const Rect& r, string& value, const string& placeholder);

    // -------- scroll
    // Handles wheel + draws scrollbar. Returns effective offset for the list.
    float ScrollArea(const string& id, const Rect& viewport, float contentHeight, float& offset);
    float ScrollOffset(const string& id) const;

    App* app = nullptr;

    Canvas* ptr() const { return cv.target; }

private:
    Rect baseClip_;
    std::vector<Rect> clipStack_;
    std::vector<TooltipRequest> tips_;
    friend struct UIRoot;
};

// ---------------------------------------------------------------- overlays
struct Toast {
    string id;
    string title;
    string message;
    bool error = false;
    bool success = false;
    double born = 0.0;
    double leavingAt = 0.0;
};

struct ModalState {
    string id;
    string title;
    string subtitle;
    bool open = false;
    bool wide = false;
    double openedAt = 0.0;
    Rect lastPanel;  // last drawn panel rect, for hit testing
};

struct DropdownState {
    string key;
    Rect anchor;
    std::vector<string> items;
    int selected = -1;
    bool open = false;
    double openedAt = 0.0;
    int hoverIndex = -1;
    Rect panel;
};

class App;
struct UIRoot {
    UIRoot();
    App* app = nullptr;
    // per-frame
    void BeginFrame(Canvas& cv, InputState& in, float dt);
    void EndFrame();

    // state for widgets
    std::map<string, double> hoverStart;
    string hot;      // currently hovered widget id
    string active;   // pressed widget id
    std::map<string, float> scrollOffsets;

    Animator anim;
    UIContext ctx;

    // tooltip
    void QueueTip(const string& text, const Rect& anchor);
    string tooltipText;
    Rect tooltipAnchor;
    double tooltipSince = 0.0;

    // toasts
    std::vector<Toast> toasts;
    void Toast_(const string& message, bool error = false, bool success = false, const string& title = "");

    // modal
    ModalState modal;
    void OpenModal(const string& id, const string& title, const string& subtitle = "", bool wide = false);
    void CloseModal();
    bool ModalOpen() const { return modal.open; }
    // draws scrim + returns the panel rect (animated); callers draw contents
    Rect ModalPanel(float w, float h, bool wide = false);
    void DrawModalChrome(const Rect& panel);

    // dropdown
    DropdownState dropdown;
    bool Select(const string& key, const Rect& r, const std::vector<string>& items, int& selected, int icon = -1);
    void CloseDropdown();

    // input focus
    void SetFocus(const string& key);
    bool HasFocus(const string& key) const;
    void Blur();
    string focus;
    double caretBlink = 0.0;
    bool TextField(const string& key, const Rect& r, string& value, const string& placeholder, bool password = false,
                   float fontSize = 14.f);
    bool TextArea(const string& key, const Rect& r, string& value, const string& placeholder);
    // deferred overlays (dropdown menus) sit above the page and modals
    std::function<void()> deferred;
    void ClearDeferred() { deferred = nullptr; }
    void FlushDeferred();

    // global
    void PushClip(const Rect& r) { ctx.PushClip(r); }
    void PopClip() { ctx.PopClip(); }

    bool AnyOverlay() const { return modal.open || dropdown.open; }
    bool EscapePressed = false;
};

// ---------------------------------------------------------------- components shared across pages
struct IconAtlas {
    std::map<string, int> ids;  // texture ids by icon name
    bool Init(Canvas& cv);
    int Get(const string& name) const;
    void Preload(Canvas& cv, int px);
};
IconAtlas& Icons();

// draws the animated 3x3 brand mark (the pixel cross)
void DrawBrandMark(Canvas& cv, const Rect& r, const Color& a, const Color& b, float rotation = 0.f, float alpha = 1.f);
void DrawSpinner(Canvas& cv, const Rect& r, const Color& c, float t);
void DrawDot(Canvas& cv, Vec2 c, float radius, const Color& col, bool glow, float t);
void DrawCheck(Canvas& cv, const Rect& r, const Color& c, float thickness = 2.f);
void DrawChevron(Canvas& cv, const Rect& r, const Color& c, float thickness, int dir);  // 0=down 1=left 2=right 3=up
void DrawClose(Canvas& cv, const Rect& r, const Color& c, float thickness);
void DrawPlus(Canvas& cv, const Rect& r, const Color& c, float thickness);
void DrawRing(Canvas& cv, Vec2 center, float radius, float thickness, const Color& c, float arc = 1.f, float rot = 0.f);

// shared page scaffolding
struct PageScroll {
    float offset = 0.f;
};

// pixel-perfect avatar face (skin head) with fallback
void DrawFace(Canvas& cv, int skinTex, const Rect& r, float radius);

}  // namespace crit
