// Crit Launcher Native — Settings page (Game / Java / Appearance / About).
#include "app/pages.h"

namespace crit {

namespace {
struct SettingsPage : PageBase {
    void Draw(App& app) override { app.DrawSettings(app.contentRect); }
};
}  // namespace

std::unique_ptr<PageBase> MakeSettingsPage() { return std::make_unique<SettingsPage>(); }

static void SettingCard(Canvas& cv, const Rect& r, UIContext& ctx, App& app, const string& title, const string& desc,
                        std::function<void(const Rect&)> body) {
    const Theme& th = GetTheme();
    (void)app;
    cv.FillRounded(r, 14.f, th.panel);
    cv.StrokeRounded(r.inset(0.5f), 13.5f, 1.f, th.border);
    ctx.Text(title, r.x + 18.f, r.y + 16.f, 14.5f, 600, th.text);
    ctx.Text(desc, r.x + 18.f, r.y + 38.f, 12.5f, 400, th.dim, r.w - 36.f);
    body(Rect(r.x + 18.f, r.y + 64.f, r.w - 36.f, r.h - 80.f));
}

void App::DrawSettings(const Rect& area) {
    const Theme& th = GetTheme();
    Canvas& cv = render.canvas;
    UIContext& ctx = ui.ctx;
    Rect page(area.x + 28.f, area.y + 28.f, area.w - 56.f, area.h - 56.f);
    float maxW = std::min(980.f, page.w);
    page.x = area.x + (area.w - maxW) * 0.5f;
    page.w = maxW;

    ctx.Text("Settings", page.x, page.y, 24.f, 700, th.text);
    ctx.Text("Tune the launcher, Java and the in-game window.", page.x, page.y + 34.f, 13.5f, 400, th.dim);

    ui.PushClip(page);
    float scroll = ui.ctx.ScrollArea("setpage", page, 1200.f, pageScroll[Page::Settings]);
    float y = page.y + 76.f - scroll;
    float w = page.w - 14.f;
    const char* tabs[] = {"Game", "Java", "Appearance", "About"};
    int tab = std::clamp(settingsTab, 0, 3);
    if (ctx.Segmented("settabs", Rect(page.x, y, 380.f, 44.f), {tabs[0], tabs[1], tabs[2], tabs[3]}, tab)) {
        settingsTab = tab;
    }
    y += 62.f;

    if (settingsTab == 0) {
        // memory + window size
        Rect mem(page.x, y, w, 132.f);
        SettingCard(cv, mem, ctx, *this, "Memory", "RAM given to Minecraft (-Xmx). Levels above 8 GB need 64-bit Java.",
                    [&](const Rect& b) {
                        float value = (float)settings.memoryMb;
                        std::string id = "mem";
                        if (ctx.Slider(id, Rect(b.x, b.y + 6.f, b.w - 140.f, 24.f), value, 512.f, 16384.f, 0)) {
                            settings.memoryMb = (int)value;
                            SaveSettings();
                        }
                        Rect valBox(b.r() - 124.f, b.y, 124.f, 34.f);
                        cv.FillRounded(valBox, 9.f, th.panel2);
                        ctx.TextCenter(Format("%d MB", (int)value), valBox, 13.f, 600, th.text);
                        ctx.Text("512 MB", b.x, b.y + 30.f, 11.f, 400, th.faint);
                        ctx.TextRight("16384 MB", Rect(b.x, b.y + 30.f, b.w - 140.f, 12.f), 11.f, 400, th.faint);
                    });
        y += 148.f;
        Rect win(page.x, y, w, 132.f);
        SettingCard(cv, win, ctx, *this, "Game window", "Leave 0 to let Minecraft pick a size.",
                    [&](const Rect& b) {
                        ctx.Text("Width", b.x, b.y, 12.5f, 500, th.dim);
                        string wv = std::to_string(settings.gameWidth);
                        Rect wf(b.x, b.y + 18.f, 110.f, 36.f);
                        if (ctx.TextField("gamew", wf, wv, "0")) {
                            settings.gameWidth = clampf((float)atoi(wv.c_str()), 0.f, 7680.f);
                            SaveSettings();
                        }
                        ctx.Text("Height", b.x + 130.f, b.y, 12.5f, 500, th.dim);
                        string hv = std::to_string(settings.gameHeight);
                        Rect hf(b.x + 130.f, b.y + 18.f, 110.f, 36.f);
                        if (ctx.TextField("gameh", hf, hv, "0")) {
                            settings.gameHeight = clampf((float)atoi(hv.c_str()), 0.f, 4320.f);
                            SaveSettings();
                        }
                        bool fs = settings.fullscreen;
                        Rect sw(b.x + 270.f, b.y + 18.f, 44.f, 26.f);
                        if (ctx.Switch("fullsw", sw, fs)) {
                            settings.fullscreen = fs;
                            SaveSettings();
                        }
                        ctx.Text("Fullscreen", b.x + 326.f, b.y + 22.f, 13.5f, 500, th.text);
                        bool pat = settings.bgPattern;
                        Rect sw2(b.x, b.y + 66.f, 44.f, 26.f);
                        if (ctx.Switch("patsw", sw2, pat)) {
                            settings.bgPattern = pat;
                            SaveSettings();
                        }
                        ctx.Text("Background pattern", b.x + 56.f, b.y + 70.f, 13.5f, 500, th.text);
                    });
        y += 148.f;
        Rect hb(page.x, y, w, 110.f);
        SettingCard(cv, hb, ctx, *this, "Home background", "The voxel artwork behind the home page.",
                    [&](const Rect& b) {
                        ctx.Text(homeBgLoaded ? "Loaded" : homeBgLoading ? "Downloading…" : "Not downloaded",
                                 b.x, b.y + 6.f, 13.f, 500, homeBgLoaded ? th.accent : th.dim);
                        Rect btn(b.r() - 150.f, b.y, 150.f, 34.f);
                        if (ctx.Button("bgredl", btn, homeBgLoaded ? "Re-download" : "Download", false, true)) {
                            if (homeBgTex >= 0) DeleteFileAt(homeBgPath);
                            homeBgLoaded = false;
                            DownloadBackground();
                        }
                    });
        y += 126.f;
    } else if (settingsTab == 1) {
        Rect java(page.x, y, w, 170.f);
        SettingCard(cv, java, ctx, *this, "Java runtime",
                    "The launcher looks for Java on PATH and in the usual install folders. Leave blank to auto-detect.",
                    [&](const Rect& b) {
                        string shown = settings.javaPath.empty() ? (javaPath.empty() ? "Auto-detect" : javaPath)
                                                                 : settings.javaPath;
                        Rect f(b.x, b.y + 4.f, b.w - 130.f, 40.f);
                        if (ctx.TextField("javapath", f, shown, "C:\\Program Files\\Java\\jdk-17\\bin\\javaw.exe")) {
                            settings.javaPath = shown;
                            SaveSettings();
                        }
                        Rect detect(b.r() - 120.f, b.y + 4.f, 120.f, 40.f);
                        if (ctx.Button("javadetect", detect, "Detect", false, false, false, IconTex("search"))) {
                            javaDetected = false;
                            DetectJava();
                            std::vector<string> found = DetectJavaRuntimes(*this);
                            ShowToast(found.empty() ? "No Java found." : Format("Found %d runtime(s).", (int)found.size()),
                                      found.empty(), !found.empty());
                        }
                        ctx.Text(javaPath.empty() ? "No Java found yet." : "Detected: " + javaPath, b.x, b.y + 54.f, 12.f,
                                 400, th.faint);
                    });
        y += 186.f;
        Rect jvm(page.x, y, w, 150.f);
        SettingCard(cv, jvm, ctx, *this, "JVM arguments", "Extra flags appended to the Java command line.",
                    [&](const Rect& b) {
                        Rect f(b.x, b.y + 4.f, b.w, 40.f);
                        string args = settings.jvmArgs;
                        if (ctx.TextField("jvmargs", f, args, "-XX:+UseG1GC", false, 13.f)) {
                            settings.jvmArgs = args;
                            SaveSettings();
                        }
                        ctx.Text("Memory is controlled by the Game tab and always wins over -Xmx here.", b.x, b.y + 54.f,
                                 12.f, 400, th.faint);
                    });
        y += 166.f;
    } else if (settingsTab == 2) {
        Rect themeCard(page.x, y, w, 118.f);
        SettingCard(cv, themeCard, ctx, *this, "Accent colour", "Used for buttons, highlights and the launch screen.",
                    [&](const Rect& b) {
                        const char* names[] = {"green", "blue", "purple", "red", "orange", "pink", "white"};
                        Color cols[] = {Color::hex(0x4ade80), Color::hex(0x60a5fa), Color::hex(0xa78bfa), Color::hex(0xf05252),
                                        Color::hex(0xfb923c), Color::hex(0xf472b6), Color::hex(0xe8e8ea)};
                        for (int i = 0; i < 7; i++) {
                            Rect sw(b.x + i * 42.f, b.y + 8.f, 30.f, 30.f);
                            if (ctx.ThemeSwatch(string("theme") + std::to_string(i), sw, cols[i], settings.theme == names[i])) {
                                settings.theme = names[i];
                                ApplyTheme();
                                SaveSettings();
                            }
                        }
                    });
        y += 134.f;
        Rect sideCard(page.x, y, w, 118.f);
        SettingCard(cv, sideCard, ctx, *this, "Sidebar", "Compact mode keeps only the icons.",
                    [&](const Rect& b) {
                        bool c = settings.sidebarCollapsed;
                        Rect sw(b.x, b.y + 8.f, 44.f, 26.f);
                        if (ctx.Switch("sidecompact", sw, c)) {
                            settings.sidebarCollapsed = c;
                            SaveSettings();
                        }
                        ctx.Text("Collapse to icons", b.x + 56.f, b.y + 12.f, 13.5f, 500, th.text);
                    });
        y += 134.f;
    } else {
        Rect about(page.x, y, w, 200.f);
        SettingCard(cv, about, ctx, *this, "Crit Launcher 1.8 (native)", "C++17 + DirectX 11 build. Same data folders as the Python launcher.",
                    [&](const Rect& b) {
                        ctx.Text("Windows: " + SystemSummary(), b.x, b.y + 2.f, 12.5f, 400, th.dim);
                        uint64_t total = 0, avail = 0;
                        GetMemoryInfo(total, avail);
                        ctx.Text(Format("Memory: %llu MB available of %llu MB", (unsigned long long)avail,
                                        (unsigned long long)total),
                                 b.x, b.y + 22.f, 12.5f, 400, th.dim);
                        ctx.Text("Data folder: " + dataDir, b.x, b.y + 42.f, 12.5f, 400, th.faint, b.w);
                        Rect open(b.x, b.y + 68.f, 170.f, 34.f);
                        if (ctx.Button("openfolder", open, "Open data folder", false, true, false, IconTex("folder-open"))) {
                            OpenFolder(dataDir);
                        }
                        Rect logs(open.r() + 10.f, open.y, 150.f, 34.f);
                        if (ctx.Button("logs", logs, "View console", false, true, false, IconTex("terminal"))) {
                            Navigate(Page::Console);
                        }
                    });
        y += 216.f;
    }
    ui.PopClip();
}

}  // namespace crit
