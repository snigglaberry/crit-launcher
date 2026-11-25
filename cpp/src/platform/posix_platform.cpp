// Crit Launcher Native — Linux/stub platform services (headless preview + CI).
// Only compiled when _WIN32 is not defined.
#ifndef _WIN32

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "platform/platform.h"

namespace crit {

namespace {
std::mutex& RenderMutex() {
    static std::mutex m;
    return m;
}
}  // namespace
void RendererLockInit() {}
RenderLock::RenderLock() { RenderMutex().lock(); }
RenderLock::~RenderLock() { RenderMutex().unlock(); }
void PlatformSleep(int ms) { usleep((useconds_t)ms * 1000); }

string ExePath() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    return buf;
}
string ExeDir() {
    string p = ExePath();
    size_t s = p.find_last_of('/');
    return s == string::npos ? p : p.substr(0, s);
}
string AppDataDir() {
    if (const char* env = getenv("CRIT_LAUNCHER_DATA")) return env;
    const char* home = getenv("HOME");
    return string(home ? home : ".") + "/.crit-launcher";
}
bool FileExists(const string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool DirExists(const string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool MakeDirs(const string& path) {
    string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!cur.empty() && !DirExists(cur)) mkdir(cur.c_str(), 0755);
            if (i < path.size()) cur.push_back(path[i]);
        } else {
            cur.push_back(path[i]);
        }
    }
    return DirExists(path);
}
bool IsDir(const string& p) { return DirExists(p); }
std::vector<string> ListDir(const string& path) {
    std::vector<string> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        out.push_back(e->d_name);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const string& a, const string& b) { return Lower(a) < Lower(b); });
    return out;
}
std::vector<uint8_t> ReadFileBytes(const string& path) {
    std::vector<uint8_t> out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize((size_t)n);
        if (fread(out.data(), 1, (size_t)n, f) != (size_t)n) out.clear();
    }
    fclose(f);
    return out;
}
string ReadFileText(const string& path) {
    auto d = ReadFileBytes(path);
    return string(d.begin(), d.end());
}
bool WriteFileBytes(const string& path, const std::vector<uint8_t>& data) {
    MakeDirs(PathParent(path));
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}
bool WriteFileText(const string& path, const string& text) {
    return WriteFileBytes(path, std::vector<uint8_t>(text.begin(), text.end()));
}
uint64_t FileSize(const string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (uint64_t)st.st_size;
}
uint64_t FileMTime(const string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (uint64_t)st.st_mtime;
}
bool DeleteFileAt(const string& path) { return remove(path.c_str()) == 0; }
bool RemoveDirRecursive(const string& path) {
    if (!DirExists(path)) return DeleteFileAt(path);
    for (auto& e : ListDir(path)) {
        string p = PathJoin(path, e);
        if (DirExists(p)) RemoveDirRecursive(p);
        else DeleteFileAt(p);
    }
    return rmdir(path.c_str()) == 0;
}
bool RenamePath(const string& from, const string& to) { return rename(from.c_str(), to.c_str()) == 0; }
string PathJoin(const string& a, const string& b) {
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}
string PathFileName(const string& path) {
    size_t s = path.find_last_of("/\\");
    return s == string::npos ? path : path.substr(s + 1);
}
string PathExtension(const string& path) {
    string name = PathFileName(path);
    size_t s = name.find_last_of('.');
    return s == string::npos ? "" : Lower(name.substr(s));
}
string PathParent(const string& path) {
    size_t s = path.find_last_of("/\\");
    return s == string::npos ? "" : path.substr(0, s);
}
bool OpenFolder(const string&) { return false; }
bool OpenUrl(const string&) { return false; }
bool RunDetached(const string&, const std::vector<string>&, const string&) { return false; }
bool RunCapture(const string&, const std::vector<string>&, string& out, int) {
    out = "openjdk version \"17.0.9\" 2023-10-17";
    return true;
}
struct ProcessHandle {
    int exitCode = -1;
    bool alive = false;
};
std::shared_ptr<ProcessHandle> SpawnProcess(const string&, const std::vector<string>&, const string&, bool) {
    return nullptr;
}
bool ProcessAlive(const std::shared_ptr<ProcessHandle>& h) { return h && h->alive; }
int ProcessExitCode(const std::shared_ptr<ProcessHandle>& h) { return h ? h->exitCode : -1; }
uint64_t ProcessPid(const std::shared_ptr<ProcessHandle>&) { return 0; }
void ProcessKill(const std::shared_ptr<ProcessHandle>&) {}
bool ProcessReadOutput(const std::shared_ptr<ProcessHandle>&, string&) { return false; }
int ReadClipboardText(string&) { return 0; }
void GetMemoryInfo(uint64_t& total, uint64_t& avail) {
    total = 16384;
    avail = 8192;
}
string SystemSummary() { return "Linux preview"; }

HttpResult HttpGet(const string&, const std::vector<std::pair<string, string>>&) {
    HttpResult r;
    r.error = "network disabled in preview";
    return r;
}
HttpResult HttpGetBytes(const string&, const std::vector<std::pair<string, string>>&) {
    HttpResult r;
    r.error = "network disabled in preview";
    return r;
}
HttpResult HttpRequest(const string&, const string&, const string&, const std::vector<std::pair<string, string>>&) {
    HttpResult r;
    r.error = "network disabled in preview";
    return r;
}
bool HttpDownload(const string&, const string&, std::function<void(float)>) { return false; }
string Sha1Hex(const std::vector<uint8_t>& data) {
    uint32_t h = 2166136261u;
    for (uint8_t b : data) {
        h ^= b;
        h *= 16777619u;
    }
    return Format("%08x%08x", h, h * 2654435761u);
}
string Sha1File(const string& path) { return Sha1Hex(ReadFileBytes(path)); }
string Md5Hex(const string& input) { return Sha1Hex(std::vector<uint8_t>(input.begin(), input.end())); }
string Base64Encode(const std::vector<uint8_t>&) { return {}; }
std::vector<uint8_t> Base64Decode(const string&) { return {}; }

void Platform::Post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(postedMutex_);
    posted_.push_back(std::move(fn));
}
void Platform::RunPosted() {
    std::vector<std::function<void()>> run;
    {
        std::lock_guard<std::mutex> lk(postedMutex_);
        std::swap(run, posted_);
    }
    for (auto& f : run) f();
}

}  // namespace crit

#endif  // !_WIN32
