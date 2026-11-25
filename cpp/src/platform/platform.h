// Crit Launcher Native — window, DirectX 11 device and OS services.
#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include "core/base.h"
#include "render/gfx.h"

namespace crit {

class App;
struct RendererState;

// Rich display list used by the renderer each frame
struct RendererState {
    Canvas canvas;
    TextureRegistry textures;
    bool frameOk = true;
};

struct WindowConfig {
    const wchar_t* title = L"Crit Launcher";
    int width = 1080;
    int height = 700;
    int minWidth = 900;
    int minHeight = 580;
};

class Platform {
public:
    virtual ~Platform() = default;
    // Creates the window + swapchain. Returns the window client width/height.
    virtual bool Create(const WindowConfig& cfg) = 0;
    virtual void Destroy() = 0;
    // OS message pump. Returns false when the app should quit.
    virtual bool PumpMessages(RendererState& state) = 0;
    virtual void Present() = 0;
    virtual void Resize(int w, int h) = 0;
    virtual Vec2 ClientSize() const = 0;
    virtual float DpiScale() const = 0;
    virtual const char* Name() const = 0;
    // tasks queued from worker threads, run on the UI thread
    void Post(std::function<void()> fn);
    void RunPosted();
    // window chrome
    virtual void Minimize() = 0;
    virtual void ToggleMaximize() = 0;
    virtual bool IsMaximized() const = 0;
    virtual void Close() = 0;
    // drag the whole window (custom title bar)
    virtual void BeginDrag() = 0;
    virtual void SetCursor(int cursor) = 0;  // 0 arrow, 1 hand, 2 ibeam, 3 sizeNS
    virtual void SetIcon(const void* icoData, size_t size) = 0;
    // clipboard
    virtual void SetClipboard(const string& utf8) = 0;

protected:
    std::mutex postedMutex_;
    std::vector<std::function<void()>> posted_;
};

// platform.cpp
Platform* CreatePlatform();
string AppDataDir();          // %APPDATA%\CritLauncher (per-user data)
string ExeDir();
string ExePath();
bool FileExists(const string& path);
bool DirExists(const string& path);
bool MakeDirs(const string& path);
std::vector<string> ListDir(const string& path);          // names, dirs first then files
bool IsDir(const string& path);
string ReadFileText(const string& path);
bool WriteFileText(const string& path, const string& text);
std::vector<uint8_t> ReadFileBytes(const string& path);
bool WriteFileBytes(const string& path, const std::vector<uint8_t>& data);
uint64_t FileSize(const string& path);
uint64_t FileMTime(const string& path);
bool DeleteFileAt(const string& path);
bool RemoveDirRecursive(const string& path);
bool RenamePath(const string& from, const string& to);
string PathJoin(const string& a, const string& b);
string PathFileName(const string& path);
string PathExtension(const string& path);   // lower-case, with dot
string PathParent(const string& path);
bool OpenFolder(const string& path);
bool OpenUrl(const string& url);
bool RunDetached(const std::string& exe, const std::vector<string>& args, const string& cwd);
// runs a command and captures stdout (used for java probing)
bool RunCapture(const string& exe, const std::vector<string>& args, string& out, int timeoutMs = 8000);
void PlatformSleep(int ms);
// worker threads touch Record which lives in the renderer; serialise them.
void RendererLockInit();
class RenderLock {
public:
    RenderLock();
    ~RenderLock();
};
struct ProcessHandle;
std::shared_ptr<ProcessHandle> SpawnProcess(const string& exe, const std::vector<string>& args, const string& cwd,
                                            bool hidden = true);
bool ProcessAlive(const std::shared_ptr<ProcessHandle>& h);
int ProcessExitCode(const std::shared_ptr<ProcessHandle>& h);
uint64_t ProcessPid(const std::shared_ptr<ProcessHandle>& h);
void ProcessKill(const std::shared_ptr<ProcessHandle>& h);
bool ProcessReadOutput(const std::shared_ptr<ProcessHandle>& h, string& out);
int ReadClipboardText(string& out);
void GetMemoryInfo(uint64_t& totalMB, uint64_t& availMB);
string SystemSummary();

// json.cpp
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    string str;
    std::vector<Json> arr;
    std::vector<std::pair<string, Json>> obj;

    Json() = default;
    static Json parse(const string& text);
    string dump(bool pretty = false) const;

    bool isNull() const { return type == Null; }
    bool isArray() const { return type == Array; }
    bool isObject() const { return type == Object; }
    size_t size() const { return type == Array ? arr.size() : obj.size(); }
    const Json& operator[](size_t i) const;
    const Json& operator[](const string& key) const;
    Json& set(const string& key, Json v);
    Json& push(Json v);
    string getString(const string& key, const string& def = "") const;
    double getNumber(const string& key, double def = 0) const;
    bool getBool(const string& key, bool def = false) const;
    bool has(const string& key) const;
    static Json object() { Json j; j.type = Object; return j; }
    static Json array() { Json j; j.type = Array; return j; }
    static Json str_(const string& s) { Json j; j.type = String; j.str = s; return j; }
    static Json num_(double d) { Json j; j.type = Number; j.num = d; return j; }
    static Json bool_(bool v) { Json j; j.type = Bool; j.b = v; return j; }
};

// http.cpp
struct HttpResult {
    bool ok = false;
    int status = 0;
    string body;
    string error;
    std::vector<uint8_t> bytes;
};
HttpResult HttpGet(const string& url, const std::vector<std::pair<string, string>>& headers = {});
HttpResult HttpGetBytes(const string& url, const std::vector<std::pair<string, string>>& headers = {});
HttpResult HttpRequest(const string& method, const string& url, const string& body,
                       const std::vector<std::pair<string, string>>& headers);
bool HttpDownload(const string& url, const string& path, std::function<void(float)> progress = nullptr);
string Sha1Hex(const std::vector<uint8_t>& data);
string Sha1File(const string& path);
string Md5Hex(const string& input);
string Base64Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> Base64Decode(const string& s);

}  // namespace crit
