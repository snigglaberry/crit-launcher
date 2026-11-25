// Crit Launcher Native — app shell: sidebar, top bar, boot overlay, overlays.
#include "app/app.h"

namespace crit {

static float S(float v) { return v; }

void DrawCard(Canvas& cv, const Rect& r, float radius) {
    const Theme& th = GetTheme();
    cv.FillRounded(r, radius, th.panel);
    cv.StrokeRounded(r.inset(0.5f), radius - 0.5f, 1.f, th.border);
}

void DrawSectionTitle(UIContext& ctx, const string& text, float x, float y) {
    ctx.Text(text, x, y, 18.f, 700, GetTheme().text);
}

// ---------------------------------------------------------------- sidebar
void App::DrawSidebar(const Rect& r) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    bool compact = sidebarAnim < 150.f;
    float w = r.w;
    cv.FillRect(r, th.panel);
    cv.FillRect(Rect(r.r() - 1.f, r.y, 1.f, r.h), th.border);

    float padX = 12.f;
    float innerW = w - padX * 2.f;
    float y = 18.f;

    // brand row
    Rect brand(padX + 7.f, y, innerW - 14.f, 40.f);
    sidebar.brand = brand;
    float markX = compact ? brand.center().x - 19.f : brand.x;
    Rect mark(markX, brand.y + 1.f, 38.f, 38.f);
    float rot = ui.anim.Animate("brandrot", ctx.Hit(brand) ? kPi * 0.5f : 0.f, 8.f);
    DrawBrandMark(cv, Rect(mark.x + 1.f, mark.y + 1.f, 36.f, 36.f), th.brand, th.brandLight, rot);
    float brandTextA = compact ? 0.f : ui.anim.Animate("brandtxt", 1.f, 10.f);
    if (brandTextA > 0.01f) {
        ctx.Text("CRIT", brand.x + 48.f, brand.y + 10.f, 14.f, 700, th.text.with(brandTextA));
        ctx.Text("LAUNCHER", brand.x + 48.f, brand.y + 24.f, 8.5f, 600, th.dim.with(brandTextA));
    }
    y += 62.f;

    // main navigation
    struct NavDef { Page page; const char* icon; const char* label; };
    static const NavDef navDefs[] = {
        {Page::Home, "home", "Home"},
        {Page::Instances, "box", "Instances"},
        {Page::Addons, "puzzle", "Addons"},
        {Page::Skins, "shirt", "Skins"},
        {Page::Screenshots, "image", "Screenshots"},
        {Page::Servers, "server", "Servers"},
    };
    float itemH = 42.f;
    for (int i = 0; i < 6; i++) {
        Rect item(padX, y, innerW, itemH);
        sidebar.nav[i] = item;
        if (ui.ctx.NavItem("nav" + std::to_string(i), item, IconTex(navDefs[i].icon), navDefs[i].label,
                           page == navDefs[i].page, compact)) {
            Navigate(navDefs[i].page);
        }
        y += itemH + 4.f;
    }

    // bottom navigation
    float statsH = 40.f;
    float launchH = 0.f;
    Rect progressBox(padX, r.b() - statsH - 84.f, innerW, 0.f);
    if (progress.active) {
        progressBox.h = 44.f;
        showSidebarProgress = true;
        sidebarProgressBar = progressBox;
        cv.FillRounded(progressBox.inset(0.f), 10.f, th.panel2.with(0.6f));
        ctx.Text(progress.task.empty() ? "Working…" : progress.task, progressBox.x + 10.f, progressBox.y + 6.f, 11.5f,
                 400, th.dim, progressBox.w - 20.f);
        Rect bar(progressBox.x + 10.f, progressBox.y + 26.f, progressBox.w - 20.f, 5.f);
        cv.FillRounded(bar, 3.f, th.panel3);
        float pw = bar.w * clamp01(progress.fraction);
        if (pw > 1.f) cv.FillRounded(Rect(bar.x, bar.y, pw, bar.h), 3.f, th.accent);
        launchH = 50.f;
    } else {
        showSidebarProgress = false;
    }
    (void)launchH;
    float navBottomY = r.b() - statsH - 12.f - (itemH + 4.f) * 2.f;
    if (progress.active) navBottomY -= 54.f;
    Rect acc(padX, navBottomY, innerW, itemH);
    sidebar.navBottom[0] = acc;
    if (ctx.NavItem("navacc", acc, IconTex("user"), "Accounts", page == Page::Accounts, compact)) Navigate(Page::Accounts);
    Rect set(padX, navBottomY + itemH + 4.f, innerW, itemH);
    sidebar.navBottom[1] = set;
    if (ctx.NavItem("navset", set, IconTex("settings"), "Settings", page == Page::Settings, compact)) Navigate(Page::Settings);

