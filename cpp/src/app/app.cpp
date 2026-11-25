// Crit Launcher Native — application lifecycle, state, navigation.
#include "app/app.h"

#include "app/pages.h"
#include "core/win_icons.h"
#include "render/preview.h"

namespace crit {

int IconTex(const char* name) { return Icons().Get(name); }

int App::RegisterTexture(std::shared_ptr<Texture> tex) {
    if (!tex) return -1;
    // upload immediately on Windows so the first frame shows it
    GpuUploadTexture(*tex);
    return render.textures.Add(std::move(tex));
}

Texture* App::Tex(int id) { return render.textures.Get(id); }

int App::LoadImageTexture(const string& path, bool keepCpu) {
    (void)keepCpu;
    std::vector<uint8_t> bytes = ReadFileBytes(path);
    if (bytes.empty()) return -1;
    RasterTarget img;
    // PNG only (the bundled art is PNG); JPEG would need a decoder we don't ship.
    if (bytes.size() > 4 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        string tmp = DataPath("tmp_decode.png");
        MakeDirs(PathParent(tmp));
        if (!WriteFileBytes(tmp, bytes)) return -1;
        if (!PngRead(tmp, img)) return -1;
        DeleteFileAt(tmp);
    } else {
        return -1;
    }
    auto tex = std::make_shared<Texture>();
    tex->w = img.w;
    tex->h = img.h;
    tex->rgba = std::move(img.rgba);
    return RegisterTexture(std::move(tex));
}

App::App() {
    pages[Page::Home] = MakeHomePage();
    pages[Page::Instances] = MakeInstancesPage();
    pages[Page::Addons] = MakeAddonsPage();
    pages[Page::Skins] = MakeSkinsPage();
    pages[Page::Screenshots] = MakeScreenshotsPage();
    pages[Page::Servers] = MakeServersPage();
    pages[Page::Accounts] = MakeAccountsPage();
    pages[Page::Settings] = MakeSettingsPage();
    pages[Page::Console] = MakeConsolePage();
}

App::~App() {
    for (auto& t : workers) {
        if (t.joinable()) t.detach();
    }
}

// ---------------------------------------------------------------- persistence
void App::LoadSettings() {
    Json j = Json::parse(ReadFileText(DataPath("settings.json")));
    if (!j.isObject()) return;
    settings.activeAccount = j.getString("active_account");
    settings.activeInstance = j.getString("active_instance");
    settings.memoryMb = (int)j.getNumber("memory_mb", 2048);
    settings.minMemoryMb = (int)j.getNumber("min_memory_mb", 0);
    settings.javaPath = j.getString("java_path");
    settings.jvmArgs = j.getString("jvm_args");
    settings.gameWidth = (int)j.getNumber("game_width", 0);
    settings.gameHeight = (int)j.getNumber("game_height", 0);
    settings.fullscreen = j.getBool("fullscreen");
    settings.theme = j.getString("theme", "green");
    settings.sidebarCollapsed = j.getBool("sidebar_collapsed");
    settings.bgPattern = j.getBool("bg_pattern", true);
    settings.agreementAccepted = j.getBool("agreement_accepted");
    settings.launcherWidth = (int)j.getNumber("launcher_width", 0);
    settings.launcherHeight = (int)j.getNumber("launcher_height", 0);
    settings.discordPresence = j.getBool("discord_presence");
    settings.colorMode = j.getString("color_mode", "dark");
    settings.reduceMotion = j.getBool("reduce_motion");
}

void App::SaveSettings() {
    Json j = Json::object();
    j.set("active_account", Json::str_(settings.activeAccount));
    j.set("active_instance", Json::str_(settings.activeInstance));
    j.set("memory_mb", Json::num_(settings.memoryMb));
    j.set("min_memory_mb", Json::num_(settings.minMemoryMb));
    j.set("java_path", Json::str_(settings.javaPath));
    j.set("jvm_args", Json::str_(settings.jvmArgs));
    j.set("game_width", Json::num_(settings.gameWidth));
    j.set("game_height", Json::num_(settings.gameHeight));
    j.set("fullscreen", Json::bool_(settings.fullscreen));
    j.set("theme", Json::str_(settings.theme));
    j.set("sidebar_collapsed", Json::bool_(settings.sidebarCollapsed));
    j.set("bg_pattern", Json::bool_(settings.bgPattern));
    j.set("agreement_accepted", Json::bool_(settings.agreementAccepted));
    j.set("launcher_width", Json::num_(settings.launcherWidth));
    j.set("launcher_height", Json::num_(settings.launcherHeight));
    j.set("discord_presence", Json::bool_(settings.discordPresence));
    j.set("color_mode", Json::str_(settings.colorMode));
    j.set("reduce_motion", Json::bool_(settings.reduceMotion));
    WriteFileText(DataPath("settings.json"), j.dump(true));
}

void App::LoadAccounts() {
    accounts.clear();
    Json j = Json::parse(ReadFileText(DataPath("accounts.json")));
    if (!j.isArray()) return;
    for (size_t i = 0; i < j.arr.size(); i++) {
        const Json& a = j.arr[i];
        Account acc;
        acc.uuid = a.getString("uuid");
        acc.name = a.getString("name");
        acc.type = a.getString("type", "offline");
        acc.mcToken = a.getString("mc_token");
        acc.mcUuid = a.getString("mc_uuid");
        acc.skinBase64 = a.getString("skin_png");
        acc.playtime = (int64_t)a.getNumber("playtime", 0);
        if (!acc.name.empty()) accounts.push_back(acc);
    }
    Log("loaded %d account(s)", (int)accounts.size());
}

void App::SaveAccounts() {
    Json arr = Json::array();
    for (auto& a : accounts) {
        Json o = Json::object();
        o.set("uuid", Json::str_(a.uuid));
        o.set("name", Json::str_(a.name));
        o.set("type", Json::str_(a.type));
        if (!a.mcToken.empty()) o.set("mc_token", Json::str_(a.mcToken));
        if (!a.mcUuid.empty()) o.set("mc_uuid", Json::str_(a.mcUuid));
        if (!a.skinBase64.empty()) o.set("skin_png", Json::str_(a.skinBase64));
        o.set("playtime", Json::num_((double)a.playtime));
        arr.push(o);
    }
    WriteFileText(DataPath("accounts.json"), arr.dump(true));
}

void App::LoadInstances() {
    instances.clear();
    string dir = DataPath("instances");
    MakeDirs(dir);
    for (auto& name : ListDir(dir)) {
        string metaPath = PathJoin(PathJoin(dir, name), "instance.json");
        if (!FileExists(metaPath)) continue;
        Json j = Json::parse(ReadFileText(metaPath));
        if (!j.isObject()) continue;
        Instance inst;
        inst.name = j.getString("name", name);
        inst.version = j.getString("version");
        inst.loader = j.getString("loader", "vanilla");
        inst.loaderVersion = j.getString("loader_version");
        inst.installed = j.getBool("installed");
        inst.mainClass = j.getString("main_class");
        inst.assetIndex = j.getString("asset_index");
        inst.minecraftArguments = j.getString("minecraft_arguments");
        inst.arguments = j["arguments"];
        for (auto& cp : j["classpath"].arr) inst.classpath.push_back(cp.str);
        inst.playtime = (int64_t)j.getNumber("playtime", 0);
        inst.created = j.getNumber("created", 0);
        inst.icon = j.getString("icon");
        instances.push_back(inst);
    }
    std::sort(instances.begin(), instances.end(), [](const Instance& a, const Instance& b) { return Lower(a.name) < Lower(b.name); });
    Log("loaded %d instance(s)", (int)instances.size());
}

void App::SaveInstance(Instance& inst) {
    Json j = Json::object();
    j.set("name", Json::str_(inst.name));
    j.set("version", Json::str_(inst.version));
    j.set("loader", Json::str_(inst.loader));
    j.set("loader_version", inst.loaderVersion.empty() ? Json() : Json::str_(inst.loaderVersion));
    j.set("installed", Json::bool_(inst.installed));
    j.set("schema", Json::num_(2));
    j.set("main_class", inst.mainClass.empty() ? Json() : Json::str_(inst.mainClass));
    j.set("asset_index", inst.assetIndex.empty() ? Json() : Json::str_(inst.assetIndex));
    j.set("arguments", inst.arguments);
    j.set("minecraft_arguments", inst.minecraftArguments.empty() ? Json() : Json::str_(inst.minecraftArguments));
    Json cp = Json::array();
    for (auto& c : inst.classpath) cp.push(Json::str_(c));
    j.set("classpath", cp);
    j.set("playtime", Json::num_((double)inst.playtime));
    j.set("icon", inst.icon.empty() ? Json() : Json::str_(inst.icon));
    j.set("created", Json::num_(inst.created));
    string dir = PathJoin(DataPath("instances"), inst.name);
    MakeDirs(dir);
    WriteFileText(PathJoin(dir, "instance.json"), j.dump(true));
}

Account* App::ActiveAccount() { return FindAccount(settings.activeAccount); }

Account* App::FindAccount(const string& key) {
    if (key.empty()) return nullptr;
    for (auto& a : accounts)
        if (a.uuid == key) return &a;
    for (auto& a : accounts)
        if (a.name == key) return &a;
    return nullptr;
}

Instance* App::ActiveInstance() {
    Instance* inst = FindInstance(settings.activeInstance);
    if (inst) return inst;
    if (!instances.empty()) {
        settings.activeInstance = instances[0].name;
        return &instances[0];
    }
    return nullptr;
}

Instance* App::FindInstance(const string& name) {
    for (auto& i : instances)
        if (i.name == name) return &i;
    return nullptr;
}

void App::SetActiveInstance(const string& name) {
    settings.activeInstance = name;
    SaveSettings();
}

bool App::IsInstanceRunning(const string& name) const {
    return std::find(runningInstances.begin(), runningInstances.end(), name) != runningInstances.end();
}

bool App::IsInstanceInstalling(const string& name) const {
    return std::find(installingInstances.begin(), installingInstances.end(), name) != installingInstances.end();
}

// ---------------------------------------------------------------- navigation
string App::PageTitle(Page p) const {
    switch (p) {
        case Page::Home: return "Home";
        case Page::Instances: return "Instances";
        case Page::Addons: return "Addons";
        case Page::Skins: return "Skins";
        case Page::Screenshots: return "Screenshots";
        case Page::Servers: return "Servers";
        case Page::Accounts: return "Accounts";
        case Page::Settings: return "Settings";
        case Page::Console: return "Console";
    }
    return "";
}

int App::PageIcon(Page p) const {
    switch (p) {
        case Page::Home: return IconTex("home");
        case Page::Instances: return IconTex("box");
        case Page::Addons: return IconTex("puzzle");
        case Page::Skins: return IconTex("shirt");
        case Page::Screenshots: return IconTex("image");
        case Page::Servers: return IconTex("server");
        case Page::Accounts: return IconTex("user");
        case Page::Settings: return IconTex("settings");
        case Page::Console: return IconTex("terminal");
    }
    return -1;
}

void App::Navigate(Page p, bool fromHistory) {
    if (!fromHistory && (pageHistory.empty() || pageHistory[historyIndex] != p)) {
        pageHistory.resize((size_t)historyIndex + 1);
        pageHistory.push_back(p);
        historyIndex = (int)pageHistory.size() - 1;
    }
    page = p;
    ui.CloseDropdown();
    ui.Blur();
}

void App::GoBack() {
    if (historyIndex > 0) {
        historyIndex--;
        Navigate(pageHistory[(size_t)historyIndex], true);
    }
}

void App::GoForward() {
    if (historyIndex + 1 < (int)pageHistory.size()) {
        historyIndex++;
        Navigate(pageHistory[(size_t)historyIndex], true);
    }
}

PageBase* App::CurrentPage() {
    auto it = pages.find(page);
    return it == pages.end() ? nullptr : it->second.get();
}

void App::ApplyTheme() { SetThemeByName(settings.theme); }

// ---------------------------------------------------------------- toasts / utils
void App::ShowToast(const string& msg, bool error, bool success) { ui.Toast_(msg, error, success); }

void App::OpenUrl(const string& url) { crit::OpenUrl(url); }

void App::PushConsole(const string& line) {
    consoleText += line;
    if (!line.empty() && line.back() != '\n') consoleText += '\n';
    if (consoleText.size() > 240000) consoleText = consoleText.substr(consoleText.size() - 200000);
}

string App::Greeting() const {
    Account* acc = const_cast<App*>(this)->ActiveAccount();
    const char* greets[] = {"Sneak 100 activated", "Ready to mine", "Adventure awaits", "The nether is calling", "Diamonds are near"};
    if (acc) return string(greets[acc->name.size() % 5]) + ", " + acc->name;
    return "Welcome to Crit Launcher";
}

void App::RefreshInstanceIcon(Instance& inst) {
    (void)inst;
}

void App::DetectJava() {
    if (javaDetected) return;
    javaDetected = true;
    for (auto& t : workers) {
        if (t.joinable()) t.detach();
    }
    workers.clear();
    workers.emplace_back([this] {
        string out;
        if (RunCapture("java", {"-version"}, out, 5000)) {
            std::function<void()> fn = [this, out] {
                javaPath = "java";
                Log("java detected: %s", Trim(out).c_str());
            };
            platform->Post(fn);
        }
    });
}

// ---------------------------------------------------------------- actions
void App::Launch(Instance& inst) {
    if (IsInstanceInstalling(inst.name)) {
        ShowToast("This instance is still installing.", true);
        return;
    }
    if (IsInstanceRunning(inst.name)) {
        StopInstance(inst.name);
        return;
    }
    if (!inst.installed) {
        ShowToast("Installing " + inst.name + "…");
        StartInstall(*this, inst.name);
        return;
    }
    StartLaunch(*this, inst.name);
}

void App::StopInstance(const string& name) {
    StopInstanceProcess(*this, name);
}

void App::CreateInstance(const string& name, const string& version, const string& loader, const string& loaderVersion) {
    newInstanceError.clear();
    if (Trim(name).empty()) {
        newInstanceError = "Give the instance a name.";
        return;
    }
    for (char c : name) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' ||
            (unsigned char)c < 32) {
            newInstanceError = "Instance name cannot contain: < > : \" / \\ | ? *";
            return;
        }
    }
    if (FindInstance(name)) {
        newInstanceError = "An instance with that name already exists.";
        return;
    }
    Instance inst;
    inst.name = name;
    inst.version = version;
    inst.loader = loader;
    inst.loaderVersion = loaderVersion;
    inst.created = (double)UnixMillis() / 1000.0;
    instances.push_back(inst);
    std::sort(instances.begin(), instances.end(),
              [](const Instance& a, const Instance& b) { return Lower(a.name) < Lower(b.name); });
    SaveInstance(*FindInstance(name));
    settings.activeInstance = name;
    SaveSettings();
    ui.CloseModal();
    ShowToast("Created " + name + ". Installing…", false, true);
    StartInstall(*this, name);
}

