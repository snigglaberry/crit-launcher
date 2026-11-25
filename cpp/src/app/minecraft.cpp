// Crit Launcher Native — Minecraft install + launch engine (vanilla, Fabric, Forge/NeoForge).
#include <atomic>
#include <thread>

#include "app/pages.h"
#include "core/zip.h"

namespace crit {

namespace {

const char* kVersionManifest = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
const char* kResourcesBase = "https://resources.download.minecraft.net/";
const char* kLibrariesBase = "https://libraries.minecraft.net/";

struct JavaRuntime {
    string version;
    string path;  // folder
};

std::vector<string> g_launchArgs;
std::map<string, std::shared_ptr<ProcessHandle>> g_processes;

string OsName() { return "windows"; }
string ArchName() {
#ifdef _M_X64
    return "x86_64";
#else
    return "x86";
#endif
}

// Mojang disallow/allow rules
bool RulesAllow(const Json& rules) {
    if (!rules.isArray() || rules.size() == 0) return true;
    bool allowed = false;
    for (auto& rule : rules.arr) {
        bool match = true;
        if (rule["os"].isObject()) {
            string name = rule["os"].getString("name");
            if (!name.empty() && name != OsName()) match = false;
            string arch = rule["os"].getString("arch");
            if (!arch.empty()) {
                if (arch == "x86" && ArchName() != "x86") match = false;
                if ((arch == "x86_64" || arch == "amd64" || arch == "x64") && ArchName() != "x86_64") match = false;
            }
        }
        if (rule.has("features")) {
            const Json& f = rule["features"];
            if (f.getBool("is_demo_user", false)) match = false;
            if (f.getBool("has_custom_resolution", false)) match = false;
            if (f.getBool("has_quick_plays_support", false)) match = false;
        }
        if (match) allowed = rule.getString("action", "disallow") == "allow";
    }
    return allowed;
}

string Substitute(const string& in, const std::map<string, string>& vars) {
    string out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '$' && i + 1 < in.size() && in[i + 1] == '{') {
            size_t close = in.find('}', i + 2);
            if (close != string::npos) {
                string key = in.substr(i + 2, close - i - 2);
                auto it = vars.find(key);
                if (it != vars.end()) {
                    out += it->second;
                    i = close + 1;
                    continue;
                }
            }
        }
        out.push_back(in[i]);
        i++;
    }
    return out;
}

std::vector<string> ResolveArgs(const Json& spec, const std::map<string, string>& vars) {
    std::vector<string> out;
    if (spec.isArray()) {
        for (auto& item : spec.arr) {
            if (item.type == Json::String) out.push_back(Substitute(item.str, vars));
            else if (item.isObject()) {
                if (RulesAllow(item["rules"])) {
                    const Json& v = item["value"];
                    if (v.isArray()) {
                        for (auto& s : v.arr) out.push_back(Substitute(s.str, vars));
                    } else if (v.type == Json::String) {
                        out.push_back(Substitute(v.str, vars));
                    }
                }
            }
        }
    } else if (spec.type == Json::String) {
        for (auto& tok : Split(spec.str, ' '))
            if (!tok.empty()) out.push_back(Substitute(tok, vars));
    }
    return out;
}

// maven coordinate -> libraries path
string MavenToPath(const string& coord) {
    auto parts = Split(coord, ':');
    if (parts.size() < 3) return coord;
    string path = ReplaceAll(parts[0], ".", "/");
    path += "/" + parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2];
    if (parts.size() > 3 && !parts[3].empty()) path += "-" + parts[3];
    path += ".jar";
    return path;
}

struct LibEntry {
    string path;   // relative to libraries dir
    string url;
    string sha1;
    int64_t size = 0;
    bool native = false;
    string extractExclude;
};

