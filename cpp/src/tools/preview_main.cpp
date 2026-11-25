// Crit Launcher Native — headless preview: renders real screens to PNG without a GPU.
// Usage: crit_preview <outDir> [width] [height] [page...]
#include <cstdio>
#include <cstring>
#include <memory>

#include "app/pages.h"
#include "render/preview.h"

namespace crit {

// preview-only platform: no window, fixed client size, deterministic clock
class PreviewPlatform : public Platform {
public:
    Vec2 sz{1080, 700};
    bool Create(const WindowConfig& cfg) override {
        sz = Vec2((float)cfg.width, (float)cfg.height);
        return true;
    }
    void Destroy() override {}
    bool PumpMessages(RendererState&) override { return true; }
    void Present() override {}
    void Resize(int w, int h) override { sz = Vec2((float)w, (float)h); }
    Vec2 ClientSize() const override { return sz; }
    float DpiScale() const override { return 1.f; }
    const char* Name() const override { return "preview"; }
    void Minimize() override {}
    void ToggleMaximize() override {}
    bool IsMaximized() const override { return false; }
    void Close() override {}
    void BeginDrag() override {}
    void SetCursor(int) override {}
    void SetIcon(const void*, size_t) override {}
    void SetClipboard(const string&) override {}
};

Platform* g_previewPlatform = nullptr;
Platform* CreatePlatform() {
    g_previewPlatform = new PreviewPlatform();
    return g_previewPlatform;
}
void SetRendererInput(InputState*) {}

}  // namespace crit

using namespace crit;

static string ReadWhole(const string& path) {
    std::vector<uint8_t> d = ReadFileBytes(path);
    return string(d.begin(), d.end());
}