void App::DeleteInstance(const string& name) {
    RemoveDirRecursive(PathJoin(DataPath("instances"), name));
    instances.erase(std::remove_if(instances.begin(), instances.end(), [&](const Instance& i) { return i.name == name; }),
                    instances.end());
    if (settings.activeInstance == name) {
        settings.activeInstance = instances.empty() ? "" : instances[0].name;
        SaveSettings();
    }
    ShowToast("Deleted " + name);
}

// ---------------------------------------------------------------- remote data
void App::LoadChangelog(bool force) {
    (void)force;
    if (changelogBusy.exchange(true)) return;
    changelog.loading = true;
    workers.emplace_back([this] {
        HttpResult r = HttpGet("https://api.github.com/repos/snigglaberry/crit-launcher/releases/latest",
                               {{"Accept", "application/vnd.github+json"}});
        ChangelogEntry entry;
        if (r.ok) {
            Json j = Json::parse(r.body);
            entry.title = j.getString("name", j.getString("tag_name", "Latest release"));
            entry.tag = j.getString("tag_name");
            entry.bodyMarkdown = j.getString("body");
            entry.date = j.getString("published_at");
            if (entry.date.size() >= 10) {
                int y = atoi(entry.date.substr(0, 4).c_str());
                int m = atoi(entry.date.substr(5, 2).c_str());
                int d = atoi(entry.date.substr(8, 2).c_str());
                static const char* mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                entry.date = Format("%s %d, %d", mon[(m - 1) % 12], d, y);
            }
            entry.loaded = true;
        } else {
            entry.error = r.error.empty() ? "Couldn't load the changelog." : r.error;
        }
        std::function<void()> fn = [this, entry] {
            changelog = entry;
            changelog.loading = false;
            changelogBusy = false;
        };
        platform->Post(fn);
    });
}