std::vector<LibEntry> CollectLibraries(App& app, const Json& libs) {
    std::vector<LibEntry> out;
    for (auto& lib : libs.arr) {
        if (lib.has("rules") && !RulesAllow(lib["rules"])) continue;
        string name = lib.getString("name");
        const Json& downloads = lib["downloads"];
        bool nativeTarget = false;
        if (lib.has("natives")) {
            const Json& natives = lib["natives"];
            string classifier = natives.isObject() ? natives.getString(OsName()) : "";
            classifier = ReplaceAll(classifier, "${arch}", ArchName() == "x86_64" ? "64" : "32");
            if (!classifier.empty() && downloads.has("classifiers")) {
                const Json& c = downloads["classifiers"][classifier];
                if (!c.isNull()) {
                    LibEntry e;
                    e.path = "libraries/" + c.getString("path", MavenToPath(name + ":" + classifier));
                    // path field from Mojang is relative to the game dir; strip a leading "libraries/"
                    if (StartsWith(e.path, "libraries/libraries/")) e.path = e.path.substr(10);
                    e.url = c.getString("url");
                    e.sha1 = c.getString("sha1");
                    e.size = (int64_t)c.getNumber("size");
                    e.native = true;
                    out.push_back(e);
                    nativeTarget = true;
                }
            }
        }
        const Json& artifact = downloads["artifact"];
        if (!artifact.isNull() && artifact.type == Json::Object) {
            LibEntry e;
            string p = artifact.getString("path", MavenToPath(name));
            e.path = "libraries/" + p;
            e.url = artifact.getString("url");
            e.sha1 = artifact.getString("sha1");
            e.size = (int64_t)artifact.getNumber("size");
            // skip the vanilla client jar duplicate; the installer downloads it separately
            if (e.path.find("libraries/libraries/") == 0) e.path = e.path.substr(10);
            out.push_back(e);
        } else if (artifact.isNull() && !nativeTarget && lib.has("url")) {
            // legacy library entry: name + url base
            LibEntry e;
            e.path = "libraries/" + MavenToPath(name);
            string base = lib.getString("url");
            if (base.empty()) base = kLibrariesBase;
            e.url = base + MavenToPath(name);
            out.push_back(e);
        }
        (void)app;
    }
    return out;
}

bool DownloadIfMissing(App& app, const LibEntry& e, std::atomic<int>* done, int total) {
    string dest = app.DataPath(e.path);
    if (FileExists(dest) && FileSize(dest) > 0) {
        if (e.sha1.empty() || FileSize(dest) == (uint64_t)e.size || true) {
            if (done) {
                int d = ++(*done);
                app.progress.fraction = total > 0 ? (float)d / (float)total : 1.f;
            }
            return true;
        }
    }
    if (e.url.empty()) return false;
    HttpResult r = HttpGetBytes(e.url);
    if (!r.ok || r.bytes.empty()) {
        LogError("download failed: %s", e.url.c_str());
        return false;
    }
    bool ok = WriteFileBytes(dest, r.bytes);
    if (done) {
        int d = ++(*done);
        app.progress.fraction = total > 0 ? (float)d / (float)total : 1.f;
    }
    return ok;
}

// extracts .dll natives from the classifier jars
bool ExtractNatives(App& app, const std::vector<LibEntry>& libs, const string& nativesDir) {
    MakeDirs(nativesDir);
    bool any = false;
    for (auto& e : libs) {
        if (!e.native) continue;
        string jarPath = app.DataPath(e.path);
        if (!FileExists(jarPath)) continue;
        ZipArchive zip;
        if (!zip.Open(jarPath)) continue;
        for (auto& entry : zip.Entries()) {
            string ext = PathExtension(entry.name);
            if (ext != ".dll" && ext != ".so" && ext != ".dylib") continue;
            string base = PathFileName(entry.name);
            std::vector<uint8_t> bytes;
            if (!zip.Read(entry.name, bytes)) continue;
            WriteFileBytes(PathJoin(nativesDir, base), bytes);
            any = true;
        }
    }
    return any;
}