    // stats strip
    Rect stats(r.x, r.b() - statsH, r.w, statsH);
    sidebar.stats = stats;
    cv.FillRect(Rect(stats.x, stats.y, stats.w, 1.f), th.border);
    float sx = stats.x + (compact ? 0.f : 20.f);
    if (compact) {
        int runIcon = IconTex("box");
        cv.Image(runIcon, Rect(stats.center().x - 1.f, stats.y + 12.f, 17.f, 17.f), th.faint);
    } else {
        int runIcon = IconTex("box");
        cv.Image(runIcon, Rect(sx, stats.y + 12.f, 17.f, 17.f), th.faint);
        string runningTxt = Format("%d running", (int)runningInstances.size());
        ctx.Text(runningTxt, sx + 23.f, stats.y + 12.f, 11.5f, 400, th.faint);
        int clk = IconTex("clock");
        int64_t total = 0;
        for (auto& i : instances) total += i.playtime;
        string timeTxt = FormatDurationShort(total);
        float tw = ctx.TextW(timeTxt, 11.5f, 400);
        cv.Image(clk, Rect(stats.r() - 20.f - tw - 23.f, stats.y + 12.f, 17.f, 17.f), th.faint);
        ctx.Text(timeTxt, stats.r() - 20.f - tw, stats.y + 12.f, 11.5f, 400, th.faint);
    }

    // collapse handle (the page shows it as a small circle on the sidebar edge)
    Rect collapse(r.r() - 12.f, 24.f, 24.f, 24.f);
    sidebar.collapseBtn = collapse;
    bool ch = ctx.Hit(collapse);
    cv.FillCircle(collapse.center(), 12.f, ch ? th.panel3 : th.panel2);
    cv.FillCircle(collapse.center(), 11.4f, ch ? th.panel3 : th.panel2);
    (void)ch;
    DrawChevron(cv, Rect(collapse.center().x - 4.f, collapse.center().y - 5.f, 8.f, 10.f), th.dim, 1.6f,
                sidebarCollapsed ? 2 : 1);
    if (ctx.Hit(collapse) && input.released) settings.sidebarCollapsed = !settings.sidebarCollapsed;
}

// ---------------------------------------------------------------- topbar
void App::DrawTopBar(const Rect& r) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    cv.FillRect(r, th.bg);
    cv.FillRect(Rect(r.x, r.b() - 1.f, r.w, 1.f), th.borderSoft);

    float x = r.x + 28.f;
    // page title
    string title = PageTitle(page);
    ctx.Text(title, x, r.y + (r.h - Fonts().LineHeight(650, 17.f) * 1.45f) * 0.5f, 17.f, 650, th.text);
    float tw = ctx.TextW(title, 17.f, 650);
    x += tw + 18.f;
    // history buttons
    Rect back(x, r.y + 12.f, 38.f, 38.f);
    bool canBack = historyIndex > 0;
    if (ctx.IconButton("topback", back, IconTex("chevron-left"), 18.f) && canBack) GoBack();
    if (!canBack) {
        cv.FillRounded(back.inset(7.f), 8.f, Color(0, 0, 0, 0));
    }
    Rect fwd(back.r() + 2.f, back.y, 38.f, 38.f);
    bool canFwd = historyIndex + 1 < (int)pageHistory.size();
    if (ctx.IconButton("topfwd", fwd, IconTex("chevron-right"), 18.f) && canFwd) GoForward();


    // right side: account chip + window buttons
    Rect winZone(r.r() - 138.f, r.y, 138.f, r.h);
    float wcIconSize = 10.f;
    const char* wcTooltips[3] = {"Minimize", "Maximize", "Close"};
    for (int i = 0; i < 3; i++) {
        Rect btn(winZone.x + 46.f * (float)i, winZone.y, 46.f, winZone.h);
        bool hover = ctx.Hit(btn);
        if (hover) {
            cv.FillRect(btn, i == 2 ? Color::hex(0xe81123) : Color(1, 1, 1, 0.1f));
        }
        Color fg = hover && i == 2 ? Color(1, 1, 1, 1) : th.text;
        Rect ic(btn.center().x - wcIconSize * 0.5f, btn.center().y - wcIconSize * 0.5f, wcIconSize, wcIconSize);
        if (i == 0) cv.FillRect(Rect(ic.x, btn.center().y, ic.w, 1.f), fg);
        else if (i == 1) {
            cv.StrokeRounded(ic, 0.f, 1.f, fg);
        } else {
            DrawClose(cv, ic, fg, 1.f);
        }
        if (hover && input.released) {
            if (i == 0) platform->Minimize();
            else if (i == 1) platform->ToggleMaximize();
            else {
                SaveSettings();
                quitRequested_ = true;
            }
        }
        (void)wcTooltips;
    }

    // account chip
    Account* acc = ActiveAccount();
    string chipName = acc ? acc->name : "Add account";
    string chipType = acc ? (acc->type == "microsoft" ? "Microsoft" : acc->type == "ely" ? "Ely.by" : "Offline") : "No account";
    float nameW = ctx.TextW(chipName, 14.f, 500);
    float typeW = ctx.TextW(chipType, 9.f, 700);
    float chipW = 26.f + 10.f + std::max(nameW, typeW) + 8.f;
    Rect chip(winZone.x - chipW - 24.f, r.y + 8.f, chipW, r.h - 16.f);
    bool chipHover = ctx.Hit(chip);
    if (chipHover) cv.FillRounded(chip, 10.f, th.panel2);
    Rect face(chip.x + 2.f, chip.center().y - 13.f, 26.f, 26.f);
    DrawFace(cv, acc ? acc->skinTex : -1, face, 7.f);
    ctx.Text(chipType, face.r() + 10.f, chip.center().y - 13.f, 9.f, 700, th.dim);
    ctx.Text(chipName, face.r() + 10.f, chip.center().y + 1.f, 14.f, 500, th.text, chipW - 40.f);
    if (chipHover && input.released) Navigate(Page::Accounts);
}

