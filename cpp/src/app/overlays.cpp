// Crit Launcher Native — overlays: modals, toasts, dropdowns, tooltips.
#include "app/app.h"

namespace crit {

void App::DrawOverlays() {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;

    // ---- modals (registered ones first)
    DrawCreateInstanceModal();
    DrawAccountModal();
    if (ui.modal.open && ui.modal.id == "instmenu") {
        Rect panel = ui.ModalPanel(420.f, 330.f, true);
        ui.DrawModalChrome(panel);
        Rect close(panel.r() - 44.f, panel.y + 16.f, 28.f, 28.f);
        if (ctx.IconButton("instmclose", close, IconTex("x"), 16.f)) ui.CloseModal();
        string name = pendingFields["instmenu"];
        ctx.Text(ui.modal.title, panel.x + 24.f, panel.y + 22.f, 18.f, 680, th.text);
        ctx.Text("Instance actions", panel.x + 24.f, panel.y + 48.f, 13.f, 400, th.dim);
        const char* actions[] = {"Open folder", "Set as active instance", "Open console", "Delete instance"};
        const char* icons[] = {"folder-open", "check", "terminal", "trash"};
        for (int i = 0; i < 4; i++) {
            Rect row(panel.x + 16.f, panel.y + 82.f + (float)i * 48.f, panel.w - 32.f, 42.f);
            bool hover = ctx.Hit(row);
            if (hover) cv.FillRounded(row, 10.f, i == 3 ? th.dangerSoft : th.panel2);
            cv.Image(IconTex(icons[i]), Rect(row.x + 12.f, row.center().y - 9.f, 18.f, 18.f),
                     i == 3 ? th.danger : th.dim);
            ctx.Text(actions[i], row.x + 42.f, row.center().y - Fonts().LineHeight(500, 14.f) * 0.72f, 14.f, 500,
                     i == 3 ? th.danger : th.text);
            if (hover && input.released) {
                if (i == 0) OpenFolder(DataPath("instances/" + name));
                else if (i == 1) {
                    SetActiveInstance(name);
                    ShowToast(name + " is now the active instance.");
                } else if (i == 2) {
                    Navigate(Page::Console);
                } else {
                    ui.CloseModal();
                    DeleteInstance(name);
                }
                if (i != 3) ui.CloseModal();
            }
        }
    }
    if (ui.modal.open && ui.modal.id == "about") {
        Rect panel = ui.ModalPanel(520.f, 340.f, true);
        ui.DrawModalChrome(panel);
        Rect close(panel.r() - 44.f, panel.y + 16.f, 28.f, 28.f);
        if (ctx.IconButton("aboutclose", close, IconTex("x"), 16.f)) ui.CloseModal();
        DrawBrandMark(cv, Rect(panel.x + 24.f, panel.y + 24.f, 44.f, 44.f), th.brand, th.brandLight, 0.f);
        ctx.Text("Crit Launcher", panel.x + 82.f, panel.y + 24.f, 20.f, 700, th.text);
        ctx.Text("Native build · 1.8 · C++17 + DirectX 11", panel.x + 82.f, panel.y + 50.f, 13.f, 500, th.dim);
        ctx.Text("A full rewrite of the Python launcher. Same data folders, same UI design,\n"
                 "now a single self-contained .exe with no runtime dependencies.",
                 panel.x + 24.f, panel.y + 96.f, 13.5f, 400, th.text.with(0.85f), panel.w - 48.f);
        ctx.Text("Not affiliated with Mojang, Microsoft, Modrinth or CurseForge.", panel.x + 24.f,
                 panel.b() - 60.f, 12.f, 400, th.faint, panel.w - 48.f);
    }

    // ---- dropdown overlay is drawn by the widget that owns it, but if it was
    //      opened by a page that no longer draws it, close it.
    if (ui.dropdown.open && input.released &&
        !ui.dropdown.panel.contains(input.mouse) && !ui.dropdown.anchor.contains(input.mouse)) {
        ui.CloseDropdown();
    }

    // ---- toasts (bottom right)
    double now = Time();
    float toastY = size.y - 24.f;
    for (int i = (int)ui.toasts.size() - 1; i >= 0; i--) {
        Toast& t = ui.toasts[(size_t)i];
        double age = now - t.born;
        if (age > 5.0 && t.leavingAt == 0.0) t.leavingAt = now;
        float out = t.leavingAt > 0.0 ? (float)clamp01((now - t.leavingAt) / 0.25) : 0.f;
        if (t.leavingAt > 0.0 && out >= 1.f) {
            ui.toasts.erase(ui.toasts.begin() + i);
            continue;
        }
        float in = (float)clamp01(age / 0.35);
        float ease = EaseCss(in);
        TextStyle st;
        st.size = 13.f;
        st.weight = 400;
        float textW = std::min(320.f, Fonts().MeasureLine(t.message, st));
        float w = std::max(240.f, textW + 76.f);
        float h = 54.f;
        Rect box(size.x - 24.f - w + (1.f - ease) * 24.f + out * 24.f, toastY - h, w, h);
        toastY -= h + 10.f;
        cv.Shadow(box, 12.f, 30.f, Color(0, 0, 0, 0.5f), 10.f);
        cv.FillRounded(box, 12.f, th.panel2);
        cv.StrokeRounded(box.inset(0.5f), 11.5f, 1.f, t.error ? th.danger.with(0.35f) : th.border);
        Color iconCol = t.error ? th.danger : t.success ? th.accent : th.dim;
        cv.Image(IconTex(t.error ? "alert" : t.success ? "check-circle" : "alert"),
                 Rect(box.x + 14.f, box.y + 18.f, 18.f, 18.f), iconCol);
        st.color = th.text;
        Fonts().Draw(cv, t.message, st, box.x + 42.f, box.y + 10.f, box.w - 54.f);
        if (ctx.Hit(box) && input.released) t.leavingAt = now;
    }
}

}  // namespace crit