string FindInPath(const string& exe) {
    std::wstring w = ToWide(exe);
    std::wstring wbuf(32767, L'\0');
#ifdef _WIN32
    unsigned long len = SearchPathW(nullptr, w.c_str(), L".exe", 32767, wbuf.data(), nullptr);
#else
    unsigned long len = 0;
#endif
    if (len == 0) return {};
    wbuf.resize(len);
    return ToUtf8(wbuf);
}

}  // namespace

std::vector<string> DetectJavaRuntimes(App& app) {
    std::vector<string> found;
    std::vector<string> candidates;
    // bundled runtimes
    string rtDir = app.DataPath("runtime");
    if (DirExists(rtDir)) {
        for (auto& d : ListDir(rtDir)) {
            string exe = PathJoin(PathJoin(PathJoin(rtDir, d), "bin"), "java.exe");
            if (FileExists(exe)) candidates.push_back(exe);
        }
    }
    // PATH
    string pathJava = FindInPath("javaw");
    if (pathJava.empty()) pathJava = FindInPath("java");
    if (!pathJava.empty()) candidates.push_back(pathJava);
    // common installs
    for (auto& base : {string("C:\\Program Files\\Java"), string("C:\\Program Files\\Eclipse Adoptium"),
                       string("C:\\Program Files\\Microsoft")}) {
        if (!DirExists(base)) continue;
        for (auto& d : ListDir(base)) {
            for (auto& sub : {"bin\\java.exe", "bin\\javaw.exe"}) {
                string exe = PathJoin(PathJoin(base, d), sub);
                if (FileExists(exe)) candidates.push_back(exe);
            }
        }
    }
    for (auto& c : candidates) {
        string out;
        if (RunCapture(c, {"-version"}, out, 4000)) {
            if (out.find("version") != string::npos) {
                found.push_back(c);
                if (app.javaPath.empty()) app.javaPath = c;
            }
        }
    }
    return found;
}

// picks a runtime with the major version the game asks for (falls back to the newest)
static string JavaForVersion(const std::vector<string>& javas, int required) {
    if (javas.empty()) return {};
    if (required <= 0) return javas[0];
    for (auto& j : javas) {
        string out;
        RunCapture(j, {"-version"}, out, 3000);
        // e.g. openjdk version "17.0.9" 2023-10-17
        size_t q1 = out.find('"');
        if (q1 == string::npos) continue;
        size_t q2 = out.find('"', q1 + 1);
        if (q2 == string::npos) continue;
        string ver = out.substr(q1 + 1, q2 - q1 - 1);
        int major = 0;
        if (StartsWith(ver, "1.")) major = atoi(ver.c_str() + 2);
        else major = atoi(ver.c_str());
        if (major == required) return j;
    }
    return javas[0];
}

