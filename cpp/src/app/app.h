// Crit Launcher Native — application object: state, pages and the frame loop.
#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "platform/platform.h"
#include "ui/ui.h"

namespace crit {

struct Account {
    string uuid;
    string accessToken;
    string refreshToken;
    uint64_t expiresAt = 0;
    string name;
    string type = "offline";  // microsoft | offline | ely
    string mcToken;
    string mcUuid;
    string skinBase64;        // raw skin png (cached)
    int skinTex = -1;         // texture id in the current registry
    int64_t playtime = 0;
    bool hasSkin = false;
};

struct Instance {
    string name;
    string version;
    string loader;            // vanilla | fabric | forge | neoforge
    string loaderVersion;
    bool installed = false;
    string mainClass;
    string assetIndex;
    std::vector<string> classpath;
    Json arguments;
    string minecraftArguments;
    int64_t playtime = 0;
    double created = 0;
    string icon;
    int iconTex = -1;
};

struct Settings {
    string activeAccount;
    string activeInstance;
    int memoryMb = 2048;
    int minMemoryMb = 0;
    string javaPath;
    string jvmArgs;
    int gameWidth = 0;
    int gameHeight = 0;
    bool fullscreen = false;
    string theme = "green";
    bool sidebarCollapsed = false;
    bool bgPattern = true;
    bool agreementAccepted = false;
    int launcherWidth = 0;
    int launcherHeight = 0;
    bool discordPresence = false;
    string colorMode = "dark";
    bool reduceMotion = false;
};

struct ChangelogEntry {
    bool loading = false;
    bool loaded = false;
    string error;
    string title;
    string tag;
    string date;
    string bodyMarkdown;
};

struct NewsItem {
    string title;
    string dateText;
    string category;
    string imageUrl;
    string body;
    int imageTex = -1;
    bool imageRequested = false;
    bool imageFailed = false;
};

struct ServerEntry {
    string host;
    string name;
    string region;
    std::vector<string> tags;
    string motd;
    int online = -1;
    int maxPlayers = -1;
    string version;
    int iconTex = -1;
};

struct ProgressState {
    bool active = false;
    string task;
    string instance;
    float fraction = 0.f;
    double startedAt = 0;
};

// ---------------------------------------------------------------- page enum
enum class Page { Home, Instances, Addons, Skins, Screenshots, Servers, Accounts, Settings, Console };

// ---------------------------------------------------------------- pages
struct PageBase {
    virtual ~PageBase() = default;
    virtual void Update(App& app) {}
    virtual void Draw(App& app) = 0;
};

struct Sidebar {
    Rect bounds;
    Rect brand;
    Rect nav[6];
    Rect navBottom[2];
    Rect stats;
    Rect avatar;
    Rect collapseBtn;
};

class App {
public:
    App();
    ~App();

    int Run();
    void Frame();
    // one frame without the OS loop (headless preview tool)
    void FrameOnceForPreview() { Frame(); }

    // ---------------- state
    Platform* platform = nullptr;
    UIRoot ui;
    InputState input;
    RendererState render;
    float dpi = 1.f;
    Vec2 size{1080, 700};
    string dataDir;
    string exeDir;
    bool running = true;
    bool firstFrame = true;
    double startTime = 0.0;
    double lastFrame = 0.0;
    float frameTime = 0.016f;
    // headless preview: advance a synthetic clock at a fixed rate instead of wall time
    bool fixedTimestep = false;
    double fixedClock = 0.0;
    // clock the UI animates against (synthetic in the preview tool)
    double Time() const { return fixedTimestep ? fixedClock : NowSeconds(); }
    int frameCount = 0;
    double fpsClock = 0.0;
    int fpsFrames = 0;
    float fps = 60.f;

    Settings settings;
    std::vector<Account> accounts;
    std::vector<Instance> instances;
    Page page = Page::Home;
    std::vector<Page> pageHistory;
    int historyIndex = -1;
    std::map<Page, std::unique_ptr<PageBase>> pages;
    std::map<Page, float> pageScroll;
    bool sidebarCollapsed = false;
    float sidebarAnim = 0.f;
    ChangelogEntry changelog;
    std::vector<NewsItem> news;
    std::vector<ServerEntry> servers;
    ProgressState progress;
    std::vector<string> runningInstances;
    std::vector<string> installingInstances;
    std::vector<string> versionList;
    std::vector<string> fabricLoaders;
    std::vector<string> forgeVersions;
    std::vector<string> neoforgeVersions;
    bool versionsLoaded = false;
    bool versionsLoading = false;
    string homeBgPath;
    int homeBgTex = -1;
    bool homeBgLoading = false;
    bool homeBgLoaded = false;
    double bootDone = 0.0;
    bool bootShown = true;
    string consoleText;
    float consoleScroll = 0.f;