void App::LoadNews() {
    if (newsBusy.exchange(true)) return;
    workers.emplace_back([this] {
        HttpResult r = HttpGet("https://launchercontent.mojang.com/v2/javaPatchNotes.json");
        std::vector<NewsItem> items;
        if (r.ok) {
            Json j = Json::parse(r.body);
            for (auto& e : j["entries"].arr) {
                NewsItem n;
                n.title = DecodeEntities(e.getString("title"));
                n.category = e.getString("category", "patch_notes");
                n.body = DecodeEntities(e.getString("body")).substr(0, 220);
                string img = e["image"].getString("url");
                if (!img.empty()) n.imageUrl = "https://launchercontent.mojang.com" + img;
                string d = e.getString("date");
                if (d.size() >= 10) {
                    int y = atoi(d.substr(0, 4).c_str());
                    int m = atoi(d.substr(5, 2).c_str());
                    int dd = atoi(d.substr(8, 2).c_str());
                    static const char* mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                    n.dateText = Format("%s %d, %d", mon[(m - 1) % 12], dd, y);
                }
                items.push_back(n);
                if (items.size() >= 8) break;
            }
        }
        std::function<void()> fn = [this, items] {
            news = items;
            newsBusy = false;
        };
        platform->Post(fn);
    });
}