// ================================================================ install
void StartInstall(App& app, const string& instanceName) {
    if (app.IsInstanceInstalling(instanceName)) return;
    app.installingInstances.push_back(instanceName);
    app.progress.active = true;
    app.progress.instance = instanceName;
    app.progress.task = "Fetching version metadata";
    app.progress.fraction = 0.f;
    app.progress.startedAt = NowSeconds();
    app.ShowToast("Installing " + instanceName + "…");
    App* a = &app;
    app.workers.emplace_back([a, instanceName] {
        Instance* inst = a->FindInstance(instanceName);
        if (!inst) return;
        string version = inst->version;
        string loader = inst->loader;
        string loaderVersion = inst->loaderVersion;
        string err;
        try {
            // 1. manifest
            HttpResult mres = HttpGet(kVersionManifest);
            if (!mres.ok) throw std::runtime_error("Couldn't reach Mojang's version list.");
            Json manifest = Json::parse(mres.body);
            string versionUrl;
            for (auto& v : manifest["versions"].arr) {
                if (v.getString("id") == version) {
                    versionUrl = v.getString("url");
                    break;
                }
            }
            if (versionUrl.empty()) throw std::runtime_error("Minecraft " + version + " was not found.");
            Json vjson = Json::parse(HttpGet(versionUrl).body);
            int javaMajor = (int)vjson["javaVersion"].getNumber("majorVersion", 8);

            // 2. java
            a->platform->Post([a] { a->progress.task = "Locating Java"; });
            std::vector<string> javas = DetectJavaRuntimes(*a);
            if (javas.empty()) throw std::runtime_error("No Java runtime found. Install Java or add one in Settings.");
            string javaExe = JavaForVersion(javas, javaMajor);

            // 3. libraries + fabric profile
            Json merged = Json::object();
            merged.set("libraries", vjson["libraries"]);
            merged.set("arguments", vjson["arguments"]);
            merged.set("mainClass", vjson["mainClass"]);
            merged.set("assetIndex", vjson["assetIndex"]);
            merged.set("downloads", vjson["downloads"]);
            if (loader == "fabric") {
                a->platform->Post([a] { a->progress.task = "Fetching Fabric profile"; });
                HttpResult fr = HttpGet("https://meta.fabricmc.net/v2/versions/loader/" + version + "/" + loaderVersion +
                                        "/profile/json");
                if (!fr.ok) throw std::runtime_error("Couldn't fetch the Fabric profile.");
                Json fp = Json::parse(fr.body);
                Json libs = Json::array();
                for (auto& l : fp["libraries"].arr) libs.push(l);
                for (auto& l : vjson["libraries"].arr) libs.push(l);
                merged.set("libraries", libs);
                merged.set("mainClass", Json::str_(fp.getString("mainClass")));
            } else if (loader == "forge" || loader == "neoforge") {
                a->platform->Post([a, loader] { a->progress.task = "Preparing " + loader; });
                // download the installer jar and run it headlessly
                Json libs = Json::array();
                for (auto& l : vjson["libraries"].arr) libs.push(l);
                merged.set("libraries", libs);
            }

            std::vector<LibEntry> libs = CollectLibraries(*a, merged["libraries"]);
            int totalSteps = (int)libs.size() + 2;
            std::atomic<int> done{0};
            for (auto& e : libs) DownloadIfMissing(*a, e, &done, totalSteps);

            // 4. client jar
            a->platform->Post([a] { a->progress.task = "Downloading client"; });
            string clientUrl = vjson["downloads"]["client"].getString("url");
            string clientPath = a->DataPath("versions/" + version + "/" + version + ".jar");
            if (!FileExists(clientPath) || FileSize(clientPath) < 1000) {
                HttpResult cr = HttpGetBytes(clientUrl);
                if (!cr.ok) throw std::runtime_error("Couldn't download the Minecraft client.");
                WriteFileBytes(clientPath, cr.bytes);
            }
            done++;

            // 5. assets index (objects are downloaded lazily by the game)
            a->platform->Post([a] { a->progress.task = "Downloading assets"; });
            string assetIndexId = vjson["assetIndex"].getString("id", version);
            string assetIndexUrl = vjson["assetIndex"].getString("url");
            string assetIndexPath = a->DataPath("assets/indexes/" + assetIndexId + ".json");
            if (!FileExists(assetIndexPath) && !assetIndexUrl.empty()) {
                HttpResult ar = HttpGet(assetIndexUrl);
                if (ar.ok) WriteFileText(assetIndexPath, ar.body);
            }

            // 6. forge/neoforge installer
            string mainClass = merged.getString("mainClass");
            if (loader == "forge" || loader == "neoforge") {
                a->platform->Post([a, loader] { a->progress.task = "Running " + loader + " installer"; });
                string full, installerUrl;
                if (loader == "forge") {
                    full = version + "-" + loaderVersion;
                    installerUrl = "https://maven.minecraftforge.net/net/minecraftforge/forge/" + full + "/forge-" + full +
                                   "-installer.jar";
                } else if (version == "1.20.1") {
                    full = version + "-" + loaderVersion;
                    installerUrl = "https://maven.neoforged.net/releases/net/neoforged/forge/" + full + "/forge-" + full +
                                   "-installer.jar";
                } else {
                    full = loaderVersion;
                    installerUrl = "https://maven.neoforged.net/releases/net/neoforged/neoforge/" + full + "/neoforge-" +
                                   full + "-installer.jar";
                }
                string installerPath = a->DataPath("tmp/" + PathFileName(installerUrl));
                HttpResult ir = HttpGetBytes(installerUrl);
                if (!ir.ok) throw std::runtime_error("Couldn't download the " + loader + " installer.");
                WriteFileBytes(installerPath, ir.bytes);
                string out;
                bool ran = RunCapture(javaExe, {"-jar", installerPath, "--installClient",
                                                a->DataPath(".").substr(0, a->DataPath(".").size() - 1)},
                                      out, 600000);
                a->PushConsole(out);
                if (!ran) LogError("forge installer failed: %s", out.substr(0, 400).c_str());
                // the modern installer writes versions/<loader>-<mc>/<loader>-<mc>.json
                string loaderId = loader == "forge" ? ("forge-" + full) : full;
                string loaderJson = a->DataPath("versions/" + loaderId + "/" + loaderId + ".json");
                if (FileExists(loaderJson)) {
                    Json lj = Json::parse(ReadFileText(loaderJson));
                    if (lj.isObject()) {
                        Json libs2 = Json::array();
                        for (auto& l : lj["libraries"].arr) libs2.push(l);
                        for (auto& l : vjson["libraries"].arr) libs2.push(l);
                        merged.set("libraries", libs2);
                        if (lj.has("mainClass")) mainClass = lj.getString("mainClass");
                        if (lj.has("arguments")) merged.set("arguments", lj["arguments"]);
                        std::vector<LibEntry> libs2e = CollectLibraries(*a, libs2);
                        std::atomic<int> d2{0};
                        for (auto& e : libs2e) DownloadIfMissing(*a, e, &d2, (int)libs2e.size());
                        libs = libs2e;
                    }
                }
            }

            // 7. natives
            a->platform->Post([a] { a->progress.task = "Extracting natives"; });
            ExtractNatives(*a, libs, a->DataPath("instances/" + instanceName + "/natives"));

            // 8. persist the resolved plan in instance.json
            std::vector<string> classpath;
            for (auto& e : libs) {
                if (e.native) continue;
                classpath.push_back(e.path);
            }
            classpath.push_back("versions/" + version + "/" + version + ".jar");
            a->platform->Post([a, instanceName, mainClass, javaExe, classpath, merged, version] {
                Instance* it = a->FindInstance(instanceName);
                if (!it) return;
                it->installed = true;
                it->mainClass = mainClass.empty() ? merged.getString("mainClass") : mainClass;
                it->classpath = classpath;
                it->assetIndex = merged["assetIndex"].getString("id", version);
                it->arguments = merged["arguments"];
                it->minecraftArguments = merged.getString("minecraftArguments");
                a->SaveInstance(*it);
                a->installingInstances.erase(std::remove(a->installingInstances.begin(), a->installingInstances.end(),
                                                        instanceName),
                                             a->installingInstances.end());
                a->progress.active = false;
                a->progress.fraction = 1.f;
                a->javaPath = javaExe;
                a->ShowToast(instanceName + " is ready to play.", false, true);
                a->PushConsole("Installed " + instanceName + " (" + version + ") using " + javaExe);
            });
            return;
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "Unexpected error while installing.";
        }
        a->platform->Post([a, instanceName, err] {
            a->installingInstances.erase(
                std::remove(a->installingInstances.begin(), a->installingInstances.end(), instanceName),
                a->installingInstances.end());
            a->progress.active = false;
            a->ShowToast(err.empty() ? "Install failed." : err, true);
            a->PushConsole("Install failed: " + err);
        });
    });
}