    // ---------------- persistence
    string DataPath(const string& rel) const { return PathJoin(dataDir, rel); }
    void LoadSettings();
    void SaveSettings();
    void LoadAccounts();
    void SaveAccounts();
    void LoadInstances();
    void SaveInstance(Instance& inst);
    Account* ActiveAccount();
    Instance* ActiveInstance();
    Instance* FindInstance(const string& name);
    Account* FindAccount(const string& key);
    void SetActiveInstance(const string& name);

    // ---------------- navigation
    void Navigate(Page p, bool fromHistory = false);
    void GoBack();
    void GoForward();
    string PageTitle(Page p) const;
    int PageIcon(Page p) const;
    PageBase* CurrentPage();

    // ---------------- actions
    void Launch(Instance& inst);
    void StopInstance(const string& name);
    void CreateInstance(const string& name, const string& version, const string& loader, const string& loaderVersion);
    void DeleteInstance(const string& name);
    void ShowToast(const string& msg, bool error = false, bool success = false);
    void OpenUrl(const string& url);

    // ---------------- async helpers (run on worker threads, marshal to UI thread)
    void LoadChangelog(bool force = false);
    void LoadNews();
    void LoadServers();
    void DownloadBackground();
    void LoadVersionLists();
    void LoadSkin(Account& acc);

    // ---------------- drawing entry points
    void DrawSidebar(const Rect& r);
    void DrawTopBar(const Rect& r);
    void DrawHome(const Rect& r);
    void DrawInstances(const Rect& r);
    void DrawAddons(const Rect& r);
    void DrawSkins(const Rect& r);
    void DrawScreenshots(const Rect& r);
    void DrawServers(const Rect& r);
    void DrawAccounts(const Rect& r);
    void DrawSettings(const Rect& r);
    void DrawConsole(const Rect& r);
    void DrawOverlays();
    void DrawBoot();
    void DrawCreateInstanceModal();
    void DrawAccountModal();
    void TileSkin(Canvas& cv, int tex, const Rect& head, float face);

    // ---------------- textures
    int RegisterTexture(std::shared_ptr<Texture> tex);
    int LoadImageTexture(const string& path, bool keepCpu = false);
    Texture* Tex(int id);

    // ---------------- helpers
    void Layout();
    string Greeting() const;
    void PushConsole(const string& line);
    void RefreshInstanceIcon(Instance& inst);
    void ApplyTheme();
    bool IsInstanceRunning(const string& name) const;
    bool IsInstanceInstalling(const string& name) const;

    // layout rectangles rebuilt each frame
    Sidebar sidebar;
    Rect contentRect;
    Rect topbarRect;
    Rect sidebarProgressBar;
    bool showSidebarProgress = false;

    // internal flags
    bool loadChangelogLater_ = false;
    bool quitRequested_ = false;
    Vec2 lastMouse_{-1.f, -1.f};
    double bootStart_ = 0.0;
    int homeTab_ = 0;

    // create-instance modal state
    string newInstanceName;
    int newVersionIndex = 0;
    int newLoaderIndex = 0;
    int newLoaderVersionIndex = 0;
    bool newCritClient = false;
    string newInstanceError;

    // account modal state
    string offlineName;
    bool acctModalOpen = false;

    // worker guards
    std::atomic<bool> changelogBusy{false};
    std::atomic<bool> newsBusy{false};
    std::atomic<bool> serversBusy{false};
    std::atomic<bool> versionsBusy{false};
    std::atomic<bool> bgBusy{false};
    std::vector<std::thread> workers;

    // java detection
    string javaPath;
    bool javaDetected = false;
    void DetectJava();

    // launch timer for playtime
    std::map<string, double> launchStartTimes;

    // page-local view state
    int addonsTab = 0;          // 0 = mods, 1 = resource packs, 2 = shaders, 3 = modpacks
    string addonsSearch;
    int addonsSource = 0;       // 0 modrinth, 1 curseforge
    string screenshotsInstance;
    bool accountsAdding = false;
    int settingsTab = 0;
    float memoryValue = 2048.f;
    std::map<string, string> pendingFields;

    // drag and drop of skin files (Windows only, via WM_DROPFILES)
    string droppedFile;
};

// platform.cpp helpers
void SetRendererInput(InputState* input);

// icon names used by pages
int IconTex(const char* name);

// shared drawing helpers used by several pages
void DrawCard(Canvas& cv, const Rect& r, float radius = 14.f);
void DrawSectionTitle(UIContext& ctx, const string& text, float x, float y);
void DrawAccountChip(UIContext& ctx, App& app, const Rect& r);

}  // namespace crit
