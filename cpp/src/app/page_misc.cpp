// Crit Launcher Native — Addons, Skins, Screenshots, Servers and Console pages.
#include "app/pages.h"

namespace crit {

namespace {

struct AddonsPage : PageBase { void Draw(App& a) override { a.DrawAddons(a.contentRect); } };
struct SkinsPage : PageBase { void Draw(App& a) override { a.DrawSkins(a.contentRect); } };
struct ShotsPage : PageBase { void Draw(App& a) override { a.DrawScreenshots(a.contentRect); } };
struct ServersPage : PageBase { void Draw(App& a) override { a.DrawServers(a.contentRect); } };
struct ConsolePage : PageBase { void Draw(App& a) override { a.DrawConsole(a.contentRect); } };

}  // namespace

std::unique_ptr<PageBase> MakeAddonsPage() { return std::make_unique<AddonsPage>(); }
std::unique_ptr<PageBase> MakeSkinsPage() { return std::make_unique<SkinsPage>(); }
std::unique_ptr<PageBase> MakeScreenshotsPage() { return std::make_unique<ShotsPage>(); }
std::unique_ptr<PageBase> MakeServersPage() { return std::make_unique<ServersPage>(); }
std::unique_ptr<PageBase> MakeConsolePage() { return std::make_unique<ConsolePage>(); }

// ---------------------------------------------------------------- addons
void App::DrawAddons(const Rect& area) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(1100.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    ctx.Text("Addons", page.x, page.y, 24.f, 700, th.text);
    ctx.Text("Mods, resource packs, shaders and modpacks from Modrinth and CurseForge.", page.x, page.y + 34.f, 13.5f,
             400, th.dim);

    // source pill
    Rect src(page.r() - 240.f, page.y, 240.f, 40.f);
    cv.FillRounded(src, 20.f, th.panel);
    cv.StrokeRounded(src.inset(0.5f), 19.5f, 1.f, th.border);
    Rect ms(src.x + 4.f, src.y + 4.f, 116.f, 32.f), cf(src.x + 120.f, src.y + 4.f, 116.f, 32.f);
    bool modrinth = addonsSource == 0;
    if (!modrinth) cv.FillRounded(cf, 16.f, th.accent.with(0.22f));
    else cv.FillRounded(ms, 16.f, th.accent.with(0.22f));
    ctx.TextCenter("Modrinth", ms, 12.5f, 700, modrinth ? th.accent : th.dim);
    ctx.TextCenter("CurseForge", cf, 12.5f, 700, modrinth ? th.dim : th.accent);
    if (ctx.Hit(ms) && input.released) addonsSource = 0;
    if (ctx.Hit(cf) && input.released) addonsSource = 1;

    // tabs
    int tab = std::clamp(addonsTab, 0, 3);
    if (ctx.Segmented("addontabs", Rect(page.x, page.y + 74.f, 430.f, 44.f), {"Mods", "Packs", "Shaders", "Modpacks"}, tab))
        addonsTab = tab;

    Rect search(page.x + 450.f, page.y + 74.f, page.w - 450.f, 44.f);
    ctx.TextField("addonsearch", search, addonsSearch, "Search addons…");

    Rect body(page.x, page.y + 136.f, page.w, page.b() - page.y - 136.f);
    ui.PushClip(body);
    // live search against Modrinth
    static std::vector<Json> results;
    static bool busy = false;
    static string lastQuery;
    static int lastSource = -1;
    static int lastTab = -1;
    bool queryChanged = lastQuery != addonsSearch || lastSource != addonsSource || lastTab != addonsTab;
    if (queryChanged && !busy) {
        busy = true;
        lastQuery = addonsSearch;
        lastSource = addonsSource;
        lastTab = addonsTab;
        string query = addonsSearch;
        int source = addonsSource;
        int tabIndex = addonsTab;
        App* a = this;
        workers.emplace_back([a, query, source, tabIndex] {
            std::vector<Json> items;
            if (source == 0) {
                const char* typeFacet[] = {"project_type:mod", "project_type:resourcepack", "project_type:shader",
                                           "project_type:modpack"};
                string url = "https://api.modrinth.com/v2/search?limit=20&facets=" +
                             UrlEncode(string("[[\"") + typeFacet[tabIndex] + "\"]]");
                if (!query.empty()) url += "&query=" + UrlEncode(query);
                HttpResult r = HttpGet(url, {{"User-Agent", "CritLauncher/1.8 (windows)"}});
                if (r.ok) {
                    Json j = Json::parse(r.body);
                    for (auto& hit : j["hits"].arr) items.push_back(hit);
                }
            }
            std::function<void()> fn = [a, items] {
                results = items;
                busy = false;
            };
            a->platform->Post(fn);
        });
    }
    if (busy) DrawSpinner(cv, Rect(body.center().x - 11.f, body.center().y - 60.f, 22.f, 22.f), th.accent,
                          (float)Time());
    if (results.empty() && !busy) {
        cv.StrokeRounded(Rect(body.x, body.y, body.w - 14.f, 160.f), 14.f, 1.f, th.border);
        int icon = IconTex("puzzle");
        cv.Image(icon, Rect(body.center().x - 15.f, body.y + 40.f, 30.f, 30.f), th.faint);
        ctx.TextCenter(addonsSearch.empty() ? "Nothing loaded yet — type to search." : "No results for that search.",
                       Rect(body.x, body.y + 84.f, body.w, 20.f), 13.5f, 400, th.dim);
    }
    float colW = (body.w - 28.f - 16.f * 2.f) / 3.f;
    float offset = ui.ctx.ScrollArea("addonlist", body, 400.f + (results.size() / 3) * 120.f, pageScroll[Page::Addons]);
    for (size_t i = 0; i < results.size(); i++) {
        int col = (int)i % 3, row = (int)i / 3;
        Rect card(body.x + (colW + 16.f) * (float)col, body.y + 120.f * (float)row - offset, colW, 104.f);
        if (card.b() < body.y || card.y > body.b()) continue;
        bool hover = ctx.Hit(card);
        cv.FillRounded(card, 13.f, hover ? th.panel2 : th.panel);
        cv.StrokeRounded(card.inset(0.5f), 12.5f, 1.f, hover ? Color::hex(0x323845) : th.border);
        const Json& it = results[i];
        Rect iconBox(card.x + 14.f, card.y + 14.f, 56.f, 56.f);
        cv.FillRounded(iconBox, 12.f, th.panel3);
        ctx.Text("C", iconBox.center().x - 4.f, iconBox.center().y - 8.f, 18.f, 800, th.accent);
        ctx.Text(it.getString("title"), iconBox.r() + 14.f, card.y + 14.f, 14.5f, 700, th.text, card.w - 100.f);
        string author = it.getString("author");
        ctx.Text("by " + author, iconBox.r() + 14.f, card.y + 36.f, 12.f, 400, th.dim, card.w - 100.f);
        string desc = it.getString("description");
        ctx.Text(desc, card.x + 14.f, card.y + 74.f, 12.f, 400, th.dim, card.w - 28.f);
        char dl[32];
        snprintf(dl, sizeof dl, "%d", (int)it.getNumber("downloads"));
        ctx.TextRight(string(dl) + " DL", Rect(card.x, card.y + 14.f, card.w - 14.f, 16.f), 11.5f, 600, th.faint);
    }
    ui.PopClip();
}

// ---------------------------------------------------------------- skins
void App::DrawSkins(const Rect& area) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(1100.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    ctx.Text("Skins", page.x, page.y, 24.f, 700, th.text);
    ctx.Text("Your account's skin. Drop a 64×64 PNG on the window to upload (offline accounts store it locally).",
             page.x, page.y + 34.f, 13.5f, 400, th.dim);

    Account* acc = ActiveAccount();
    Rect stage(page.x, page.y + 80.f, page.w * 0.46f, page.b() - page.y - 80.f);
    cv.FillRounded(stage, 18.f, th.panel);
    cv.StrokeRounded(stage.inset(0.5f), 17.5f, 1.f, th.border);
    if (acc) {
        float face = std::min(stage.w * 0.56f, stage.h * 0.55f);
        Rect head(stage.center().x - face * 0.5f, stage.y + stage.h * 0.16f, face, face);
        cv.FillCircle(head.center(), face * 0.7f, th.accent.with(0.06f));
        if (acc->skinTex >= 0) {
            TileSkin(cv, acc->skinTex, head, face);
        } else {
            DrawFace(cv, acc->skinTex, head, 18.f);
            if (acc->skinTex < 0) LoadSkin(*acc);
        }
        ctx.TextCenter(acc->name, Rect(stage.x, head.b() + 20.f, stage.w, 24.f), 18.f, 700, th.text);
        ctx.TextCenter(acc->type == "microsoft" ? "Microsoft account" : "Offline account",
                       Rect(stage.x, head.b() + 44.f, stage.w, 18.f), 12.5f, 500, th.dim);
    } else {
        ctx.TextCenter("No account selected", Rect(stage.x, stage.center().y, stage.w, 20.f), 13.5f, 400, th.dim);
    }

    // right column: library + actions
    Rect side(page.x + page.w * 0.46f + 18.f, page.y + 80.f, page.w - page.w * 0.46f - 18.f, page.b() - page.y - 80.f);
    float y = side.y;
    Rect card1(side.x, y, side.w, 150.f);
    cv.FillRounded(card1, 14.f, th.panel);
    cv.StrokeRounded(card1.inset(0.5f), 13.5f, 1.f, th.border);
    ctx.Text("Skin library", card1.x + 18.f, card1.y + 16.f, 14.5f, 600, th.text);
    ctx.Text("Saved local skins from this launcher.", card1.x + 18.f, card1.y + 38.f, 12.5f, 400, th.dim);
    ui.PushClip(card1);
    std::vector<string> saved = ListDir(DataPath("skins"));
    float sx = card1.x + 18.f;
    for (size_t i = 0; i < saved.size() && i < 7; i++) {
        if (!EndsWith(Lower(saved[i]), ".png")) continue;
        Rect tile(sx, card1.y + 70.f, 60.f, 60.f);
        cv.FillRounded(tile, 10.f, th.panel3);
        ctx.TextCenter("PNG", tile, 11.f, 600, th.faint);
        if (ctx.Hit(tile) && input.released) ShowToast("Selected " + saved[i]);
        sx += 68.f;
    }
    if (saved.empty()) ctx.Text("No saved skins yet.", card1.x + 18.f, card1.y + 90.f, 12.5f, 400, th.faint);
    ui.PopClip();
    y += 166.f;
    Rect card2(side.x, y, side.w, 150.f);
    cv.FillRounded(card2, 14.f, th.panel);
    cv.StrokeRounded(card2.inset(0.5f), 13.5f, 1.f, th.border);
    ctx.Text("Actions", card2.x + 18.f, card2.y + 16.f, 14.5f, 600, th.text);
    Rect openSk(card2.x + 18.f, card2.y + 48.f, card2.w - 36.f, 38.f);
    if (ctx.Button("openskins", openSk, "Open skins folder", false, false, false, IconTex("folder-open"))) {
        MakeDirs(DataPath("skins"));
        OpenFolder(DataPath("skins"));
    }
    Rect refresh(card2.x + 18.f, card2.y + 94.f, card2.w - 36.f, 38.f);
    if (ctx.Button("refreshskin", refresh, "Refresh skin", false, false, false, IconTex("refresh"))) {
        if (acc) {
            acc->skinTex = -1;
            acc->hasSkin = false;
            LoadSkin(*acc);
        }
    }
}

void App::TileSkin(Canvas& cv, int tex, const Rect& head, float face) {
    int px = 8;
    float cell = face / (float)px;
    for (int y = 0; y < px; y++) {
        for (int x = 0; x < px; x++) {
            Rect q(head.x + (float)x * cell, head.y + (float)y * cell, cell + 0.5f, cell + 0.5f);
            cv.Image(tex, q, Color(1, 1, 1, 1), (8.f + (float)x) / 64.f, (8.f + (float)y) / 64.f,
                     (9.f + (float)x) / 64.f, (9.f + (float)y) / 64.f);
            cv.Image(tex, q, Color(1, 1, 1, 1), (40.f + (float)x) / 64.f, (8.f + (float)y) / 64.f,
                     (41.f + (float)x) / 64.f, (9.f + (float)y) / 64.f);
        }
    }
}

// ---------------------------------------------------------------- screenshots
void App::DrawScreenshots(const Rect& area) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(1100.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    ctx.Text("Screenshots", page.x, page.y, 24.f, 700, th.text);
    ctx.Text("Pictures saved in each instance's screenshots folder.", page.x, page.y + 34.f, 13.5f, 400, th.dim);

    std::vector<string> shots;
    for (auto& inst : instances) {
        string dir = DataPath("instances/" + inst.name + "/game/screenshots");
        for (auto& f : ListDir(dir)) {
            if (EndsWith(Lower(f), ".png")) shots.push_back(PathJoin(dir, f));
        }
    }
    Rect body(page.x, page.y + 80.f, page.w, page.b() - page.y - 80.f);
    if (shots.empty()) {
        cv.StrokeRounded(body.inset(0.5f), 13.5f, 1.f, th.border);
        int icon = IconTex("image");
        cv.Image(icon, Rect(body.center().x - 15.f, body.center().y - 44.f, 30.f, 30.f), th.faint);
        ctx.TextCenter("No screenshots yet. Press F2 in game to grab one.", Rect(body.x, body.center().y, body.w, 20.f),
                       13.5f, 400, th.dim);
        return;
    }
    ui.PushClip(body);
    float cardW = (body.w - 14.f - 12.f * 2.f) / 3.f;
    float offset = ui.ctx.ScrollArea("shotlist", body, ((shots.size() / 3) + 1) * 230.f, pageScroll[Page::Screenshots]);
    for (size_t i = 0; i < shots.size(); i++) {
        int col = (int)i % 3, row = (int)i / 3;
        Rect card(body.x + (cardW + 12.f) * (float)col, body.y + 230.f * (float)row - offset, cardW, 218.f);
        if (card.b() < body.y || card.y > body.b()) continue;
        cv.FillRounded(card, 13.f, th.panel);
        cv.StrokeRounded(card.inset(0.5f), 12.5f, 1.f, th.border);
        cv.FillRounded(Rect(card.x + 8.f, card.y + 8.f, card.w - 16.f, card.h - 44.f), 9.f, th.panel3);
        int icon = IconTex("image");
        cv.Image(icon, Rect(card.center().x - 14.f, card.y + 70.f, 28.f, 28.f), th.faint);
        ctx.Text(PathFileName(shots[i]), card.x + 10.f, card.b() - 28.f, 11.5f, 400, th.dim, card.w - 20.f);
        if (ctx.Hit(card) && input.released) OpenFolder(PathParent(shots[i]));
    }
    ui.PopClip();
}

// ---------------------------------------------------------------- servers
void App::DrawServers(const Rect& area) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(980.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    ctx.Text("Servers", page.x, page.y, 24.f, 700, th.text);
    ctx.Text("Public Minecraft servers, live player counts. Click one to copy the address.", page.x, page.y + 34.f, 13.5f,
             400, th.dim);
    Rect refresh(page.r() - 120.f, page.y, 120.f, 40.f);
    if (ctx.Button("srvrefresh", refresh, "Refresh", false, false, false, IconTex("refresh"))) {
        serversBusy = false;
        LoadServers();
    }

    Rect body(page.x, page.y + 80.f, page.w, page.b() - page.y - 80.f);
    if (servers.empty()) {
        DrawSpinner(cv, Rect(body.center().x - 11.f, body.center().y - 11.f, 22.f, 22.f), th.accent, (float)Time());
        ctx.TextCenter("Pinging servers…", Rect(body.x, body.center().y + 18.f, body.w, 20.f), 13.f, 400, th.dim);
        return;
    }
    ui.PushClip(body);
    float rowH = 68.f;
    float offset = ui.ctx.ScrollArea("srvlist", body, (rowH + 8.f) * (float)servers.size(), pageScroll[Page::Servers]);
    float y = body.y - offset;
    for (size_t i = 0; i < servers.size(); i++) {
        ServerEntry& s = servers[i];
        Rect row(body.x, y, body.w - 14.f, rowH);
        y += rowH + 8.f;
        if (row.b() < body.y || row.y > body.b()) continue;
        bool hover = ctx.Hit(row);
        cv.FillRounded(row, 12.f, hover ? th.panel2 : th.panel);
        cv.StrokeRounded(row.inset(0.5f), 11.5f, 1.f, hover ? Color::hex(0x313744) : th.border);
        Rect icon(row.x + 12.f, row.y + 12.f, 44.f, 44.f);
        cv.FillRounded(icon, 10.f, th.panel3);
        ctx.TextCenter(string(1, (char)toupper(s.name.empty() ? '?' : s.name[0])), icon, 18.f, 700, th.accent);
        ctx.Text(s.name, icon.r() + 14.f, row.y + 12.f, 15.f, 600, th.text);
        ctx.Text(s.host, icon.r() + 14.f, row.y + 34.f, 12.5f, 400, th.dim);
        Color dot = s.online >= 0 ? th.accent : th.faint;
        cv.FillCircle({row.r() - 130.f, row.center().y}, 4.f, dot);
        string players = s.online >= 0 ? Format("%d/%d", s.online, s.maxPlayers) : "offline";
        ctx.Text(players, row.r() - 116.f, row.center().y - 8.f, 13.f, 600, th.text);
        ctx.TextRight(s.region, Rect(row.x, row.center().y - 8.f, row.w - 16.f, 16.f), 11.5f, 500, th.faint);
        if (hover && input.released) {
            platform->SetClipboard(s.host);
            ShowToast("Copied " + s.host);
        }
    }
    ui.PopClip();
}

// ---------------------------------------------------------------- console
void App::DrawConsole(const Rect& area) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(1100.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    ctx.Text("Console", page.x, page.y, 24.f, 700, th.text);
    ctx.Text("Launcher log and the Minecraft output of this session.", page.x, page.y + 34.f, 13.5f, 400, th.dim);
    Rect openLog(page.r() - 150.f, page.y, 150.f, 40.f);
    if (ctx.Button("openlog", openLog, "Open log file", false, false, false, IconTex("scroll"))) {
        OpenFolder(dataDir);
    }
    Rect clear(page.r() - 150.f - 100.f - 8.f, page.y, 100.f, 40.f);
    if (ctx.Button("clearlog", clear, "Clear", false, false)) consoleText.clear();

    Rect body(page.x, page.y + 80.f, page.w, page.b() - page.y - 80.f);
    cv.FillRounded(body, 14.f, Color::hex(0x07080a));
    cv.StrokeRounded(body.inset(0.5f), 13.5f, 1.f, th.border);
    ui.PushClip(body.inset(16.f));
    // assemble the text: launcher log tail + console buffer
    std::vector<string> lines = LogTail(400);
    TextStyle st;
    st.size = 12.5f;
    st.weight = 400;
    st.color = Color::hex(0xcfd3d9);
    float lh = Fonts().LineHeight(400, 12.5f) * 1.35f;
    std::vector<string> all = lines;
    for (auto& l : Split(consoleText, '\n'))
        if (!l.empty()) all.push_back(l);
    float contentH = lh * (float)all.size() + 8.f;
    float offset = ui.ctx.ScrollArea("console", body.inset(16.f), contentH, pageScroll[Page::Console]);
    if (pageScroll[Page::Console] == 0.f && contentH > body.h) pageScroll[Page::Console] = contentH - body.h + 8.f;
    float y = body.y + 16.f - offset;
    for (auto& line : all) {
        Color col = th.text.with(0.9f);
        if (line.find("ERROR") != string::npos) col = th.danger;
        else if (line.find("[launch]") != string::npos) col = th.accent;
        st.color = col;
        Fonts().Draw(cv, line, st, body.x + 16.f, y, body.w - 40.f);
        y += lh;
    }
    ui.PopClip();
}

}  // namespace crit