int main(int argc, char** argv) {
    string outDir = argc > 1 ? argv[1] : "/tmp/shots";
    int w = argc > 2 ? atoi(argv[2]) : 1080;
    int h = argc > 3 ? atoi(argv[3]) : 700;
    std::vector<string> which;
    for (int i = 4; i < argc; i++) which.push_back(argv[i]);
    if (which.empty()) which = {"home", "instances", "addons", "skins", "servers", "accounts", "settings", "console", "boot"};

    MakeDirs(outDir);
    setenv("CRIT_LAUNCHER_DATA", "/tmp/crit-preview-data", 1);
    string data = "/tmp/crit-preview-data";
    MakeDirs(data);
    MakeDirs(PathJoin(data, "instances"));
    MakeDirs(PathJoin(data, "instances/Vanilla 1.21/game"));
    MakeDirs(PathJoin(data, "instances/Crit Client Fabric/game"));
    MakeDirs(PathJoin(data, "instances/Modded Forge/game"));
    // seed a couple of instances for realistic screens
    if (!FileExists(PathJoin(data, "instances/Vanilla 1.21/instance.json"))) {
        WriteFileText(PathJoin(data, "instances/Vanilla 1.21/instance.json"),
                      "{\"name\":\"Vanilla 1.21\",\"version\":\"1.21\",\"loader\":\"vanilla\",\"loader_version\":null,"
                      "\"installed\":true,\"schema\":2,\"main_class\":\"net.minecraft.client.main.Main\","
                      "\"asset_index\":\"17\",\"arguments\":null,\"minecraft_arguments\":null,"
                      "\"classpath\":[],\"playtime\":13440,\"icon\":null,\"created\":1730000000}");
        WriteFileText(PathJoin(data, "instances/Crit Client Fabric/instance.json"),
                      "{\"name\":\"Crit Client Fabric\",\"version\":\"1.21.1\",\"loader\":\"fabric\","
                      "\"loader_version\":\"0.16.5\",\"installed\":true,\"schema\":2,\"main_class\":\"net.fabricmc.loader.impl.launch.knot.KnotClient\","
                      "\"asset_index\":\"19\",\"arguments\":null,\"minecraft_arguments\":null,\"classpath\":[],"
                      "\"playtime\":3600,\"icon\":null,\"created\":1730000001}");
        WriteFileText(PathJoin(data, "instances/Modded Forge/instance.json"),
                      "{\"name\":\"Modded Forge\",\"version\":\"1.20.1\",\"loader\":\"forge\",\"loader_version\":\"47.2.0\","
                      "\"installed\":false,\"schema\":2,\"main_class\":null,\"asset_index\":null,\"arguments\":null,"
                      "\"minecraft_arguments\":null,\"classpath\":[],\"playtime\":0,\"icon\":null,\"created\":1730000002}");
        WriteFileText(PathJoin(data, "accounts.json"),
                      "[{\"uuid\":\"a1b2c3d4e5f60718\",\"name\":\"Steve\",\"type\":\"offline\",\"playtime\":7200}]");
        WriteFileText(PathJoin(data, "settings.json"),
                      "{\"active_account\":\"a1b2c3d4e5f60718\",\"active_instance\":\"Vanilla 1.21\","
                      "\"memory_mb\":4096,\"theme\":\"green\",\"agreement_accepted\":true}");
    }

    App app;
    // bind the preview platform before Run() creates its own
    app.platform = nullptr;
    int code = 0;
    {
        // replicate App::Run without the message loop
        Platform* plat = CreatePlatform();
        app.platform = plat;
        app.dpi = 1.f;
        app.exeDir = ExeDir();
        app.dataDir = data;
        LogInit(data, "preview.log");
        app.LoadSettings();
        app.ApplyTheme();
        app.LoadAccounts();
        app.LoadInstances();
        WindowConfig cfg;
        cfg.width = w;
        cfg.height = h;
        app.size = Vec2((float)w, (float)h);
        plat->Create(cfg);
        app.render.canvas.textures = &app.render.textures;
        Fonts().Init(app.render.canvas);
        Icons().Init(app.render.canvas);
        app.ui.app = &app;
        app.fixedTimestep = true;
        app.fixedClock = 1000.0;
        app.startTime = 1000.0 - 3.0;
        // background art for the home shot
        string bg = "/home/user/crit-Cpp/assets/dark_Bg.png";
        if (FileExists(bg)) {
            app.homeBgPath = bg;
            app.homeBgTex = app.LoadImageTexture(bg);
            app.homeBgLoaded = app.homeBgTex >= 0;
        }
        // a representative changelog so the card renders like the real one
        app.changelog.loaded = true;
        app.changelog.title = "Crit Launcher 1.8";
        app.changelog.tag = "v1.8";
        app.changelog.date = "Nov 24, 2026";
        app.changelog.bodyMarkdown =
            "### What's new\n"
            "- **Native port**: the launcher is now a single C++ / DirectX 11 executable.\n"
            "- Faster start-up, smoother animations and lower memory use.\n"
            "- Fabric, Forge and NeoForge installers run in the background with live progress.\n"
            "\n"
            "### Fixes\n"
            "- Skin downloads retry on slow connections.\n"
            "- The launch button no longer waits for the changelog request.\n"
            "- Window size is remembered when the launcher closes.\n"
            "\n"
            "Thanks for using Crit Launcher!";
        app.news.resize(3);
        app.servers = {{"mc.hypixel.net", "Hypixel", "North America", {}, "", 18432, 250000, "1.21", -1},
                       {"play.cubecraft.net", "CubeCraft", "Europe", {}, "", 4210, 30000, "1.21", -1},
                       {"play.wynncraft.com", "Wynncraft", "North America", {}, "", 1280, 8000, "1.20", -1}};
        app.settings.activeAccount = app.accounts.empty() ? "" : app.accounts[0].uuid;
        app.javaPath = "C:\\Program Files\\Java\\jdk-21\\bin\\javaw.exe";

        if (getenv("CRIT_DUMP_BATCHES")) {
            // dump batches for the current canvas at the end of a render
        }
        auto renderPage = [&](Page page, const string& name, int frames, std::function<void(App&)> prep) {
            app.Navigate(page, true);
            app.ui.toasts.clear();
            for (int i = 0; i < frames; i++) {
                NowSeconds();
                app.FrameOnceForPreview();
            }
            if (prep) {
                prep(app);
                app.FrameOnceForPreview();
            }
            RasterTarget target;
            target.w = (int)app.size.x;
            target.h = (int)app.size.y;
            if (getenv("CRIT_DUMP_BATCHES")) {
                int bi = 0;
                int ntex = app.render.textures.Items().size();
                for (int t = 0; t < ntex; t++) {
                    auto* tx = app.render.textures.Get(t);
                    int alpha = 0;
                    if (tx) for (size_t i = 3; i < tx->rgba.size(); i += 4) if (tx->rgba[i]) alpha++;
                    fprintf(stderr, "tex %d: %dx%d dirty=%d alpha=%d\n", t, tx ? tx->w : -1, tx ? tx->h : -1,
                            tx ? (int)tx->dirty : -1, alpha);
                }
                for (auto& b : app.render.canvas.batches) {
                    fprintf(stderr, "batch %d kind=%d shape=%d tex=%d clip=%.0f,%.0f %.0fx%.0f idx=%u first=%u\n",
                            bi++, (int)b.kind, b.shapeType, b.texture, b.clip.x, b.clip.y, b.clip.w, b.clip.h,
                            b.indexCount, b.firstIndex);
                }
            }
            Rasterize(app.render.canvas, target);
            string path = PathJoin(outDir, name + ".png");
            PngWrite(path, target);
            printf("wrote %s (%dx%d)\n", path.c_str(), target.w, target.h);
        };

        if (std::find(which.begin(), which.end(), "home") != which.end()) {
            renderPage(Page::Home, "01-home", 40, nullptr);
        }
        if (std::find(which.begin(), which.end(), "instances") != which.end())
            renderPage(Page::Instances, "02-instances", 30, nullptr);
        if (std::find(which.begin(), which.end(), "addons") != which.end())
            renderPage(Page::Addons, "03-addons", 20, nullptr);
        if (std::find(which.begin(), which.end(), "skins") != which.end())
            renderPage(Page::Skins, "04-skins", 20, nullptr);
        if (std::find(which.begin(), which.end(), "servers") != which.end())
            renderPage(Page::Servers, "05-servers", 20, nullptr);
        if (std::find(which.begin(), which.end(), "accounts") != which.end())
            renderPage(Page::Accounts, "06-accounts", 20, nullptr);
        if (std::find(which.begin(), which.end(), "settings") != which.end())
            renderPage(Page::Settings, "07-settings", 20, nullptr);
        if (std::find(which.begin(), which.end(), "console") != which.end()) {
            app.consoleText = "[17:42:10] [main/INFO]: Loading Minecraft 1.21\n"
                              "[17:42:11] [main/INFO]: Fabric Loader 0.16.5\n"
                              "[17:42:12] [Render thread/INFO]: OpenGL 4.6 initialised\n"
                              "[17:42:13] [Render thread/INFO]: Sound engine started\n";
            renderPage(Page::Console, "08-console", 20, nullptr);
        }
        if (std::find(which.begin(), which.end(), "boot") != which.end()) {
            // boot overlay shot: rewind the clock
            app.startTime = app.fixedClock - 1.35;
            app.Navigate(Page::Home, true);
            app.FrameOnceForPreview();
            RasterTarget target;
            target.w = (int)app.size.x;
            target.h = (int)app.size.y;
            Rasterize(app.render.canvas, target);
            PngWrite(PathJoin(outDir, "00-boot.png"), target);
            printf("wrote 00-boot.png\n");
        }
        if (std::find(which.begin(), which.end(), "modal") != which.end()) {
            app.Navigate(Page::Instances, true);
            app.FrameOnceForPreview();
            app.ui.OpenModal("newinst", "Create instance", "Pick a version and a mod loader.");
            app.newInstanceName = "Skyblock Adventure";
            app.versionList = {"1.21.4", "1.21.1", "1.20.6", "1.20.1", "1.19.2", "1.18.2"};
            app.newLoaderIndex = 1;
            app.fabricLoaders = {"0.16.9", "0.16.5", "0.15.11"};
            app.FrameOnceForPreview();
            app.FrameOnceForPreview();
            RasterTarget target;
            target.w = (int)app.size.x;
            target.h = (int)app.size.y;
            Rasterize(app.render.canvas, target);
            PngWrite(PathJoin(outDir, "09-modal.png"), target);
            printf("wrote 09-modal.png\n");
        }
        if (std::find(which.begin(), which.end(), "compact") != which.end()) {
            app.settings.sidebarCollapsed = true;
            renderPage(Page::Instances, "10-compact", 40, nullptr);
            app.settings.sidebarCollapsed = false;
        }
    }
    return code;
}
