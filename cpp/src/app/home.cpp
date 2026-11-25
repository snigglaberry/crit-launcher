// Crit Launcher Native — Home page (skin stage, changelog, stats, launch dock).
#include "app/app.h"
#include "app/pages.h"

namespace crit {

namespace {

// draws the skin model on the stage. Until a skin is available a small
// placeholder figure is drawn so the panel is never empty.
void DrawSkinStage(App& app, const Rect& r, Account* acc) {
    const Theme& th = GetTheme();
    Canvas& cv = app.render.canvas;
    UIContext& ctx = app.ui.ctx;

    cv.Shadow(Rect(r.x, r.y, r.w, r.h), 16.f, 22.f, Color(0, 0, 0, 0.30f), 8.f);
    cv.FillRounded(r, 16.f, Color::hex(0x0e1014).with(0.74f));
    cv.StrokeRounded(r.inset(0.5f), 15.5f, 1.f, Color(1, 1, 1, 0.08f));

    if (!acc) {
        int icon = IconTex("user");
        cv.Image(icon, Rect(r.center().x - 15.f, r.center().y - 15.f, 30.f, 30.f), th.faint);
        return;
    }
    float t = (float)(app.Time() - app.startTime);
    // the panel shows the account's skin head front-on, large, like the page's 3D skin stage
    float face = std::min(r.w * 0.52f, r.h * 0.5f);
    Rect head(r.center().x - face * 0.5f, r.y + r.h * 0.24f, face, face);
    // soft accent glow behind the figure
    cv.FillCircle(Rect(r.x, r.y + r.h * 0.2f, r.w, r.h * 0.6f).center(), face * 0.85f, th.accent.with(0.05f));
    cv.FillCircle(Rect(r.x, r.y + r.h * 0.2f, r.w, r.h * 0.6f).center(), face * 0.6f, th.accent.with(0.04f));
    if (acc->skinTex >= 0) {
        // head + hat overlay, nearest-neighbour look via pixel-snapped quads
        int px = 8;
        float cell = face / (float)px;
        for (int y = 0; y < px; y++) {
            for (int x = 0; x < px; x++) {
                Rect q(head.x + (float)x * cell, head.y + (float)y * cell, cell + 0.5f, cell + 0.5f);
                cv.Image(acc->skinTex, q, Color(1, 1, 1, 1), (8.f + (float)x) / 64.f, (8.f + (float)y) / 64.f,
                         (9.f + (float)x) / 64.f, (9.f + (float)y) / 64.f);
            }
        }
        for (int y = 0; y < px; y++) {
            for (int x = 0; x < px; x++) {
                Rect q(head.x + (float)x * cell, head.y + (float)y * cell, cell + 0.5f, cell + 0.5f);
                cv.Image(acc->skinTex, q, Color(1, 1, 1, 1), (40.f + (float)x) / 64.f, (8.f + (float)y) / 64.f,
                         (41.f + (float)x) / 64.f, (9.f + (float)y) / 64.f);
            }
        }
    } else {
        cv.FillCircle(head.center(), face * 0.42f, th.panel3);
        int icon = IconTex("user");
        cv.Image(icon, Rect(head.center().x - face * 0.2f, head.center().y - face * 0.2f, face * 0.4f, face * 0.4f), th.faint);
    }
    // name tag
    string name = acc->name;
    float nw = ctx.TextW(name, 17.f, 700);
    Rect tag(r.center().x - nw * 0.5f - 14.f, head.b() + 16.f, nw + 28.f, 32.f);
    cv.FillRounded(tag, 9.f, Color(0, 0, 0, 0.35f));
    ctx.Text(name, tag.x + 14.f, tag.y + (tag.h - Fonts().LineHeight(700, 17.f) * 1.45f) * 0.5f, 17.f, 700, th.text);
    (void)t;
}

// renders a tiny subset of markdown (headings, bullets, bold, code, links)
void DrawMarkdown(App& app, const string& md, const Rect& area, float& usedHeight) {
    const Theme& th = GetTheme();
    UIContext& ctx = app.ui.ctx;
    Canvas& cv = app.render.canvas;
    float y = area.y;
    float x = area.x;
    TextStyle st;
    st.weight = 400;
    st.size = 13.5f;
    st.color = Color::hex(0xcfd3d9);
    for (auto& rawLine : Split(md, '\n')) {
        string line = Trim(rawLine);
        if (line.empty()) {
            y += 7.f;
            continue;
        }
        if (StartsWith(line, "### ")) {
            st.weight = 650;
            st.size = 14.f;
            st.color = th.text;
            y += 6.f;
            Fonts().Draw(cv, line.substr(4), st, x, y, area.w);
            y += Fonts().LineHeight(650, 14.f) * 1.45f + 4.f;
            continue;
        }
        if (StartsWith(line, "## ")) {
            st.weight = 650;
            st.size = 16.f;
            st.color = th.text;
            y += 8.f;
            Fonts().Draw(cv, line.substr(3), st, x, y, area.w);
            y += Fonts().LineHeight(650, 16.f) * 1.45f + 4.f;
            continue;
        }
        if (StartsWith(line, "# ")) {
            st.weight = 650;
            st.size = 18.f;
            st.color = th.text;
            y += 8.f;
            Fonts().Draw(cv, line.substr(2), st, x, y, area.w);
            y += Fonts().LineHeight(650, 18.f) * 1.45f + 4.f;
            continue;
        }
        bool bullet = false;
        string text = line;
        if (StartsWith(line, "- ") || StartsWith(line, "* ")) {
            bullet = true;
            text = line.substr(2);
        }
        // strip markdown emphasis/code markers for display
        string clean;
        for (size_t i = 0; i < text.size(); i++) {
            char c = text[i];
            if (c == '*' || c == '`') continue;
            if (c == '[') {
                size_t close = text.find(']', i);
                if (close != string::npos) {
                    string label = text.substr(i + 1, close - i - 1);
                    size_t paren = text.find(')', close);
                    if (paren != string::npos) {
                        clean += label;
                        i = paren;
                        continue;
                    }
                }
            }
            clean.push_back(c);
        }
        st.weight = 400;
        st.size = 13.5f;
        st.color = Color::hex(0xcfd3d9);
        float lh = Fonts().LineHeight(400, 13.5f) * 1.55f;
        if (bullet) {
            cv.FillCircle({x + 4.f, y + lh * 0.42f}, 2.f, th.faint);
            int lines = 0;
            float used = Fonts().Draw(cv, clean, st, x + 16.f, y, area.w - 16.f, 0.f, &lines);
            y += std::max(lh, used);
        } else {
            int lines = 0;
            float used = Fonts().Draw(cv, clean, st, x, y, area.w, 0.f, &lines);
            y += std::max(lh, used);
        }
        if (y > area.b()) break;
    }
    usedHeight = y - area.y;
}

}  // namespace

void App::DrawHome(const Rect& contentArea) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect area = contentArea;