void App::LoadServers() {
    if (serversBusy.exchange(true)) return;
    workers.emplace_back([this] {
        struct Def { const char* host; const char* name; const char* region; };
        static const Def defs[] = {
            {"mc.hypixel.net", "Hypixel", "North America"},
            {"play.cubecraft.net", "CubeCraft", "Europe"},
            {"play.wynncraft.com", "Wynncraft", "North America"},
            {"play.pika-network.net", "PikaNetwork", "North America"},
            {"play.thearchon.net", "The Archon", "North America"},
            {"gommehd.net", "GommeHD", "Europe"},
            {"2b2t.org", "2b2t", "North America"},
            {"play.snapcraft.net", "Snapcraft", "North America"},
        };
        std::vector<ServerEntry> out;
        for (auto& d : defs) {
            ServerEntry s;
            s.host = d.host;
            s.name = d.name;
            s.region = d.region;
            HttpResult r = HttpGet("https://api.mcsrvstat.us/3/" + string(d.host));
            if (r.ok) {
                Json j = Json::parse(r.body);
                s.online = (int)j["players"].getNumber("online", 0);
                s.maxPlayers = (int)j["players"].getNumber("max", 0);
                s.version = j.getString("version");
                s.motd = j["motd"].isObject() ? j["motd"]["clean"][0].getString("", "") : "";
            }
            out.push_back(s);
        }
        std::sort(out.begin(), out.end(), [](const ServerEntry& a, const ServerEntry& b) {
            return a.online > b.online;
        });
        std::function<void()> fn = [this, out] {
            servers = out;
            serversBusy = false;
        };
        platform->Post(fn);
    });
}