// ---------------------------------------------------------------- boot overlay
void App::DrawBoot() {
    if (!bootShown) return;
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    double t = Time() - startTime;
    // the page keeps the splash until the UI is wired up; fade once things are loaded
    double fadeStart = 1.6;
    double fadeDur = 0.6;
    float a = (float)clamp01((t - fadeStart) / fadeDur);
    a = a * a * (3.f - 2.f * a);
    if (a >= 1.f) {
        bootShown = false;
        return;
    }
    Rect full(0, 0, size.x, size.y);
    cv.FillRect(full, th.bg.with(1.f - a));
    float scale = 1.f + a * 0.035f;
    Vec2 c = full.center();
    // radial glow
    for (int i = 6; i >= 1; i--) {
        float rad = 120.f + (float)i * 44.f;
        cv.FillCircle(c, rad * scale, th.accent.with(0.018f * (7 - i) * (1.f - a)));
    }
    bool reveal = t > 0.55;
    float logoSize = (reveal ? 84.f : 56.f);
    float lt = (float)clamp01((t - 0.55) / 1.4);
    logoSize = lerp(56.f, 84.f, EaseCss(lt));
    float pop = (float)clamp01(t / 0.8);
    float popScale = 1.f;
    if (pop < 1.f) {
        // spring overshoot
        float s = EaseOut(pop);
        popScale = 1.15f * s + (1.f - s) * 0.0f;
        popScale = 0.4f + (popScale - 0.4f);
    }
    float ax = lerp(logoSize, 56.f * popScale, pop < 1.f ? 1.f : 0.f);
    if (pop >= 1.f) ax = logoSize;
    Rect logo(c.x - 170.f - ax * 0.5f, c.y - 60.f - ax * 0.5f, ax, ax);
    // when not revealed the logo sits centered
    float nameW = ctx.TextW("Crit Launcher", 40.f, 700);
    float totalW = ax + (reveal ? 20.f + nameW * EaseCss(lt) : 0.f);
    logo.x = c.x - totalW * 0.5f;
    DrawBrandMark(cv, logo, th.brand, th.brandLight, 0.f, 1.f - a);
    if (reveal) {
        float nameA = (float)clamp01((t - 0.75) / 0.55);
        ctx.Text("Crit Launcher", logo.r() + 20.f, logo.center().y - Fonts().LineHeight(700, 40.f) * 0.72f, 40.f, 700,
                 th.text.with(nameA * (1.f - a)));
    }
    // loading bar
    if (!reveal) {
        Rect bar(c.x - 95.f, c.y + 60.f, 190.f, 4.f);
        cv.FillRounded(bar, 2.f, Color(1, 1, 1, 0.07f));
        float prog = (float)clamp01(t / 2.0);
        prog = EaseOut(prog) * 0.86f;
        if (prog > 0.002f) cv.FillRounded(Rect(bar.x, bar.y, bar.w * prog, bar.h), 2.f, th.accent);
    }
    // version tag bottom
    ctx.Text("1.8", c.x - 10.f, size.y - 34.f, 12.f, 500, th.faint.with(1.f - a));
}

}  // namespace crit