    // home background artwork, faded like #home-bg
    if (homeBgTex >= 0) {
        float imgA = ui.anim.Animate("hombg", homeBgLoaded ? 1.f : 0.f, 3.f);
        if (imgA > 0.01f) {
            Texture* tex = Tex(homeBgTex);
            if (tex && tex->w > 0) {
                float tr = (float)tex->w / (float)tex->h;
                float rr = area.w / area.h;
                Rect dst = area;
                if (tr > rr) {
                    float w = area.h * tr;
                    dst = Rect(area.center().x - w * 0.5f, area.y, w, area.h);
                } else {
                    // background-position: center 30%
                    float h = area.w / tr;
                    dst = Rect(area.x, area.y + (area.h - h) * 0.3f, area.w, h);
                }
                cv.Image(homeBgTex, dst, Color(1, 1, 1, imgA));
                // #home-bg::after vertical shade stops (same as the CSS gradient)
                cv.GradientV(Rect(area.x, area.y, area.w, area.h * 0.18f), th.bg.with(imgA),
                             Color(11 / 255.f, 12 / 255.f, 14 / 255.f, 0.55f * imgA));
                cv.GradientV(Rect(area.x, area.y + area.h * 0.18f, area.w, area.h * 0.20f),
                             Color(11 / 255.f, 12 / 255.f, 14 / 255.f, 0.55f * imgA),
                             Color(11 / 255.f, 12 / 255.f, 14 / 255.f, 0.25f * imgA));
                cv.GradientV(Rect(area.x, area.y + area.h * 0.38f, area.w, area.h * 0.34f),
                             Color(11 / 255.f, 12 / 255.f, 14 / 255.f, 0.25f * imgA),
                             Color(11 / 255.f, 12 / 255.f, 14 / 255.f, 0.55f * imgA));
                cv.GradientV(Rect(area.x, area.y + area.h * 0.72f, area.w, area.h * 0.28f),
                             Color(11 / 255.f, 12 / 255.f, 14 / 255.f, 0.55f * imgA), th.bg.with(imgA));
            }
        }
    }