void App::DownloadBackground() {
    if (bgBusy.exchange(true)) return;
    homeBgLoading = true;
    workers.emplace_back([this] {
        string dir = DataPath("ui_assets");
        MakeDirs(dir);
        string dest = PathJoin(dir, "dark_Bg.png");
        bool ok = FileExists(dest) && FileSize(dest) > 100000;
        if (!ok) {
            // local copy first (assets/dark_Bg.png next to the exe or the repo)
            std::vector<string> candidates = {PathJoin(exeDir, "assets\\dark_Bg.png"),
                                              PathJoin(PathJoin(exeDir, ".."), "assets\\dark_Bg.png")};
            for (auto& c : candidates) {
                if (FileExists(c)) {
                    WriteFileBytes(dest, ReadFileBytes(c));
                    ok = true;
                    break;
                }
            }
        }
        if (!ok) {
            HttpResult r = HttpGetBytes("https://raw.githubusercontent.com/snigglaberry/crit-launcher/main/assets/dark_Bg.png");
            if (r.ok && !r.bytes.empty()) ok = WriteFileBytes(dest, r.bytes);
        }
        std::function<void()> fn = [this, ok, dest] {
            homeBgLoading = false;
            bgBusy = false;
            if (ok) {
                homeBgPath = dest;
                homeBgTex = LoadImageTexture(dest);
                homeBgLoaded = homeBgTex >= 0;
                Log("home background loaded (%d)", homeBgTex);
            } else {
                LogError("background download failed");
            }
        };
        platform->Post(fn);
    });
}