// ================================================================ launch
void StartLaunch(App& app, const string& instanceName) {
    if (app.IsInstanceRunning(instanceName)) return;
    Instance* inst = app.FindInstance(instanceName);
    if (!inst) return;
    if (!inst->installed) {
        StartInstall(app, instanceName);
        return;
    }
    // optimistic running state so the UI reacts instantly
    app.runningInstances.push_back(instanceName);
    app.launchStartTimes[instanceName] = NowSeconds();
    App* a = &app;
    std::string name = instanceName;
    std::vector<string> classpath = inst->classpath;
    string mainClass = inst->mainClass;
    Json arguments = inst->arguments;
    string legacyArgs = inst->minecraftArguments;
    string assetIndex = inst->assetIndex;
    string version = inst->version;
    Account* acc = app.ActiveAccount();
    Account accountCopy;
    if (acc) accountCopy = *acc;
    else accountCopy.name = "Player";
    Settings s = app.settings;

    app.workers.emplace_back([a, name, classpath, mainClass, arguments, legacyArgs, assetIndex, version, accountCopy, s] {
        string javaExe = s.javaPath;
        if (javaExe.empty() || !FileExists(javaExe)) {
            std::vector<string> javas = DetectJavaRuntimes(*a);
            if (javas.empty()) {
                a->platform->Post([a, name] {
                    a->runningInstances.erase(std::remove(a->runningInstances.begin(), a->runningInstances.end(), name),
                                              a->runningInstances.end());
                    a->ShowToast("No Java runtime found. Install Java 17+ and try again.", true);
                });
                return;
            }
            javaExe = javas[0];
        }
        string gameDir = a->DataPath("instances/" + name + "/game");
        string nativesDir = a->DataPath("instances/" + name + "/natives");
        MakeDirs(gameDir);
        string uuid = accountCopy.mcUuid.empty() ? accountCopy.uuid : accountCopy.mcUuid;
        uuid = ReplaceAll(uuid, "-", "");
        std::map<string, string> vars;
        vars["auth_player_name"] = accountCopy.name;
        vars["auth_uuid"] = uuid;
        vars["auth_access_token"] = accountCopy.type == "offline" ? "0" : accountCopy.mcToken;
        vars["auth_session"] = "0";
        vars["auth_xuid"] = "0";
        vars["clientid"] = "0";
        vars["user_type"] = accountCopy.type == "offline" ? "legacy" : "msa";
        vars["user_properties"] = "{}";
        vars["version_name"] = version;
        vars["version_type"] = "release";
        vars["game_directory"] = gameDir;
        vars["assets_root"] = a->DataPath("assets");
        vars["assets_index_name"] = assetIndex.empty() ? version : assetIndex;
        vars["natives_directory"] = nativesDir;
        vars["library_directory"] = a->DataPath("libraries");
        vars["classpath_separator"] = ";";
        string cp;
        for (size_t i = 0; i < classpath.size(); i++) {
            if (i) cp += ";";
            string p = classpath[i];
            cp += (p.size() > 1 && p[1] == ':') ? p : a->DataPath(p);
        }
        vars["classpath"] = cp;
        vars["launcher_name"] = "CritLauncher";
        vars["launcher_version"] = "1.8";
        std::vector<string> args;
        int xms = s.minMemoryMb >= 256 ? std::min(s.minMemoryMb, s.memoryMb) : std::min(s.memoryMb, 1024);
        args.push_back(Format("-Xmx%dM", s.memoryMb));
        args.push_back(Format("-Xms%dM", xms));
        std::vector<string> jvmArgs = ResolveArgs(arguments["jvm"], vars);
        if (jvmArgs.empty()) {
            jvmArgs = {"-Djava.library.path=${natives_directory}", "-Dminecraft.launcher.brand=crit-launcher",
                       "-Dminecraft.launcher.version=1.8", "-cp", "${classpath}"};
            // resolve those templates
            std::vector<string> resolved;
            for (auto& j : jvmArgs) resolved.push_back(Substitute(j, vars));
            jvmArgs = resolved;
        }
        bool hasCp = false;
        for (auto& j : jvmArgs)
            if (j == "-cp" || j == "-classpath") hasCp = true;
        if (!hasCp) {
            args.push_back("-cp");
            args.push_back(cp);
        }
        for (auto& j : jvmArgs) args.push_back(j);
        if (!s.jvmArgs.empty()) {
            for (auto& tok : Split(s.jvmArgs, ' '))
                if (!tok.empty()) args.push_back(tok);
        }
        args.push_back(mainClass);
        std::vector<string> gameArgs = ResolveArgs(arguments["game"], vars);
        if (gameArgs.empty()) gameArgs = ResolveArgs(Json::str_(legacyArgs), vars);
        if (gameArgs.empty()) {
            gameArgs = {"--username", accountCopy.name, "--version", version, "--gameDir", gameDir,
                        "--assetsDir", a->DataPath("assets"), "--assetIndex", assetIndex, "--uuid", uuid,
                        "--accessToken", vars["auth_access_token"], "--userType", vars["user_type"],
                        "--versionType", "release"};
        }
        for (auto& g : gameArgs) args.push_back(g);
        if (s.fullscreen) args.push_back("--fullscreen");
        else if (s.gameWidth >= 320 && s.gameHeight >= 240) {
            args.push_back("--width");
            args.push_back(std::to_string(s.gameWidth));
            args.push_back("--height");
            args.push_back(std::to_string(s.gameHeight));
        }
        string exe = javaExe;
        if (EndsWith(Lower(exe), "java.exe")) exe = exe.substr(0, exe.size() - 8) + "javaw.exe";
        auto proc = SpawnProcess(exe, args, gameDir, false);
        if (!proc) proc = SpawnProcess(javaExe, args, gameDir, false);
        a->platform->Post([a, name, args] {
            string cmdline = "java ";
            for (auto& x : args) cmdline += x + " ";
            a->PushConsole("[launch] " + name + "\n" + cmdline + "\n");
        });
        if (!proc) {
            a->platform->Post([a, name] {
                a->runningInstances.erase(std::remove(a->runningInstances.begin(), a->runningInstances.end(), name),
                                          a->runningInstances.end());
                a->ShowToast("Couldn't start Java for " + name + ".", true);
            });
            return;
        }
        g_processes[name] = proc;
        a->platform->Post([a] { a->ShowToast("Minecraft is starting…", false, true); });
        // watch the process
        for (;;) {
            string out;
            while (ProcessReadOutput(proc, out)) {
                a->platform->Post([a, out] { a->PushConsole(out); });
            }
            if (!ProcessAlive(proc)) break;
            PlatformSleep(250);
        }
        string rest;
        while (ProcessReadOutput(proc, rest)) {
            a->platform->Post([a, rest] { a->PushConsole(rest); });
        }
        int code = ProcessExitCode(proc);
        a->platform->Post([a, name, code] {
            a->runningInstances.erase(std::remove(a->runningInstances.begin(), a->runningInstances.end(), name),
                                      a->runningInstances.end());
            for (auto& i : a->instances) {
                if (i.name == name) {
                    double start = a->launchStartTimes[name];
                    int64_t played = (int64_t)(NowSeconds() - start);
                    i.playtime += played;
                    a->SaveInstance(i);
                }
            }
            if (code != 0 && code != -1) {
                a->ShowToast(name + " exited with code " + std::to_string(code) + ".", true);
                a->Navigate(Page::Console);
            }
        });
        g_processes.erase(name);
    });
}

void StopInstanceProcess(App& app, const string& instanceName) {
    auto it = g_processes.find(instanceName);
    if (it != g_processes.end() && it->second) {
        ProcessKill(it->second);
        app.ShowToast("Stopping " + instanceName + "…");
    } else {
        app.runningInstances.erase(std::remove(app.runningInstances.begin(), app.runningInstances.end(), instanceName),
                                   app.runningInstances.end());
    }
}

}  // namespace crit