    // content: page has 28px padding, page max-width 1100 centred, height = viewport - 118
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(1100.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    // bottom dock (launch button): centred, 18px above the content edge
    float dockH = 100.f;
    Rect dock(page.x, page.b() - dockH, page.w, dockH);
    Rect body(page.x, page.y, page.w, page.h - dockH - 12.f);

    // grid: left column minmax(250,330) / 1fr, gap 16
    float leftW = clampf(body.w * 0.32f, 250.f, 330.f);
    Rect left(body.x, body.y, leftW, body.h);
    Rect right(body.x + leftW + 16.f, body.y, body.w - leftW - 16.f, body.h);
    Rect skin(left.x, left.y, left.w, left.h);
    Rect news(right.x, right.y, right.w, right.h);

    Account* acc = ActiveAccount();
    if (acc && acc->skinTex < 0 && acc->hasSkin == false) LoadSkin(*acc);
    DrawSkinStage(*this, skin, acc);
    if (ctx.Hit(skin) && input.released) Navigate(Page::Skins);

    // changelog card
    cv.Shadow(news, 16.f, 22.f, Color(0, 0, 0, 0.30f), 8.f);
    cv.FillRounded(news, 16.f, Color::hex(0x0e1014).with(0.74f));
    cv.StrokeRounded(news.inset(0.5f), 15.5f, 1.f, Color(1, 1, 1, 0.08f));
    Rect inner(news.x + 22.f, news.y + 20.f, news.w - 28.f, news.h - 40.f);
    // head
    Rect headIcon(inner.x, inner.y + 2.f, 17.f, 17.f);
    cv.Image(IconTex("list"), headIcon, th.dim);
    ctx.Text("Changelog", inner.x + 27.f, inner.y, 16.f, 680, th.text);
    if (!changelog.tag.empty()) {
        float tw = ctx.TextW(changelog.tag, 12.f, 500);
        Rect tag(inner.x + 27.f + ctx.TextW("Changelog", 16.f, 680) + 10.f, inner.y + 2.f, tw + 18.f, 22.f);
        cv.FillRounded(tag, 11.f, th.accentSoft);
        ctx.TextCenter(changelog.tag, tag, 12.f, 500, th.accent);
    }
    if (!changelog.date.empty()) ctx.TextRight(changelog.date, Rect(inner.x, inner.y, inner.w, 22.f), 12.5f, 400, th.faint);
    Rect bodyRect(inner.x, inner.y + 34.f, inner.w, inner.h - 34.f);
    if (changelog.loading && !changelog.loaded) {
        DrawSpinner(cv, Rect(bodyRect.center().x - 11.f, bodyRect.center().y - 11.f, 22.f, 22.f), th.dim,
                    (float)Time());
        ctx.TextCenter("Loading changelog...", Rect(bodyRect.x, bodyRect.center().y + 16.f, bodyRect.w, 20.f), 13.f, 400,
                       th.dim);
    } else if (!changelog.loaded) {
        int alertIcon = IconTex("alert");
        cv.Image(alertIcon, Rect(bodyRect.center().x - 13.f, bodyRect.center().y - 30.f, 26.f, 26.f), th.dim);
        ctx.TextCenter(changelog.error.empty() ? "No changelog available." : changelog.error,
                       Rect(bodyRect.x, bodyRect.center().y + 2.f, bodyRect.w, 20.f), 13.f, 400, th.dim);
        Rect retry(bodyRect.center().x - 50.f, bodyRect.center().y + 30.f, 100.f, 32.f);
        if (ctx.Button("clretry", retry, "Try again", false, true, false, IconTex("refresh"))) {
            changelog.loading = false;
            changelog.loaded = false;
            LoadChangelog(true);
        }
    } else {
        ui.PushClip(bodyRect);
        float used = 0.f;
        float scroll = ui.ctx.ScrollArea("clbody", bodyRect, 2000.f, pageScroll[Page::Home]);
        DrawMarkdown(*this, changelog.bodyMarkdown, Rect(bodyRect.x, bodyRect.y - scroll, bodyRect.w - 14.f, 4000.f), used);
        ui.PopClip();
    }

    // ------------------------------------------------ dock
    Instance* active = ActiveInstance();
    string label = active && IsInstanceRunning(active->name) ? "PLAYING" : "LAUNCH";
    if (!active) label = "LAUNCH";
    Rect split(dock.center().x - 195.f, dock.y + 14.f, 390.f, 72.f);
    cv.Shadow(split, 14.f, 22.f, Color(0, 0, 0, 0.35f), 9.f);
    Rect mainBtn(split.x, split.y, split.w * 0.75f, split.h);
    Rect pickBtn(mainBtn.r(), split.y, split.w * 0.25f, split.h);
    bool busy = active && IsInstanceInstalling(active->name);
    bool runningNow = active && IsInstanceRunning(active->name);
    Color mainCol = th.accent;
    if (ctx.Hit(mainBtn) && !busy) mainCol = th.accentHover;
    cv.FillRounded(split, 14.f, mainCol);
    cv.FillRect(Rect(pickBtn.x - 1.f, pickBtn.y, 1.f, pickBtn.h), Color(0, 0, 0, 0.22f));
    // sheen on hover
    float hov = ui.anim.HoverFade("launch", ctx.Hit(split), 10.f);
    if (hov > 0.01f) cv.FillRect(Rect(split.x, split.y, split.w * 0.45f * hov, split.h), Color(1, 1, 1, 0.05f));
    string mainLabel = runningNow ? "PLAYING" : (active && !active->installed ? "INSTALL" : label);
    ctx.TextCenter(mainLabel, Rect(mainBtn.x, mainBtn.y + 14.f, mainBtn.w, 30.f), 28.f, 900, th.accentInk);
    string sub = active ? (active->name + " · " + active->version) : "No instance selected";
    ctx.TextCenter(sub, Rect(mainBtn.x, mainBtn.y + 45.f, mainBtn.w, 15.f), 11.f, 650, th.accentInk.with(0.72f));
    // pick chevron
    DrawChevron(cv, Rect(pickBtn.center().x - 9.f, pickBtn.center().y - 5.f, 18.f, 10.f), th.accentInk, 2.2f, 3);
    if (ctx.Hit(mainBtn) && input.pressed && active && !busy) {
        // press animation handled by the page's scale on click
    }
    if (ctx.Hit(mainBtn) && input.released && active && !busy) Launch(*active);
    if (ctx.Hit(pickBtn) && input.released) {
        std::vector<string> names;
        int sel = 0;
        for (size_t i = 0; i < instances.size(); i++) {
            names.push_back(instances[i].name);
            if (active && instances[i].name == active->name) sel = (int)i;
        }
        if (names.empty()) names.push_back("No instances");
        int chosen = sel;
        if (ui.Select("launchpick", Rect(pickBtn.x - 180.f, pickBtn.y, 200.f, 30.f), names, chosen)) {
            if (chosen >= 0 && chosen < (int)instances.size()) SetActiveInstance(instances[(size_t)chosen].name);
        }
    }
    // statistics card under the launcher button
    float statsY = dock.b() - 22.f;
    int64_t total = 0;
    for (auto& i : instances) total += i.playtime;
    int clk = IconTex("clock");
    string playedText = FormatDurationShort(total) + " played";
    cv.Image(clk, Rect(split.x - 96.f, statsY - 2.f, 15.f, 15.f), th.faint);
    ctx.Text(playedText, split.x - 76.f, statsY - 2.f, 12.f, 500, th.faint);
    string cnt = Format("%d instance%s", (int)instances.size(), instances.size() == 1 ? "" : "s");
    ctx.TextRight(cnt, Rect(split.r() - 20.f, statsY - 2.f, 120.f, 15.f), 12.f, 500, th.faint);
}

// ---------------------------------------------------------------- page wrapper
namespace {
struct HomePage : PageBase {
    void Draw(App& app) override { app.DrawHome(app.contentRect); }
};
}  // namespace

std::unique_ptr<PageBase> MakeHomePage() { return std::make_unique<HomePage>(); }

}  // namespace crit