void App::LoadVersionLists() {
    if (versionsBusy.exchange(true)) return;
    versionsLoading = true;
    workers.emplace_back([this] {
        std::vector<string> releases;
        HttpResult r = HttpGet("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
        if (r.ok) {
            Json j = Json::parse(r.body);
            for (auto& v : j["versions"].arr) {
                if (v.getString("type") == "release") releases.push_back(v.getString("id"));
                if (releases.size() >= 40) break;
            }
        }
        std::function<void()> fn = [this, releases] {
            versionList = releases;
            versionsLoaded = !releases.empty();
            versionsLoading = false;
            versionsBusy = false;
            if (releases.empty()) ShowToast("Couldn't load the Minecraft version list.", true);
        };
        platform->Post(fn);
    });
}

void App::LoadSkin(Account& acc) {
    // Offline/microsoft skins are fetched by name from mc-heads (PNG 64x64).
    if (acc.name.empty() || acc.skinTex >= 0) return;
    string name = acc.name;
    workers.emplace_back([this, name] {
        std::vector<uint8_t> bytes;
        HttpResult r = HttpGetBytes("https://mc-heads.net/skin/" + UrlEncode(name));
        if (r.ok) bytes = r.bytes;
        if (bytes.empty()) {
            HttpResult r2 = HttpGetBytes("https://minotar.net/skin/" + UrlEncode(name));
            if (r2.ok) bytes = r2.bytes;
        }
        std::function<void()> fn = [this, name, bytes] {
            Account* a = FindAccount(name);
            if (!a || bytes.empty()) return;
            string tmp = DataPath("tmp_skin.png");
            WriteFileBytes(tmp, bytes);
            RasterTarget img;
            if (PngRead(tmp, img)) {
                auto tex = std::make_shared<Texture>();
                tex->w = img.w;
                tex->h = img.h;
                tex->rgba = std::move(img.rgba);
                a->skinTex = RegisterTexture(std::move(tex));
                a->hasSkin = true;
            }
            DeleteFileAt(tmp);
        };
        platform->Post(fn);
    });
}

// ---------------------------------------------------------------- run loop
int App::Run() {
    Platform* plat = CreatePlatform();
    platform = plat;
    dpi = plat->DpiScale();
    exeDir = ExeDir();
    dataDir = AppDataDir();
    MakeDirs(dataDir);
    LogInit(dataDir, "launcher.log");
    Log("Crit Launcher Native starting (%s)", plat->Name());

    LoadSettings();
    ApplyTheme();
    LoadAccounts();
    LoadInstances();
    if (!instances.empty() && settings.activeInstance.empty()) settings.activeInstance = instances[0].name;

    WindowConfig cfg;
    cfg.width = settings.launcherWidth > 0 ? settings.launcherWidth : 1080;
    cfg.height = settings.launcherHeight > 0 ? settings.launcherHeight : 700;
    size = Vec2((float)cfg.width, (float)cfg.height);
    if (!plat->Create(cfg)) {
        LogError("window creation failed");
        return 1;
    }
    // window icon from the bundled .ico
    plat->SetIcon(kIconIco, kIconIcoSize);
    dpi = plat->DpiScale();
    render.canvas.textures = &render.textures;
    Fonts().Init(render.canvas);
    Icons().Init(render.canvas);
    SetRendererInput(&input);
    ui.app = this;

    Navigate(Page::Home, true);
    startTime = Time();
    loadChangelogLater_ = true;

    // background work
    LoadChangelog();
    LoadNews();
    LoadServers();
    DownloadBackground();
    DetectJava();
    for (auto& a : accounts)
        if (a.skinTex < 0 && !a.name.empty()) LoadSkin(a);

    lastFrame = NowSeconds();
    while (running) {
        if (!plat->PumpMessages(render)) break;
        plat->RunPosted();
        Frame();
        if (quitRequested_) break;
    }
    SaveSettings();
    Log("shutting down");
    return 0;
}

void App::Frame() {
    double now = fixedTimestep ? (fixedClock += 1.0 / 60.0) : NowSeconds();
    float dt = fixedTimestep ? 1.0f / 60.0f : (float)std::min(0.1, now - lastFrame);
    lastFrame = now;
    frameTime = dt;
    frameCount++;
    fpsFrames++;
    if (now - fpsClock > 0.5) {
        fps = (float)fpsFrames / (float)(now - fpsClock);
        fpsClock = now;
        fpsFrames = 0;
    }
    Vec2 cs = platform->ClientSize();
    size = Vec2(cs.x / dpi, cs.y / dpi);
    kScreenW = size.x;
    kScreenH = size.y;

    InputState in;
    in = input;
    in.prevMouse = lastMouse_;
    in.mouse = input.mouse;
    input.prevMouse = input.mouse;
    in.delta();
    lastMouse_ = in.mouse;
    Animator& anim = ui.anim;
    (void)anim;

    Canvas& cv = render.canvas;
    cv.Reset(Rect(0, 0, size.x, size.y));
    ui.BeginFrame(cv, in, dt);
    Layout();
    // background pattern + main shell
    const Theme& th = GetTheme();
    cv.FillRect(Rect(0, 0, size.x, size.y), th.bg);
    if (page != Page::Home) {
        // bg pattern dots (a cheap stand-in for the svg pattern)
        (void)0;
    }
    DrawSidebar(sidebar.bounds);
    DrawTopBar(topbarRect);
    ui.PushClip(contentRect);
    if (PageBase* p = CurrentPage()) p->Draw(*this);
    ui.PopClip();
    ui.FlushDeferred();
    DrawOverlays();
    DrawBoot();
    ui.EndFrame();

    // tooltip last (top layer)
    if (!ui.tooltipText.empty()) {
        TextStyle st;
        st.size = 12.5f;
        st.weight = 500;
        float w = Fonts().MeasureLine(ui.tooltipText, st) + 20.f;
        float h = 30.f;
        Vec2 p(in.mouse.x + 14.f, in.mouse.y + 18.f);
        if (p.x + w > size.x - 8) p.x = in.mouse.x - w - 8;
        if (p.y + h > size.y - 8) p.y = in.mouse.y - h - 8;
        Rect box(p.x, p.y, w, h);
        cv.Shadow(box, 8, 16, Color(0, 0, 0, 0.45f), 4);
        cv.FillRounded(box, 8, th.panel3);
        cv.StrokeRounded(box.inset(0.5f), 7.5f, 1, th.border);
        st.color = th.text;
        float lh = Fonts().LineHeight(500, 12.5f) * 1.45f;
        Fonts().Draw(cv, ui.tooltipText, st, box.x + 10, box.y + (box.h - lh) * 0.5f);
    }

    // reset per-frame input edges (platform writes into `input` cumulatively)
    input.pressed = false;
    input.released = false;
    input.wheel = 0.f;
    input.textInput.clear();
    input.keyBackspace = input.keyEnter = input.keyEscape = input.keyTab = false;

    platform->Present();

    // fade the boot overlay after a moment
    if (bootShown && now - startTime > 2.1) {
        bootShown = false;
        bootDone = now;
    }
}

void App::Layout() {
    const Theme& th = GetTheme();
    (void)th;
    sidebarCollapsed = settings.sidebarCollapsed;
    sidebarAnim = ui.anim.Animate("sidebar", sidebarCollapsed ? 80.f : 228.f, 12.f);
    float sidebarW = sidebarAnim;
    sidebar.bounds = Rect(0, 0, sidebarW, size.y);
    topbarRect = Rect(sidebarW, 0, size.x - sidebarW, 62);
    contentRect = Rect(sidebarW, 62, size.x - sidebarW, size.y - 62);
}

}  // namespace crit
