import os
import sys
import base64
import struct
import re
import json
import uuid
import time
import shutil
import hashlib
import logging
import zipfile
import platform
import subprocess
import threading
import traceback
import urllib.request
import urllib.parse
import urllib.error
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
def _resource_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS)  
    return Path(__file__).resolve().parent
def _app_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent
RESOURCE_DIR = _resource_dir()
APP_DIR = _app_dir()
DATA_DIR = Path(os.environ.get("CRIT_LAUNCHER_DATA", str(APP_DIR / "launcher_data")))
INSTANCES_DIR = DATA_DIR / "instances"
VERSIONS_DIR = DATA_DIR / "versions"
LIBRARIES_DIR = DATA_DIR / "libraries"
ASSETS_DIR = DATA_DIR / "assets"
ASSET_OBJECTS_DIR = ASSETS_DIR / "objects"
ASSET_INDEXES_DIR = ASSETS_DIR / "indexes"
RUNTIME_DIR = DATA_DIR / "runtime"
TMP_DIR = DATA_DIR / "tmp"
SKINS_DIR = DATA_DIR / "skins"
ACCOUNTS_FILE = DATA_DIR / "accounts.json"
SETTINGS_FILE = DATA_DIR / "settings.json"
LOG_FILE = DATA_DIR / "launcher.log"
for _d in (DATA_DIR, INSTANCES_DIR, VERSIONS_DIR, LIBRARIES_DIR, ASSETS_DIR,
           ASSET_OBJECTS_DIR, ASSET_INDEXES_DIR, RUNTIME_DIR, TMP_DIR, SKINS_DIR):
    _d.mkdir(parents=True, exist_ok=True)
logging.basicConfig(
    filename=str(LOG_FILE),
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
def log_exception(context: str):
    logging.error("%s\n%s", context, traceback.format_exc())
VERSION_MANIFEST_URL = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
FABRIC_LOADER_LIST_URL = "https://meta.fabricmc.net/v2/versions/loader"
FABRIC_GAME_LIST_URL = "https://meta.fabricmc.net/v2/versions/game"
FABRIC_PROFILE_URL_TMPL = "https://meta.fabricmc.net/v2/versions/loader/{mc}/{loader}/profile/json"
FORGE_MAVEN = "https://maven.minecraftforge.net/net/minecraftforge/forge/"
FORGE_METADATA_URL = FORGE_MAVEN + "maven-metadata.xml"
JAVA_RUNTIME_INDEX_URL = ("https://launchermeta.mojang.com/v1/products/java-runtime/"
                          "2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json")
MODRINTH_API = "https://api.modrinth.com/v2"
MC_PROFILE_URL = "https://api.minecraftservices.com/minecraft/profile"
MC_SKINS_URL = "https://api.minecraftservices.com/minecraft/profile/skins"
MC_CAPE_URL = "https://api.minecraftservices.com/minecraft/profile/capes/active"
MOJANG_LOOKUP_URL = "https://api.mojang.com/users/profiles/minecraft/"
SESSION_PROFILE_URL = "https://sessionserver.mojang.com/session/minecraft/profile/"
MC_WEB_LOGIN_URL = "https://www.minecraft.net/en-us/login"
MC_WEB_PROFILE_URL = "https://www.minecraft.net/en-us/msaprofile"
MC_WEB_COOKIE_JS = "`; ${document.cookie}`.split('; bearer_token=').pop().split(';').shift()"
RESOURCES_BASE_URL = "https://resources.download.minecraft.net"
LIBRARIES_BASE_URL = "https://libraries.minecraft.net/"
USER_AGENT = "CritLauncher/1.0 (+https://example.invalid)"
CREATE_NO_WINDOW = 0x08000000 if os.name == "nt" else 0
INSTALL_SCHEMA = 2
DEFAULT_JAVA_INFO = {"component": "jre-legacy", "majorVersion": 8}
ADDON_TYPES = ("mod", "resourcepack", "modpack")
INSTANCE_ICONS = {"box", "shirt", "flame", "hammer", "square", "circle", "triangle", "star",
                  "hexagon", "diamond", "heart", "cat", "ghost", "skull", "zap", "gem"}
_INVALID_NAME_RE = re.compile(r'[<>:"/\\|?*\x00-\x1f]')
def http_get_json(url: str, timeout: int = 20, retries: int = 2):
    last = None
    for attempt in range(retries + 1):
        try:
            req = urllib.request.Request(
                url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            if exc.code in (400, 401, 403, 404):
                raise
            last = exc
        except Exception as exc:  
            last = exc
        time.sleep(0.8 * (attempt + 1))
    raise last  
def http_get_text(url: str, timeout: int = 30) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8", "replace")
def _json_or_text(raw: bytes):
    try:
        return json.loads(raw.decode("utf-8"))
    except Exception:
        return {"raw": raw.decode("utf-8", "replace")[:300]}
def http_get_auth(url: str, token: str, timeout: int = 25):
    req = urllib.request.Request(url, headers={
        "Authorization": f"Bearer {token}", "User-Agent": USER_AGENT, "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, _json_or_text(resp.read())
    except urllib.error.HTTPError as exc:
        return exc.code, _json_or_text(exc.read())
def http_download(url: str, dest: Path, expected_size=None, expected_sha1=None,
                  progress_cb=None, chunk_size: int = 65536):
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp, open(tmp, "wb") as f:
        while True:
            chunk = resp.read(chunk_size)
            if not chunk:
                break
            f.write(chunk)
            if progress_cb:
                progress_cb(len(chunk))
    if expected_sha1:
        if file_sha1(tmp) != expected_sha1:
            tmp.unlink(missing_ok=True)
            raise IOError(f"Checksum mismatch for {dest.name}")
    os.replace(tmp, dest)
def file_sha1(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()
def file_is_valid(path: Path, expected_size=None, expected_sha1=None) -> bool:
    if not path.exists() or not path.is_file():
        return False
    try:
        if expected_size is not None and path.stat().st_size != expected_size:
            return False
        if expected_sha1 is not None:
            return file_sha1(path) == expected_sha1
        return path.stat().st_size > 0
    except OSError:
        return False
def is_within(root: Path, target: Path) -> bool:
    try:
        target.resolve().relative_to(root.resolve())
        return True
    except (ValueError, OSError):
        return False
def sanitize_instance_name(name: str) -> str:
    name = (name or "").strip().rstrip(".")
    if not name:
        raise ValueError("Instance name cannot be empty.")
    if len(name) > 64:
        raise ValueError("Instance name is too long.")
    if ".." in name or "/" in name or "\\" in name:
        raise ValueError("Instance name contains invalid characters.")
    if os.path.isabs(name):
        raise ValueError("Instance name cannot be an absolute path.")
    if _INVALID_NAME_RE.search(name):
        raise ValueError('Instance name cannot contain: < > : " / \\ | ? *')
    return name
def offline_uuid(username: str) -> str:
    data = ("OfflinePlayer:" + username).encode("utf-8")
    digest = bytearray(hashlib.md5(data).digest())
    digest[6] = (digest[6] & 0x0F) | 0x30
    digest[8] = (digest[8] & 0x3F) | 0x80
    return str(uuid.UUID(bytes=bytes(digest)))
def current_os_name() -> str:
    system = platform.system().lower()
    if system.startswith("win"):
        return "windows"
    if system == "darwin":
        return "osx"
    return "linux"
def current_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        return "64"
    if machine in ("x86", "i386", "i686"):
        return "32"
    if "arm64" in machine or "aarch64" in machine:
        return "arm64"
    return "64"
def classpath_sep() -> str:
    return ";" if current_os_name() == "windows" else ":"
def open_in_file_manager(path: Path):
    path.mkdir(parents=True, exist_ok=True)
    if current_os_name() == "windows":
        os.startfile(str(path))  
    elif current_os_name() == "osx":
        subprocess.Popen(["open", str(path)])
    else:
        subprocess.Popen(["xdg-open", str(path)])
class JsonStore:
    def __init__(self, path: Path, default):
        self.path = path
        self._lock = threading.RLock()
        self._default = default
        if not self.path.exists():
            self._write(default)
    def _write(self, data):
        tmp = self.path.with_suffix(".tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
        os.replace(tmp, self.path)
    def _read(self):
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                return json.load(f)
        except (json.JSONDecodeError, FileNotFoundError, OSError):
            return json.loads(json.dumps(self._default))
    def read(self):
        with self._lock:
            return self._read()
    def write(self, data):
        with self._lock:
            self._write(data)
    def update(self, mutator):
        with self._lock:
            data = self._read()
            result = mutator(data)
            if result is not None:
                data = result
            self._write(data)
            return data
accounts_store = JsonStore(ACCOUNTS_FILE, [])
settings_store = JsonStore(SETTINGS_FILE, {
    "active_account": None,
    "active_instance": None,
    "memory_mb": 2048,
    "java_path": None,
    "ms_warning_seen": False,
    "sidebar_collapsed": False,
    "playtime_seconds": 0,
})
def _arch_matches(rule_arch: str) -> bool:
    cur = current_arch()
    if rule_arch == "x86":
        return cur == "32"
    if rule_arch in ("arm64", "aarch64"):
        return cur == "arm64"
    if rule_arch in ("x86_64", "amd64", "x64"):
        return cur == "64"
    return True
def rule_matches(rule: dict, features: dict) -> bool:
    os_rule = rule.get("os")
    if os_rule:
        name = os_rule.get("name")
        if name and name != current_os_name():
            return False
        arch = os_rule.get("arch")
        if arch and not _arch_matches(arch):
            return False
    features_rule = rule.get("features")
    if features_rule:
        for key, expected in features_rule.items():
            if bool(features.get(key, False)) != bool(expected):
                return False
    return True
def rules_allow(rules, features=None) -> bool:
    if not rules:
        return True
    features = features or {}
    allowed = False
    for rule in rules:
        if rule_matches(rule, features):
            allowed = rule.get("action", "disallow") == "allow"
    return allowed
def substitute(template: str, replacements: dict) -> str:
    def repl(match):
        key = match.group(1)
        val = replacements.get(key)
        return str(val) if val is not None else match.group(0)
    return re.sub(r"\$\{([a-zA-Z0-9_.]+)\}", repl, template)
def resolve_argument_list(spec_list, features, replacements) -> list:
    out = []
    for item in spec_list:
        if isinstance(item, str):
            out.append(substitute(item, replacements))
        elif isinstance(item, dict):
            if rules_allow(item.get("rules", []), features):
                value = item.get("value", "")
                if isinstance(value, list):
                    out.extend(substitute(v, replacements) for v in value)
                else:
                    out.append(substitute(value, replacements))
    return out
def merge_arguments(parent, child):
    if not parent and not child:
        return None
    parent = parent or {}
    child = child or {}
    return {
        "jvm": list(parent.get("jvm", [])) + list(child.get("jvm", [])),
        "game": list(parent.get("game", [])) + list(child.get("game", [])),
    }
DEFAULT_MODERN_JVM_ARGS = [
    "-Djava.library.path=${natives_directory}",
    "-Dminecraft.launcher.brand=crit-launcher",
    "-Dminecraft.launcher.version=1.0",
    "-cp",
    "${classpath}",
]
_JAVA_PROBE_CACHE: dict = {}
_JAVA_PROBE_LOCK = threading.Lock()
_JAVA_UI_CACHE = {"at": 0.0, "value": (None, None)}
def console_java(path: str) -> str:
    p = Path(path)
    if p.name.lower() == "javaw.exe":
        sibling = p.with_name("java.exe")
        if sibling.exists():
            return str(sibling)
    return str(path)
def _parse_java_major(text: str):
    m = re.search(r'version "(\d+)(?:\.(\d+))?', text)
    if m:
        major = int(m.group(1))
        if major == 1 and m.group(2):
            major = int(m.group(2))
        return major
    return None
def probe_java(path: str):
    exe = console_java(path)
    with _JAVA_PROBE_LOCK:
        if exe in _JAVA_PROBE_CACHE:
            return _JAVA_PROBE_CACHE[exe]
    result = (None, None)
    try:
        proc = subprocess.run(
            [exe, "-version"], capture_output=True, text=True, errors="replace",
            timeout=15, creationflags=CREATE_NO_WINDOW,
        )
        text = (proc.stderr or proc.stdout or "")
        lines = text.splitlines()
        result = (_parse_java_major(text), lines[0] if lines else None)
    except Exception:
        result = (None, None)
    with _JAVA_PROBE_LOCK:
        _JAVA_PROBE_CACHE[exe] = result
    return result
def java_compatible(major, required: int) -> bool:
    if major is None:
        return True  
    if required <= 8:
        return major == 8
    return major >= required
def _java_exe_names():
    return ("java.exe",) if current_os_name() == "windows" else ("java",)
def find_system_javas() -> list:
    found = []
    seen = set()
    def add(p):
        try:
            p = Path(p)
            if not p.is_file():
                return
            key = str(p.resolve())
        except OSError:
            return
        if key in seen:
            return
        seen.add(key)
        found.append(str(p))
    for exe in ("java", "java.exe"):
        w = shutil.which(exe)
        if w:
            add(w)
    java_home = os.environ.get("JAVA_HOME")
    if java_home:
        for n in _java_exe_names():
            add(Path(java_home) / "bin" / n)
    home = Path.home()
    system = current_os_name()
    globs = []
    if system == "windows":
        vendors = ["Java", "Eclipse Adoptium", "Eclipse Foundation", "Microsoft", "Zulu",
                   "BellSoft", "Amazon Corretto", "Semeru", "AdoptOpenJDK", "OpenJDK"]
        for env in ("ProgramFiles", "ProgramFiles(x86)"):
            base = os.environ.get(env)
            if base:
                for v in vendors:
                    globs.append((Path(base) / v, "*/bin"))
        appdata = os.environ.get("APPDATA")
        if appdata:
            globs.append((Path(appdata) / ".minecraft" / "runtime", "*/*/*/bin"))
        local = os.environ.get("LOCALAPPDATA")
        if local:
            globs.append((Path(local) / "Packages", "Microsoft.4297127D64EC6_*/LocalCache/Local/runtime/*/*/*/bin"))
    elif system == "osx":
        globs.append((Path("/Library/Java/JavaVirtualMachines"), "*/Contents/Home/bin"))
        globs.append((home / "Library/Application Support/minecraft/runtime", "*/*/*/jre.bundle/Contents/Home/bin"))
    else:
        globs.append((Path("/usr/lib/jvm"), "*/bin"))
        globs.append((home / ".sdkman/candidates/java", "*/bin"))
        globs.append((home / ".jdks", "*/bin"))
        globs.append((home / ".minecraft/runtime", "*/*/*/bin"))
    for base, pattern in globs:
        try:
            if not base.exists():
                continue
            for bin_dir in base.glob(pattern):
                for n in _java_exe_names():
                    add(bin_dir / n)
        except OSError:
            pass
    return found
def mojang_runtime_platform() -> str:
    system = current_os_name()
    arch = current_arch()
    if system == "windows":
        return {"64": "windows-x64", "32": "windows-x86", "arm64": "windows-arm64"}.get(arch, "windows-x64")
    if system == "osx":
        return "mac-os-arm64" if arch == "arm64" else "mac-os"
    return "linux-i386" if arch == "32" else "linux"
def managed_java_path(component: str):
    root = RUNTIME_DIR / component
    if not (root / ".crit_installed").exists():
        return None
    for rel in ("bin/java.exe", "bin/java", "jre.bundle/Contents/Home/bin/java"):
        p = root / rel
        if p.exists():
            return str(p)
    return None
def install_managed_java(component: str, tracker):
    tracker = tracker or ProgressTracker(lambda *_: None)
    tracker.set_task("Looking up Java runtime", force=True)
    plat = mojang_runtime_platform()
    index = http_get_json(JAVA_RUNTIME_INDEX_URL, timeout=30)
    entries = (index.get(plat) or {}).get(component) or []
    if not entries:
        raise RuntimeError(
            f"Mojang has no '{component}' Java runtime for {plat}. Install a matching Java "
            "yourself and set its path in Settings.")
    manifest = http_get_json(entries[0]["manifest"]["url"], timeout=30)
    files = manifest.get("files", {})
    root = RUNTIME_DIR / component
    root.mkdir(parents=True, exist_ok=True)
    items, executables, links = [], [], []
    total = 0
    for rel, info in files.items():
        target = root / rel
        if not is_within(root, target):
            continue
        kind = info.get("type")
        if kind == "directory":
            target.mkdir(parents=True, exist_ok=True)
        elif kind == "file":
            raw = (info.get("downloads") or {}).get("raw")
            if not raw:
                continue
            items.append(({"path": rel, "url": raw["url"], "size": raw.get("size"),
                           "sha1": raw.get("sha1")}, root))
            total += raw.get("size") or 0
            if info.get("executable"):
                executables.append(target)
        elif kind == "link":
            links.append((target, info.get("target")))
    tracker.reset(max(total, 1))
    tracker.set_task(f"Downloading Java runtime ({component})", force=True)
    download_all(items, tracker, workers=12)
    if current_os_name() != "windows":
        for target in executables:
            try:
                os.chmod(target, 0o755)
            except OSError:
                pass
        for target, link_to in links:
            try:
                if link_to and not target.exists():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    os.symlink(link_to, target)
            except OSError:
                pass
    (root / ".crit_installed").write_text(str(time.time()), encoding="utf-8")
    path = managed_java_path(component)
    if not path:
        raise RuntimeError("Java runtime was downloaded but no java executable was found in it.")
    return path
def java_info_from_meta(meta: dict) -> dict:
    return {
        "component": meta.get("java_component") or DEFAULT_JAVA_INFO["component"],
        "majorVersion": int(meta.get("java_major") or DEFAULT_JAVA_INFO["majorVersion"]),
    }
def resolve_java(java_info, allow_download=False, tracker=None):
    info = java_info or DEFAULT_JAVA_INFO
    required = int(info.get("majorVersion") or 8)
    component = info.get("component") or "jre-legacy"
    configured = (settings_store.read().get("java_path") or "").strip()
    if configured and Path(configured).exists():
        major, _ = probe_java(configured)
        if java_compatible(major, required):
            return configured
        logging.info("Configured Java (%s) is not compatible with Java %s", major, required)
    managed = managed_java_path(component)
    if managed:
        return managed
    best = None
    for cand in find_system_javas():
        major, _ = probe_java(cand)
        if major is None or not java_compatible(major, required):
            continue
        if best is None or major < best[0]:
            best = (major, cand)
    if best:
        return best[1]
    if allow_download:
        return install_managed_java(component, tracker)
    return None
def detect_java_for_ui():
    now = time.time()
    if now - _JAVA_UI_CACHE["at"] < 20 and _JAVA_UI_CACHE["at"] > 0:
        return _JAVA_UI_CACHE["value"]
    value = (None, None)
    configured = (settings_store.read().get("java_path") or "").strip()
    if configured and Path(configured).exists():
        value = (configured, probe_java(configured)[1])
    else:
        best = None
        for cand in find_system_javas():
            major, line = probe_java(cand)
            if major is not None and (best is None or major > best[0]):
                best = (major, cand, line)
        if best:
            value = (best[1], best[2])
    _JAVA_UI_CACHE["at"] = now
    _JAVA_UI_CACHE["value"] = value
    return value
class MetaCache:
    def __init__(self):
        self._lock = threading.Lock()
        self.manifest = None
        self.fabric_loaders = None
        self.fabric_loaders_by_mc = {}
        self.fabric_games = None
        self.forge_all = None
    def get_manifest(self, force=False):
        with self._lock:
            if self.manifest is not None and not force:
                return self.manifest
        manifest = None
        try:
            manifest = http_get_json(VERSION_MANIFEST_URL)
        except Exception:
            log_exception("Failed to download version manifest")
            cache_file = VERSIONS_DIR / "version_manifest_v2.json"
            if cache_file.exists():
                try:
                    manifest = json.loads(cache_file.read_text(encoding="utf-8"))
                except Exception:
                    log_exception("Failed to read cached manifest")
        if manifest is None:
            raise RuntimeError("Failed to load versions.")
        try:
            (VERSIONS_DIR / "version_manifest_v2.json").write_text(
                json.dumps(manifest), encoding="utf-8")
        except OSError:
            pass
        with self._lock:
            self.manifest = manifest
        return manifest
    def get_fabric_loaders(self, mc_version=None, force=False):
        with self._lock:
            if mc_version:
                if mc_version in self.fabric_loaders_by_mc and not force:
                    return self.fabric_loaders_by_mc[mc_version]
            elif self.fabric_loaders is not None and not force:
                return self.fabric_loaders
        try:
            if mc_version:
                try:
                    loaders = http_get_json(
                        f"{FABRIC_LOADER_LIST_URL}/{urllib.parse.quote(mc_version)}")
                except urllib.error.HTTPError:
                    loaders = []  
            else:
                loaders = http_get_json(FABRIC_LOADER_LIST_URL)
        except Exception:
            log_exception("Failed to download fabric loader list")
            raise RuntimeError("Fabric loader could not be retrieved.")
        with self._lock:
            if mc_version:
                self.fabric_loaders_by_mc[mc_version] = loaders
            else:
                self.fabric_loaders = loaders
        return loaders
    def get_fabric_game_versions(self, force=False):
        with self._lock:
            if self.fabric_games is not None and not force:
                return self.fabric_games
        try:
            games = http_get_json(FABRIC_GAME_LIST_URL)
        except Exception:
            log_exception("Failed to download fabric game list")
            raise RuntimeError("Fabric versions could not be retrieved.")
        versions = [g.get("version") for g in games if g.get("version")]
        with self._lock:
            self.fabric_games = versions
        return versions
    def get_fabric_profile(self, mc_version: str, loader_version: str) -> dict:
        cache_path = VERSIONS_DIR / f"fabric-{loader_version}-{mc_version}.json"
        if cache_path.exists():
            try:
                return json.loads(cache_path.read_text(encoding="utf-8"))
            except Exception:
                pass
        url = FABRIC_PROFILE_URL_TMPL.format(
            mc=urllib.parse.quote(mc_version), loader=urllib.parse.quote(loader_version))
        data = http_get_json(url)
        cache_path.write_text(json.dumps(data), encoding="utf-8")
        return data
    def _forge_all_versions(self, force=False):
        with self._lock:
            if self.forge_all is not None and not force:
                return self.forge_all
        xml_text = http_get_text(FORGE_METADATA_URL)
        all_versions = re.findall(r"<version>([^<]+)</version>", xml_text)
        with self._lock:
            self.forge_all = all_versions
        return all_versions
    def get_forge_versions(self, mc_version: str, force=False):
        all_versions = self._forge_all_versions(force)
        prefix = mc_version + "-"
        matches = [v[len(prefix):] for v in all_versions if v.startswith(prefix)]
        matches.reverse()  
        return matches
    def get_forge_mc_versions(self, force=False):
        seen, out = set(), []
        for v in self._forge_all_versions(force):
            mc = v.split("-", 1)[0]
            if mc and mc not in seen:
                seen.add(mc)
                out.append(mc)
        return out
    def get_version_json(self, version_id: str, url: str) -> dict:
        cache_path = VERSIONS_DIR / version_id / f"{version_id}.json"
        if cache_path.exists():
            try:
                return json.loads(cache_path.read_text(encoding="utf-8"))
            except Exception:
                pass
        data = http_get_json(url)
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        cache_path.write_text(json.dumps(data), encoding="utf-8")
        return data
meta_cache = MetaCache()
_meta_lock = threading.RLock()
def instance_dir(name: str) -> Path:
    safe = sanitize_instance_name(name)
    return INSTANCES_DIR / safe
def read_instance_meta(name: str) -> dict | None:
    meta_path = instance_dir(name) / "instance.json"
    if not meta_path.exists():
        return None
    try:
        return json.loads(meta_path.read_text(encoding="utf-8"))
    except Exception:
        return None
def write_instance_meta(name: str, meta: dict):
    path = instance_dir(name) / "instance.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    with _meta_lock:
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(meta, indent=2), encoding="utf-8")
        os.replace(tmp, path)
def update_instance_meta(name: str, mutator):
    with _meta_lock:
        meta = read_instance_meta(name)
        if meta is None:
            return None
        mutator(meta)
        write_instance_meta(name, meta)
        return meta
def is_installed(meta: dict) -> bool:
    return bool(meta.get("installed")) and meta.get("schema") == INSTALL_SCHEMA
def list_instances() -> list:
    result = []
    if not INSTANCES_DIR.exists():
        return result
    for child in sorted(INSTANCES_DIR.iterdir(), key=lambda p: p.name.lower()):
        if child.is_dir():
            try:
                meta = read_instance_meta(child.name)
            except ValueError:
                continue
            if meta:
                result.append(meta)
    return result
def instance_game_dir(name: str) -> Path:
    return instance_dir(name) / "game"
def instance_mods_dir(name: str) -> Path:
    return instance_game_dir(name) / "mods"
def ensure_game_layout(name: str, loader: str):
    game = instance_game_dir(name)
    folders = ["resourcepacks", "shaderpacks", "saves"]
    if loader in ("fabric", "forge"):
        folders += ["mods", "config"]
    for f in folders:
        (game / f).mkdir(parents=True, exist_ok=True)
def unique_instance_name(base: str) -> str:
    cleaned = _INVALID_NAME_RE.sub("-", base or "").replace("..", "-").strip().rstrip(".")
    cleaned = (cleaned or "Modpack")[:56]
    candidate = cleaned
    n = 2
    while instance_dir(candidate).exists():
        candidate = f"{cleaned} ({n})"
        n += 1
    return candidate
class ProgressTracker:
    def __init__(self, emit_fn, total_estimate: int = 1):
        self.emit_fn = emit_fn
        self.event = "installProgress"
        self.lock = threading.Lock()
        self._emit_lock = threading.Lock()
        self.total = max(total_estimate, 1)
        self.downloaded = 0
        self.unit = "bytes"
        self.current_task = "Preparing"
        self._last_emit = 0.0
    def reset(self, total: int, unit: str = "bytes"):
        with self.lock:
            self.total = max(int(total), 1)
            self.downloaded = 0
            self.unit = unit
        self._emit(force=True)
    def set_total(self, total: int):
        with self.lock:
            self.total = max(total, 1)
    def set_task(self, task: str, force=False):
        with self.lock:
            self.current_task = task
        self._emit(force=True)
    def add(self, n: int):
        with self.lock:
            self.downloaded += n
        self._emit()
    def _emit(self, force=False):
        if force:
            self._emit_lock.acquire()
        elif not self._emit_lock.acquire(False):
            return
        try:
            now = time.time()
            if not force and now - self._last_emit < 0.08:
                return
            self._last_emit = now
            with self.lock:
                percent = min(100, int(self.downloaded * 100 / self.total))
                payload = {
                    "percent": percent,
                    "current": self.current_task,
                    "downloaded": self.downloaded,
                    "total": self.total,
                    "unit": self.unit,
                }
            self.emit_fn(self.event, payload)
        finally:
            self._emit_lock.release()
def maven_to_path(name: str) -> str:
    parts = name.split(":")
    group, artifact, version = parts[0], parts[1], parts[2]
    classifier = parts[3] if len(parts) > 3 else None
    ext = "jar"
    if "@" in version:
        version, ext = version.split("@", 1)
    if classifier and "@" in classifier:
        classifier, ext = classifier.split("@", 1)
    group_path = group.replace(".", "/")
    filename = f"{artifact}-{version}"
    if classifier:
        filename += f"-{classifier}"
    filename += f".{ext}"
    return f"{group_path}/{artifact}/{version}/{filename}"
def lib_key(name: str) -> str:
    parts = name.split(":")
    if len(parts) < 3:
        return name
    key = f"{parts[0]}:{parts[1]}"
    if len(parts) > 3:
        key += ":" + parts[3].split("@", 1)[0]
    return key
def fix_maven_url(url: str) -> str:
    url = url.replace("http://files.minecraftforge.net/maven/", "https://maven.minecraftforge.net/")
    if url.startswith("http://") and not url.startswith(("http://127.0.0.1", "http://localhost")):
        url = "https://" + url[len("http://"):]
    if not url.endswith("/"):
        url += "/"
    return url
def collect_library_entries(libraries: list, features=None):
    classpath_entries = []
    native_entries = []
    features = features or {}
    seen_keys = set()
    seen_native_paths = set()
    for lib in libraries:
        if lib.get("clientreq") is False:
            continue  
        if "rules" in lib and not rules_allow(lib["rules"], features):
            continue
        name = lib.get("name", "")
        downloads = lib.get("downloads") or {}
        artifact = downloads.get("artifact")
        if artifact and artifact.get("path"):
            key = lib_key(name) if name else artifact["path"]
            if key not in seen_keys:
                seen_keys.add(key)
                classpath_entries.append({
                    "path": artifact["path"],
                    "url": artifact.get("url", ""),
                    "size": artifact.get("size"),
                    "sha1": artifact.get("sha1"),
                })
        elif name and not downloads.get("classifiers"):
            key = lib_key(name)
            if key not in seen_keys:
                seen_keys.add(key)
                path = maven_to_path(name)
                base_url = fix_maven_url(lib.get("url") or LIBRARIES_BASE_URL)
                classpath_entries.append({
                    "path": path,
                    "url": base_url + path,
                    "size": lib.get("size"),
                    "sha1": lib.get("sha1"),
                })
        natives_map = lib.get("natives")
        if natives_map:
            classifier_key = natives_map.get(current_os_name())
            if classifier_key:
                classifier_key = classifier_key.replace("${arch}", current_arch())
                classifiers = downloads.get("classifiers", {})
                native_artifact = classifiers.get(classifier_key)
                if native_artifact and native_artifact.get("path"):
                    if native_artifact["path"] in seen_native_paths:
                        continue
                    seen_native_paths.add(native_artifact["path"])
                    exclude = lib.get("extract", {}).get("exclude", [])
                    native_entries.append({
                        "path": native_artifact["path"],
                        "url": native_artifact.get("url", ""),
                        "size": native_artifact.get("size"),
                        "sha1": native_artifact.get("sha1"),
                        "extract_exclude": exclude,
                    })
    return classpath_entries, native_entries
def entry_weight(entry: dict) -> int:
    return int(entry.get("size") or 300_000)
def ensure_file(entry: dict, dest_root: Path, tracker, retries: int = 3):
    dest = dest_root / entry["path"]
    quick_sha = None if entry.get("quick") else entry.get("sha1")
    if file_is_valid(dest, entry.get("size"), quick_sha):
        return dest
    if not entry.get("url"):
        raise RuntimeError(f"Failed to download library: {entry['path']} (no URL)")
    def cb(n):
        if tracker:
            tracker.add(n)
    last = None
    for attempt in range(retries):
        try:
            http_download(entry["url"], dest, entry.get("size"), entry.get("sha1"), cb)
            return dest
        except Exception as exc:
            last = exc
            time.sleep(1.0 * (attempt + 1))
    raise RuntimeError(f"Failed to download {entry['path']} ({last})") from last
def download_all(items, tracker, workers: int = 10):
    todo, missing, seen = [], [], set()
    for entry, root in items:
        dest = root / entry["path"]
        key = str(dest)
        if key in seen:
            continue
        seen.add(key)
        quick_sha = None if entry.get("quick") else entry.get("sha1")
        if file_is_valid(dest, entry.get("size"), quick_sha):
            tracker.add(entry_weight(entry))
            continue
        if not entry.get("url"):
            missing.append(entry)
            continue
        todo.append((entry, root))
    if not todo:
        return missing
    if len(todo) == 1 or workers <= 1:
        for entry, root in todo:
            ensure_file(entry, root, tracker)
        return missing
    with ThreadPoolExecutor(max_workers=min(workers, len(todo))) as pool:
        futures = [pool.submit(ensure_file, e, r, tracker) for e, r in todo]
        try:
            for fut in as_completed(futures):
                fut.result()
        except Exception:
            for fut in futures:
                fut.cancel()
            raise
    return missing
def extract_natives(native_paths_and_excludes, natives_dir: Path):
    shutil.rmtree(natives_dir, ignore_errors=True)
    natives_dir.mkdir(parents=True, exist_ok=True)
    for jar_path, exclude in native_paths_and_excludes:
        try:
            with zipfile.ZipFile(jar_path, "r") as zf:
                for member in zf.namelist():
                    if member.startswith("META-INF/") or member.endswith("/"):
                        continue
                    if any(member.startswith(ex.rstrip("/") + "/") or member == ex
                           for ex in exclude):
                        continue
                    if not is_within(natives_dir, natives_dir / member):
                        continue
                    zf.extract(member, natives_dir)
        except zipfile.BadZipFile:
            log_exception(f"Bad native jar: {jar_path}")
def _jar_main_class(jar_path: Path) -> str | None:
    try:
        with zipfile.ZipFile(jar_path) as zf:
            manifest = zf.read("META-INF/MANIFEST.MF").decode("utf-8", "ignore")
            manifest = manifest.replace("\r\n ", "").replace("\n ", "")
            m = re.search(r"Main-Class:\s*(\S+)", manifest)
            if m:
                return m.group(1)
    except Exception:
        pass
    return None
class ForgeInstallContext:
    def __init__(self, profile: dict, installer_jar: Path, tmp_dir: Path,
                 mc_version: str, client_path: Path):
        self.profile = profile
        self.installer_jar = installer_jar
        self.tmp_dir = tmp_dir
        self.tokens = {
            "SIDE": "client",
            "MINECRAFT_JAR": str(client_path),
            "MINECRAFT_VERSION": mc_version,
            "ROOT": str(DATA_DIR),
            "INSTALLER": str(installer_jar),
            "LIBRARY_DIR": str(LIBRARIES_DIR),
        }
        for key, val in (profile.get("data") or {}).items():
            client_val = val.get("client") if isinstance(val, dict) else val
            if client_val is not None:
                self.tokens[key] = self._data_value(str(client_val))
    def _data_value(self, value: str) -> str:
        if value.startswith("[") and value.endswith("]"):
            return str(LIBRARIES_DIR / maven_to_path(value[1:-1]))
        if len(value) >= 2 and value.startswith("'") and value.endswith("'"):
            return value[1:-1]
        if value.startswith("/"):
            member = value.lstrip("/")
            dest = self.tmp_dir / member
            if not dest.exists():
                dest.parent.mkdir(parents=True, exist_ok=True)
                with zipfile.ZipFile(self.installer_jar) as zf:
                    dest.write_bytes(zf.read(member))
            return str(dest)
        return value
    def resolve(self, arg: str) -> str:
        if arg.startswith("[") and arg.endswith("]"):
            return str(LIBRARIES_DIR / maven_to_path(arg[1:-1]))
        out = re.sub(r"\{([A-Za-z0-9_]+)\}",
                     lambda m: self.tokens.get(m.group(1), m.group(0)), arg)
        if len(out) >= 2 and out.startswith("'") and out.endswith("'"):
            out = out[1:-1]
        return out
    def outputs_valid(self, proc: dict) -> bool:
        outputs = proc.get("outputs") or {}
        if not outputs:
            return False
        for key, expected in outputs.items():
            path = Path(self.resolve(key))
            want = self.resolve(expected)
            if not path.is_file():
                return False
            try:
                if file_sha1(path) != want:
                    return False
            except OSError:
                return False
        return True
class Installer:
    def __init__(self, api: "API", meta: dict):
        self.api = api
        self.name = sanitize_instance_name(meta["name"])
        self.version_id = meta["version"]
        self.loader = meta.get("loader") or "vanilla"
        self.loader_version = meta.get("loader_version")
    def run(self) -> bool:
        emit = self.api.emit
        forge_tmp = None
        try:
            emit("installStarted", {"instance": self.name,
                                    "title": f"Installing {self.name}..."})
            target_dir = instance_dir(self.name)
            target_dir.mkdir(parents=True, exist_ok=True)
            tracker = ProgressTracker(emit)
            tracker.set_task("Fetching version metadata", force=True)
            manifest = meta_cache.get_manifest()
            version_entry = next(
                (v for v in manifest["versions"] if v["id"] == self.version_id), None)
            if not version_entry:
                raise RuntimeError(f"Minecraft {self.version_id} was not found in Mojang's version list.")
            vanilla_json = meta_cache.get_version_json(self.version_id, version_entry["url"])
            java_info = vanilla_json.get("javaVersion") or dict(DEFAULT_JAVA_INFO)
            java_path = resolve_java(java_info, allow_download=True, tracker=tracker)
            if not java_path:
                raise RuntimeError("Could not find or download a suitable Java runtime.")
            asset_index_info = vanilla_json.get("assetIndex", {})
            asset_index_id = asset_index_info.get("id", self.version_id)
            asset_index_path = ASSET_INDEXES_DIR / f"{asset_index_id}.json"
            tracker.set_task("Fetching asset index", force=True)
            if asset_index_path.exists():
                asset_index_data = json.loads(asset_index_path.read_text(encoding="utf-8"))
            elif asset_index_info.get("url"):
                asset_index_data = http_get_json(asset_index_info["url"])
                asset_index_path.write_text(json.dumps(asset_index_data), encoding="utf-8")
            else:
                asset_index_data = {"objects": {}}
            asset_objects = asset_index_data.get("objects", {})
            if self.loader == "fabric":
                plan = self._plan_fabric(vanilla_json, tracker)
            elif self.loader == "forge":
                plan, forge_tmp = self._plan_forge(vanilla_json, tracker)
            else:
                plan = self._plan_vanilla(vanilla_json)
            cp_entries, native_entries = collect_library_entries(plan["libraries"])
            client_dl = vanilla_json.get("downloads", {}).get("client", {})
            client_entry = {
                "path": f"{self.version_id}/{self.version_id}.jar",
                "url": client_dl.get("url", ""),
                "size": client_dl.get("size"),
                "sha1": client_dl.get("sha1"),
            }
            asset_items, seen_hashes = [], set()
            for _name, obj in asset_objects.items():
                obj_hash = obj.get("hash")
                if not obj_hash or obj_hash in seen_hashes:
                    continue
                seen_hashes.add(obj_hash)
                prefix = obj_hash[:2]
                asset_items.append(({
                    "path": f"{prefix}/{obj_hash}",
                    "url": f"{RESOURCES_BASE_URL}/{prefix}/{obj_hash}",
                    "size": obj.get("size"),
                    "sha1": obj_hash,
                    "quick": True,
                }, ASSET_OBJECTS_DIR))
            lib_items = [(e, LIBRARIES_DIR)
                         for e in cp_entries + native_entries + plan.get("installer_entries", [])]
            total = entry_weight(client_entry)
            total += sum(entry_weight(e) for e, _ in lib_items)
            total += sum(int(e.get("size") or 0) for e, _ in asset_items)
            tracker.reset(total)
            tracker.set_task("Downloading client", force=True)
            download_all([(client_entry, VERSIONS_DIR)], tracker)
            client_path = VERSIONS_DIR / client_entry["path"]
            tracker.set_task("Downloading libraries", force=True)
            download_all(lib_items, tracker)
            tracker.set_task("Downloading assets", force=True)
            download_all(asset_items, tracker, workers=16)
            post = plan.get("post")
            if post:
                post(client_path, java_path, tracker)
            missing = [e["path"] for e in cp_entries
                       if not (LIBRARIES_DIR / e["path"]).is_file()]
            missing += [e["path"] for e in native_entries
                        if not (LIBRARIES_DIR / e["path"]).is_file()]
            if missing:
                raise RuntimeError(
                    "Some files are still missing after install: " + ", ".join(missing[:3]))
            tracker.set_task("Extracting natives", force=True)
            natives_dir = target_dir / "natives"
            extract_natives(
                [(LIBRARIES_DIR / e["path"], e.get("extract_exclude", [])) for e in native_entries],
                natives_dir)
            tracker.set_task("Finalizing instance", force=True)
            ensure_game_layout(self.name, self.loader)
            classpath = ["libraries/" + e["path"] for e in cp_entries]
            classpath.append(f"versions/{client_entry['path']}")
            meta = read_instance_meta(self.name) or {}
            meta.update({
                "name": self.name,
                "version": self.version_id,
                "loader": self.loader,
                "loader_version": self.loader_version,
                "main_class": plan["main_class"],
                "asset_index": asset_index_id,
                "arguments": plan.get("arguments"),
                "minecraft_arguments": plan.get("minecraft_arguments"),
                "classpath": classpath,
                "java_component": java_info.get("component", DEFAULT_JAVA_INFO["component"]),
                "java_major": int(java_info.get("majorVersion", DEFAULT_JAVA_INFO["majorVersion"])),
                "installed": True,
                "schema": INSTALL_SCHEMA,
                "created": meta.get("created", time.time()),
            })
            write_instance_meta(self.name, meta)
            def _set_active(s):
                if not s.get("active_instance"):
                    s["active_instance"] = self.name
            settings_store.update(_set_active)
            emit("installFinished", {"instance": self.name})
            self.api.push_state()
            return True
        except Exception as exc:
            log_exception(f"Install failed for {self.name}")
            emit("installError", {"message": str(exc) or "Installation failed."})
            return False
        finally:
            if forge_tmp:
                shutil.rmtree(forge_tmp, ignore_errors=True)
    def _plan_vanilla(self, vanilla_json):
        return {
            "main_class": vanilla_json["mainClass"],
            "libraries": list(vanilla_json.get("libraries", [])),
            "arguments": merge_arguments(vanilla_json.get("arguments"), None),
            "minecraft_arguments": vanilla_json.get("minecraftArguments"),
        }
    def _plan_fabric(self, vanilla_json, tracker):
        if not self.loader_version:
            raise RuntimeError("No Fabric loader version selected.")
        tracker.set_task("Fetching Fabric metadata", force=True)
        try:
            profile = meta_cache.get_fabric_profile(self.version_id, self.loader_version)
        except urllib.error.HTTPError as exc:
            raise RuntimeError(
                f"Fabric loader {self.loader_version} isn't available for Minecraft "
                f"{self.version_id} (HTTP {exc.code}).") from exc
        main_class = profile.get("mainClass") or vanilla_json["mainClass"]
        if isinstance(main_class, dict):
            main_class = main_class.get("client") or vanilla_json["mainClass"]
        return {
            "main_class": main_class,
            "libraries": list(profile.get("libraries", [])) + list(vanilla_json.get("libraries", [])),
            "arguments": merge_arguments(vanilla_json.get("arguments"), profile.get("arguments")),
            "minecraft_arguments": profile.get("minecraftArguments") or vanilla_json.get("minecraftArguments"),
        }
    def _plan_forge(self, vanilla_json, tracker):
        if not self.loader_version:
            raise RuntimeError("No Forge version selected.")
        mc_version = self.version_id
        full_version = f"{mc_version}-{self.loader_version}"
        installer_jar = VERSIONS_DIR / f"forge-installer-{full_version}.jar"
        installer_url = f"{FORGE_MAVEN}{full_version}/forge-{full_version}-installer.jar"
        tracker.set_task("Downloading Forge installer", force=True)
        tracker.reset(8_000_000)
        if installer_jar.exists() and not zipfile.is_zipfile(installer_jar):
            installer_jar.unlink(missing_ok=True)
        if not installer_jar.exists():
            try:
                http_download(installer_url, installer_jar, progress_cb=tracker.add)
            except urllib.error.HTTPError as exc:
                raise RuntimeError(
                    f"Forge {self.loader_version} for Minecraft {mc_version} could not be "
                    f"downloaded (HTTP {exc.code}).") from exc
        tmp_dir = TMP_DIR / f"forge-{full_version}"
        shutil.rmtree(tmp_dir, ignore_errors=True)
        tmp_dir.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(installer_jar) as zf:
            profile = json.loads(zf.read("install_profile.json").decode("utf-8"))
            legacy = "versionInfo" in profile
            if legacy:
                version_json = profile["versionInfo"]
            else:
                json_member = (profile.get("json") or "/version.json").lstrip("/")
                version_json = json.loads(zf.read(json_member).decode("utf-8"))
            for member in zf.namelist():
                if member.endswith("/") or not member.startswith("maven/"):
                    continue
                dest = LIBRARIES_DIR / member[len("maven/"):]
                if not is_within(LIBRARIES_DIR, dest):
                    continue
                if not dest.exists():
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(zf.read(member))
            if legacy:
                install = profile.get("install", {})
                file_path, art = install.get("filePath"), install.get("path")
                if file_path and art:
                    dest = LIBRARIES_DIR / maven_to_path(art)
                    if not dest.exists():
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        dest.write_bytes(zf.read(file_path))
        installer_entries = []
        if not legacy:
            installer_entries, _ = collect_library_entries(list(profile.get("libraries", [])))
        plan = {
            "main_class": version_json.get("mainClass", vanilla_json["mainClass"]),
            "libraries": list(version_json.get("libraries", [])) + list(vanilla_json.get("libraries", [])),
            "arguments": merge_arguments(vanilla_json.get("arguments"), version_json.get("arguments")),
            "minecraft_arguments": version_json.get("minecraftArguments") or vanilla_json.get("minecraftArguments"),
            "installer_entries": installer_entries,
        }
        if not legacy and profile.get("processors"):
            def post(client_path, java_path, trk):
                self._run_forge_processors(profile, installer_jar, tmp_dir, mc_version,
                                           client_path, java_path, trk)
            plan["post"] = post
        return plan, tmp_dir
    def _run_forge_processors(self, profile, installer_jar, tmp_dir, mc_version,
                              client_path, java_path, tracker):
        ctx = ForgeInstallContext(profile, installer_jar, tmp_dir, mc_version, client_path)
        procs = [p for p in profile.get("processors", [])
                 if not p.get("sides") or "client" in p["sides"]]
        if not procs:
            return
        java_exe = console_java(java_path)
        tracker.reset(len(procs), unit="steps")
        for index, proc in enumerate(procs, 1):
            tracker.set_task(f"Setting up Forge ({index}/{len(procs)})", force=True)
            if ctx.outputs_valid(proc):
                tracker.add(1)
                continue
            jar_rel = maven_to_path(proc["jar"])
            jar_path = LIBRARIES_DIR / jar_rel
            if not jar_path.is_file():
                raise RuntimeError(f"Missing Forge tool: {proc['jar']}")
            cp_parts = [str(jar_path)]
            for coord in proc.get("classpath", []):
                cp = LIBRARIES_DIR / maven_to_path(coord)
                if not cp.is_file():
                    raise RuntimeError(f"Missing Forge tool library: {coord}")
                cp_parts.append(str(cp))
            main_class = _jar_main_class(jar_path)
            if not main_class:
                raise RuntimeError(f"Forge tool has no main class: {proc['jar']}")
            args = [ctx.resolve(a) for a in proc.get("args", [])]
            cmd = [java_exe, "-cp", classpath_sep().join(cp_parts), main_class] + args
            logging.info("Forge processor %s/%s: %s", index, len(procs), " ".join(cmd)[:1500])
            result = subprocess.run(
                cmd, cwd=str(tmp_dir), capture_output=True, text=True, errors="replace",
                timeout=1200, creationflags=CREATE_NO_WINDOW,
            )
            if result.returncode != 0:
                tail = ((result.stderr or "") + (result.stdout or ""))[-800:].strip()
                logging.error("Forge processor failed (%s): %s", main_class, tail)
                raise RuntimeError(f"Forge setup step failed ({main_class.split('.')[-1]}): {tail}")
            tracker.add(1)
def build_launch_command(meta: dict, account: dict, memory_mb: int, java_exe: str) -> list:
    name = meta["name"]
    game_dir = instance_game_dir(name)
    natives_dir = instance_dir(name) / "natives"
    game_dir.mkdir(parents=True, exist_ok=True)
    classpath_list = [str(DATA_DIR / rel) for rel in meta["classpath"]]
    replacements = {
        "auth_player_name": account["name"],
        "auth_uuid": account["uuid"].replace("-", "") if account.get("type") == "microsoft" else account["uuid"],
        "auth_access_token": account.get("mc_token") or "0" if account.get("type") == "microsoft" else "0",
        "auth_session": "0",
        "auth_xuid": "0",
        "clientid": "0",
        "user_type": "msa" if account.get("type") == "microsoft" else "legacy",
        "user_properties": "{}",
        "version_name": meta["version"],
        "version_type": "release",
        "game_directory": str(game_dir),
        "assets_root": str(ASSETS_DIR),
        "assets_index_name": meta.get("asset_index") or meta["version"],
        "natives_directory": str(natives_dir),
        "library_directory": str(LIBRARIES_DIR),
        "classpath_separator": classpath_sep(),
        "classpath": classpath_sep().join(classpath_list),
        "launcher_name": "CritLauncher",
        "launcher_version": "1.0",
    }
    features = {}
    args_spec = meta.get("arguments") or {}
    if args_spec.get("jvm"):
        jvm_args = resolve_argument_list(args_spec["jvm"], features, replacements)
    else:
        jvm_args = [substitute(a, replacements) for a in DEFAULT_MODERN_JVM_ARGS]
    if not any(a in ("-cp", "-classpath") for a in jvm_args):
        jvm_args = ["-cp", replacements["classpath"]] + jvm_args
    if not any(a.startswith("-Djava.library.path=") for a in jvm_args):
        jvm_args = [f"-Djava.library.path={natives_dir}"] + jvm_args
    if args_spec.get("game"):
        game_args = resolve_argument_list(args_spec["game"], features, replacements)
    elif meta.get("minecraft_arguments"):
        game_args = [substitute(tok, replacements)
                     for tok in meta["minecraft_arguments"].split(" ") if tok]
    else:
        game_args = [
            "--username", replacements["auth_player_name"],
            "--version", replacements["version_name"],
            "--gameDir", replacements["game_directory"],
            "--assetsDir", replacements["assets_root"],
            "--assetIndex", replacements["assets_index_name"],
            "--uuid", replacements["auth_uuid"],
            "--accessToken", replacements["auth_access_token"],
            "--userType", replacements["user_type"],
            "--versionType", replacements["version_type"],
        ]
    command = [java_exe, f"-Xmx{memory_mb}M", f"-Xms{min(memory_mb, 1024)}M"]
    command.extend(jvm_args)
    command.append(meta["main_class"])
    command.extend(game_args)
    return command
def _log_tail(path: Path, max_bytes: int = 4000) -> str:
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            f.seek(max(0, size - max_bytes))
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""
def _explain_crash(tail: str) -> str:
    if "UnsupportedClassVersionError" in tail or "class file version" in tail:
        return "This Minecraft version needs a newer Java. Clear the custom Java path in Settings so Crit Launcher can pick one."
    if "Could not create the Java Virtual Machine" in tail or "Invalid maximum heap size" in tail:
        return "Java couldn't start with that memory setting. Try lowering the allocated memory in Settings."
    if "ClassNotFoundException" in tail or "NoClassDefFoundError" in tail:
        return "A library is missing. Delete the instance's files by re-installing it, then try again."
    lines = [ln.strip() for ln in tail.splitlines() if ln.strip()]
    return lines[-1][:220] if lines else "Check launch_output.log in the instance folder."
def pick_best_version(versions: list):
    if not versions:
        return None
    for wanted in ("release", "beta", "alpha"):
        for v in versions:
            if v.get("version_type") == wanted:
                return v
    return versions[0]
def primary_file(version: dict):
    files = version.get("files", [])
    return next((f for f in files if f.get("primary")), files[0] if files else None)
class AuthError(Exception):
    pass
def ensure_fresh_account(account: dict) -> dict:
    if account.get("type") != "microsoft":
        return account
    if account.get("mc_token") and float(account.get("mc_expires", 0)) - time.time() > 60:
        return account
    raise AuthError("This session has expired. Sign in again or import a new token.")
def public_account(a: dict) -> dict:
    out = {"name": a.get("name"), "uuid": a.get("uuid"), "type": a.get("type", "offline")}
    if a.get("type") == "microsoft":
        out["session"] = True
        out["expires"] = a.get("mc_expires")
    return out
def jwt_expiry(token: str):
    try:
        payload = token.split(".")[1]
        payload += "=" * (-len(payload) % 4)
        return float(json.loads(base64.urlsafe_b64decode(payload.encode("ascii")))["exp"])
    except Exception:
        return None
def _parse_iso_time(text):
    if not text:
        return None
    try:
        from datetime import datetime, timezone
        t = str(text).replace("Z", "+00:00")
        dt = datetime.fromisoformat(t)
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
        return dt.timestamp()
    except Exception:
        return None
def official_launcher_accounts_file() -> Path:
    system = current_os_name()
    home = Path.home()
    if system == "windows":
        base = Path(os.environ.get("APPDATA") or (home / "AppData" / "Roaming")) / ".minecraft"
    elif system == "osx":
        base = home / "Library" / "Application Support" / "minecraft"
    else:
        base = home / ".minecraft"
    return base / "launcher_accounts.json"
def read_official_sessions() -> list:
    path = official_launcher_accounts_file()
    if not path.is_file():
        raise AuthError("Couldn't find the official Minecraft launcher's account file on this PC.")
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except Exception:
        raise AuthError("The official launcher's account file couldn't be read.")
    found = []
    for a in (data.get("accounts") or {}).values():
        token = a.get("accessToken")
        prof = a.get("minecraftProfile") or {}
        if not token or not prof.get("id"):
            continue
        found.append({"token": token, "name": prof.get("name"), "id": prof["id"],
                      "expires": _parse_iso_time(a.get("accessTokenExpiresAt")) or jwt_expiry(token)})
    return found
def profile_for_token(token: str):
    status, prof = http_get_auth(MC_PROFILE_URL, token)
    if status in (401, 403):
        raise AuthError("That session token isn't valid (it has probably expired).")
    if status == 404:
        raise AuthError("That account doesn't own Minecraft: Java Edition.")
    if status != 200 or "id" not in prof:
        raise AuthError(f"Couldn't check the token (HTTP {status}).")
    return prof["name"], str(uuid.UUID(prof["id"]))
def save_session_account(name: str, account_uuid: str, token: str, expires) -> dict:
    account = {"name": name, "uuid": account_uuid, "type": "microsoft", "session_only": True,
               "mc_token": token, "mc_expires": expires or (time.time() + 3600)}
    def upsert(accounts):
        for a in accounts:
            if a.get("uuid") == account_uuid:
                a["name"], a["mc_token"], a["mc_expires"] = name, account["mc_token"], account["mc_expires"]
                if not a.get("refresh_token"):
                    a["session_only"] = True
                return
        accounts.append(account)
    accounts_store.update(upsert)
    settings_store.update(lambda st: st.__setitem__("active_account", account_uuid))
    return account
def find_account(accounts: list, key):
    if not key:
        return None
    return (next((a for a in accounts if a.get("uuid") == key), None)
            or next((a for a in accounts if a.get("name") == key), None))
PNG_SIG = b"\x89PNG\r\n\x1a\n"
def http_send(method: str, url: str, token=None, data=None, content_type=None, timeout: int = 30):
    hdrs = {"User-Agent": USER_AGENT, "Accept": "application/json"}
    if token:
        hdrs["Authorization"] = f"Bearer {token}"
    if content_type:
        hdrs["Content-Type"] = content_type
    req = urllib.request.Request(url, data=data, headers=hdrs, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, _json_or_text(resp.read())
    except urllib.error.HTTPError as exc:
        return exc.code, _json_or_text(exc.read())
def decode_png_uri(uri: str) -> bytes:
    m = re.match(r"^data:image/png;base64,([A-Za-z0-9+/=\s]+)$", uri or "")
    if not m:
        raise ValueError("That isn't a PNG image.")
    try:
        data = base64.b64decode(m.group(1))
    except Exception:
        raise ValueError("That image couldn't be read.")
    if not data.startswith(PNG_SIG) or len(data) < 33:
        raise ValueError("That isn't a PNG image.")
    if len(data) > 200_000:
        raise ValueError("That file is too big to be a skin.")
    width, height = struct.unpack(">II", data[16:24])
    if (width, height) not in ((64, 64), (64, 32)):
        raise ValueError("Skins must be 64x64 (or 64x32) pixels.")
    return data
def png_uri(data: bytes) -> str:
    return "data:image/png;base64," + base64.b64encode(data).decode("ascii")
def _texture_host_ok(host: str) -> bool:
    return host == "minecraft.net" or host.endswith(".minecraft.net")
def fetch_texture_uri(url):
    if not url:
        return None
    if url.startswith("http://textures.minecraft.net"):
        url = "https://" + url[len("http://"):]
    if not _texture_host_ok(urllib.parse.urlparse(url).hostname or ""):
        return None
    try:
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=20) as resp:
            data = resp.read(400_001)
        if len(data) > 400_000 or not data.startswith(PNG_SIG):
            return None
        return png_uri(data)
    except Exception:
        return None
def multipart_skin(variant: str, png: bytes):
    boundary = "----CritLauncher" + uuid.uuid4().hex
    head = (f'--{boundary}\r\nContent-Disposition: form-data; name="variant"\r\n\r\n{variant}\r\n'
            f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="skin.png"\r\n'
            f'Content-Type: image/png\r\n\r\n').encode("utf-8")
    return head + png + f"\r\n--{boundary}--\r\n".encode("utf-8"), f"multipart/form-data; boundary={boundary}"
def _skin_error(status: int) -> str:
    if status in (401, 403):
        return "Your session has expired. Sign in again."
    if status == 400:
        return "Minecraft rejected that request."
    if status == 429:
        return "Too many changes in a short time. Wait a bit and try again."
    return f"Minecraft answered with an error (HTTP {status})."
def _fetch_named_skin(name: str):
    try:
        found = http_get_json(MOJANG_LOOKUP_URL + urllib.parse.quote(name), retries=1)
    except Exception:
        return None
    if not found or "id" not in found:
        return None
    try:
        prof = http_get_json(SESSION_PROFILE_URL + found["id"], retries=1)
    except Exception:
        return None
    skin = None
    for prop in prof.get("properties", []):
        if prop.get("name") == "textures":
            try:
                tex = json.loads(base64.b64decode(prop["value"]).decode("utf-8"))
            except Exception:
                continue
            skin = (tex.get("textures") or {}).get("SKIN")
    if not skin or not skin.get("url"):
        return None
    uri = fetch_texture_uri(skin["url"])
    if not uri:
        return None
    try:
        decode_png_uri(uri)
    except ValueError:
        return None
    return {"name": found.get("name", name), "image": uri,
            "variant": "slim" if (skin.get("metadata") or {}).get("model") == "slim" else "classic"}
skins_index = JsonStore(SKINS_DIR / "index.json", [])
def list_saved_skins() -> list:
    out = []
    for entry in skins_index.read():
        path = SKINS_DIR / f"{entry.get('id', '')}.png"
        if path.is_file():
            out.append({**entry, "image": png_uri(path.read_bytes())})
    return out
def _zip_read(zf: zipfile.ZipFile, member: str):
    member = member.replace("\\", "/").lstrip("./").lstrip("/")
    try:
        return zf.read(member)
    except KeyError:
        lower = member.lower()
        for n in zf.namelist():
            if n.lower() == lower:
                return zf.read(n)
    return None
def _clean_text(value, limit: int = 240):
    if value is None:
        return None
    if isinstance(value, (list, dict)):
        value = _flatten_component(value)
    text = re.sub(r"\u00a7.", "", str(value))
    text = re.sub(r"\s+", " ", text).strip()
    return text[:limit] if text else None
def _flatten_component(c) -> str:
    if c is None:
        return ""
    if isinstance(c, str):
        return c
    if isinstance(c, list):
        return "".join(_flatten_component(x) for x in c)
    if isinstance(c, dict):
        out = str(c.get("text", "") or "")
        return out + "".join(_flatten_component(x) for x in c.get("extra", []))
    return str(c)
def _authors_text(authors):
    if not authors:
        return None
    if isinstance(authors, str):
        return _clean_text(authors, 80)
    names = []
    for a in authors:
        if isinstance(a, dict):
            a = a.get("name")
        if a:
            names.append(str(a))
    return _clean_text(", ".join(names[:3]), 80)
def _toml_fields(text: str) -> dict:
    data = None
    try:
        import tomllib  
        data = tomllib.loads(text)
    except Exception:
        data = None
    if data is not None:
        mods = data.get("mods") or []
        m = mods[0] if mods and isinstance(mods[0], dict) else {}
        return {
            "name": m.get("displayName") or m.get("modId"),
            "version": m.get("version"),
            "description": m.get("description"),
            "icon": m.get("logoFile") or data.get("logoFile"),
            "authors": m.get("authors") or data.get("authors"),
        }
    def grab(key):
        rx = re.compile(r'^\s*' + key + r'\s*=\s*(?:|\'\'\'(.*?)\'\'\'|"([^"\n]*)"|\'([^\'\n]*)\')',
                        re.M | re.S)
        m = rx.search(text)
        return next((g for g in m.groups() if g is not None), None) if m else None
    return {"name": grab("displayName") or grab("modId"), "version": grab("version"),
            "description": grab("description"), "icon": grab("logoFile"), "authors": grab("authors")}
def _manifest_version(zf: zipfile.ZipFile):
    raw = _zip_read(zf, "META-INF/MANIFEST.MF")
    if raw:
        m = re.search(r"Implementation-Version:\s*(\S+)", raw.decode("utf-8", "ignore"))
        if m:
            return m.group(1)
    return None
def read_mod_jar(path: Path) -> dict:
    info = {"name": None, "version": None, "description": None, "authors": None,
            "loader": None, "_icon": None}
    try:
        with zipfile.ZipFile(path) as zf:
            raw = _zip_read(zf, "fabric.mod.json")
            if raw:
                d = json.loads(raw.decode("utf-8-sig"))
                icon = d.get("icon")
                if isinstance(icon, dict) and icon:
                    icon = icon[min(icon, key=lambda k: abs(int(k) - 128))] if all(
                        str(k).isdigit() for k in icon) else next(iter(icon.values()))
                info.update(name=d.get("name") or d.get("id"), version=d.get("version"),
                            description=d.get("description"), authors=d.get("authors"),
                            loader="fabric", _icon=icon if isinstance(icon, str) else None)
            else:
                raw = _zip_read(zf, "quilt.mod.json")
                if raw:
                    d = json.loads(raw.decode("utf-8-sig"))
                    ql = d.get("quilt_loader", {})
                    meta = ql.get("metadata", {})
                    contributors = meta.get("contributors")
                    info.update(name=meta.get("name") or ql.get("id"), version=ql.get("version"),
                                description=meta.get("description"),
                                authors=list(contributors) if isinstance(contributors, dict) else None,
                                loader="quilt", _icon=meta.get("icon") if isinstance(meta.get("icon"), str) else None)
                else:
                    toml_raw, loader = _zip_read(zf, "META-INF/neoforge.mods.toml"), "neoforge"
                    if not toml_raw:
                        toml_raw, loader = _zip_read(zf, "META-INF/mods.toml"), "forge"
                    if toml_raw:
                        f = _toml_fields(toml_raw.decode("utf-8-sig", "replace"))
                        info.update(name=f["name"], version=f["version"], description=f["description"],
                                    authors=f["authors"], loader=loader, _icon=f["icon"])
                    else:
                        raw = _zip_read(zf, "mcmod.info")
                        if raw:
                            d = json.loads(raw.decode("utf-8-sig"))
                            if isinstance(d, dict):
                                d = d.get("modList", [])
                            m = d[0] if d else {}
                            info.update(name=m.get("name") or m.get("modid"), version=m.get("version"),
                                        description=m.get("description"),
                                        authors=m.get("authorList") or m.get("authors"),
                                        loader="forge", _icon=m.get("logoFile"))
            version = info["version"]
            if isinstance(version, str) and "${" in version:
                version = _manifest_version(zf)
            info["version"] = _clean_text(version, 40)
    except Exception:
        logging.info("Couldn't read mod metadata from %s", path.name)
    info["name"] = _clean_text(info["name"], 80)
    info["description"] = _clean_text(info["description"])
    info["authors"] = _authors_text(info["authors"])
    return info
def read_resourcepack(path: Path) -> dict:
    info = {"name": None, "version": None, "description": None, "authors": None,
            "loader": None, "_icon": "pack.png"}
    try:
        raw = None
        if path.is_dir():
            f = path / "pack.mcmeta"
            raw = f.read_bytes() if f.is_file() else None
            if not (path / "pack.png").is_file():
                info["_icon"] = None
        else:
            with zipfile.ZipFile(path) as zf:
                raw = _zip_read(zf, "pack.mcmeta")
                if _zip_read(zf, "pack.png") is None:
                    info["_icon"] = None
        if raw:
            d = json.loads(raw.decode("utf-8-sig"))
            info["description"] = (d.get("pack") or {}).get("description")
    except Exception:
        logging.info("Couldn't read pack metadata from %s", path.name)
    info["description"] = _clean_text(info["description"])
    return info
def _content_folder(instance_name: str, kind: str) -> Path:
    game = instance_game_dir(instance_name)
    return game / ("mods" if kind == "mods" else "resourcepacks")
def list_instance_content(instance_name: str, kind: str) -> list:
    folder = _content_folder(instance_name, kind)
    items = []
    if not folder.is_dir():
        return items
    for p in folder.iterdir():
        lower = p.name.lower()
        enabled = True
        if kind == "mods":
            if lower.endswith(".jar.disabled"):
                enabled, stem = False, p.name[:-len(".jar.disabled")]
            elif lower.endswith(".jar") and p.is_file():
                stem = p.name[:-4]
            else:
                continue
            info = read_mod_jar(p)
        else:
            if p.is_file() and lower.endswith(".zip"):
                stem = p.name[:-4]
            elif p.is_dir():
                stem = p.name
            else:
                continue
            info = read_resourcepack(p)
        items.append({
            "file": p.name,
            "name": info["name"] or stem,
            "version": info["version"],
            "description": info["description"],
            "authors": info["authors"],
            "loader": info["loader"],
            "has_icon": bool(info["_icon"]),
            "enabled": enabled,
        })
    items.sort(key=lambda i: i["name"].lower())
    return items
def read_content_icon(instance_name: str, kind: str, filename: str):
    folder = _content_folder(instance_name, kind)
    path = folder / Path(filename).name
    if not path.exists() or not is_within(folder, path):
        return None
    info = read_mod_jar(path) if kind == "mods" else read_resourcepack(path)
    icon_path = info["_icon"]
    if not icon_path:
        return None
    data = None
    try:
        if path.is_dir():
            f = path / icon_path
            data = f.read_bytes() if f.is_file() and is_within(path, f) else None
        else:
            with zipfile.ZipFile(path) as zf:
                data = _zip_read(zf, icon_path)
    except Exception:
        return None
    if not data or len(data) > 320_000:
        return None
    if data.startswith(b"\x89PNG"):
        mime = "image/png"
    elif data.startswith(b"\xff\xd8"):
        mime = "image/jpeg"
    else:
        return None
    return f"data:{mime};base64," + base64.b64encode(data).decode("ascii")
class API:
    def __init__(self):
        self._window = None
        self._processes: dict = {}
        self._sessions: dict = {}
        self._stopping: set = set()
        self._installing: set = set()
        self._addon_busy = False
        self._web_login = None
        self._proc_lock = threading.Lock()
    def set_window(self, window):
        self._window = window
    def emit(self, name: str, data=None):
        if self._window is None:
            return
        try:
            payload = json.dumps(data if data is not None else {})
            name_json = json.dumps(name)
            code = f"window.__critEvent && window.__critEvent({name_json}, {payload});"
            self._window.evaluate_js(code)
        except Exception:
            log_exception(f"emit({name}) failed")
    def push_state(self):
        self.emit("stateChanged", self._state_dict())
    def _runtime_dict(self) -> dict:
        with self._proc_lock:
            running = list(self._processes.keys())
            sessions = dict(self._sessions)
            installing = list(self._installing)
        return {
            "running": running,
            "sessions": sessions,
            "installing": installing,
            "playtime_total": int(settings_store.read().get("playtime_seconds", 0)),
        }
    def _state_dict(self) -> dict:
        settings = settings_store.read()
        accounts = accounts_store.read()
        active_account = find_account(accounts, settings.get("active_account"))
        if not active_account and accounts:
            active_account = accounts[0]
        instances = []
        for meta in list_instances():
            instances.append({
                "name": meta.get("name"),
                "version": meta.get("version"),
                "loader": meta.get("loader", "vanilla"),
                "loader_version": meta.get("loader_version"),
                "installed": is_installed(meta),
                "playtime": int(meta.get("playtime", 0)),
                "icon": meta.get("icon") if meta.get("icon") in INSTANCE_ICONS else None,
                "created": meta.get("created"),
            })
        active_instance_name = settings.get("active_instance")
        active_instance = next(
            (i for i in instances if i["name"] == active_instance_name), None)
        if not active_instance and instances:
            active_instance = instances[0]
        java_path, java_line = detect_java_for_ui()
        runtime = self._runtime_dict()
        return {
            "accounts": [public_account(a) for a in accounts],
            "active_account": public_account(active_account) if active_account else None,
            "instances": instances,
            "active_instance": active_instance,
            "running": runtime["running"],
            "sessions": runtime["sessions"],
            "installing": runtime["installing"],
            "playtime_total": runtime["playtime_total"],
            "settings": {
                "memory_mb": int(settings.get("memory_mb", 2048)),
                "java_path": settings.get("java_path"),
                "ms_warning_seen": bool(settings.get("ms_warning_seen", False)),
                "sidebar_collapsed": bool(settings.get("sidebar_collapsed", False)),
            },
            "java_ok": java_path is not None,
            "java_version": java_line,
        }
    def get_state(self):
        try:
            return {"success": True, "state": self._state_dict()}
        except Exception as exc:
            log_exception("get_state")
            return {"success": False, "error": str(exc)}
    def get_runtime(self):
        try:
            data = self._runtime_dict()
            data["success"] = True
            return data
        except Exception as exc:
            log_exception("get_runtime")
            return {"success": False, "error": str(exc)}
    def get_versions(self):
        try:
            manifest = meta_cache.get_manifest()
            releases = [v for v in manifest["versions"] if v["type"] == "release"]
            return {"success": True, "versions": releases,
                    "latest_release": manifest.get("latest", {}).get("release")}
        except Exception:
            log_exception("get_versions")
            return {"success": False, "error": "Failed to load versions"}
    def get_fabric_loaders(self, minecraft_version=None):
        try:
            loaders = meta_cache.get_fabric_loaders(minecraft_version)
            return {"success": True, "loaders": loaders}
        except Exception as exc:
            log_exception("get_fabric_loaders")
            return {"success": False, "error": str(exc) or "Fabric loader could not be retrieved."}
    def get_fabric_game_versions(self):
        try:
            return {"success": True, "versions": meta_cache.get_fabric_game_versions()}
        except Exception as exc:
            log_exception("get_fabric_game_versions")
            return {"success": False, "error": str(exc) or "Fabric versions could not be retrieved."}
    def get_forge_versions(self, minecraft_version):
        try:
            versions = meta_cache.get_forge_versions(minecraft_version)
            return {"success": True, "versions": versions}
        except Exception as exc:
            log_exception("get_forge_versions")
            return {"success": False, "error": str(exc) or "Forge versions could not be retrieved."}
    def get_forge_mc_versions(self):
        try:
            return {"success": True, "versions": meta_cache.get_forge_mc_versions()}
        except Exception as exc:
            log_exception("get_forge_mc_versions")
            return {"success": False, "error": str(exc) or "Forge versions could not be retrieved."}
    def add_account(self, username):
        try:
            username = (username or "").strip()
            if not username or len(username) > 16 or not re.match(r"^[A-Za-z0-9_]+$", username):
                return {"success": False, "error": "Enter a valid username (letters, numbers, underscore)."}
            accounts = accounts_store.read()
            if any((a.get("name") or "").lower() == username.lower() for a in accounts):
                return {"success": False, "error": "An account with that name already exists."}
            account = {"name": username, "uuid": offline_uuid(username), "type": "offline"}
            accounts.append(account)
            accounts_store.write(accounts)
            def _set_active(s):
                if not s.get("active_account"):
                    s["active_account"] = account["uuid"]
            settings_store.update(_set_active)
            self.push_state()
            return {"success": True, "account": public_account(account)}
        except Exception as exc:
            log_exception("add_account")
            return {"success": False, "error": f"Failed to add account: {exc}"}
    def remove_account(self, key):
        try:
            accounts = accounts_store.read()
            target = find_account(accounts, key)
            if not target:
                return {"success": False, "error": "Account not found."}
            accounts = [a for a in accounts if a is not target and a.get("uuid") != target.get("uuid")]
            accounts_store.write(accounts)
            def _fix_active(s):
                cur = find_account(accounts_store.read(), s.get("active_account"))
                if not cur:
                    s["active_account"] = accounts[0]["uuid"] if accounts else None
            settings_store.update(_fix_active)
            self.push_state()
            return {"success": True}
        except Exception as exc:
            log_exception("remove_account")
            return {"success": False, "error": f"Failed to remove account: {exc}"}
    def switch_account(self, key):
        try:
            target = find_account(accounts_store.read(), key)
            if not target:
                return {"success": False, "error": "Account not found."}
            settings_store.update(lambda s: s.__setitem__("active_account", target["uuid"]))
            self.push_state()
            return {"success": True}
        except Exception:
            log_exception("switch_account")
            return {"success": False, "error": "Failed to switch account."}
    def start_minecraft_web_login(self):
        try:
            import webview
        except ImportError:
            return {"success": False, "error": "pywebview isn't available."}
        try:
            with self._proc_lock:
                old = self._web_login
                self._web_login = None
            if old:
                old["cancel"] = True
                try:
                    old["window"].destroy()
                except Exception:
                    pass
            state = {"cancel": False, "closed": False}
            win = webview.create_window(
                "Sign in to Minecraft", MC_WEB_LOGIN_URL, width=960, height=760,
                min_size=(720, 560), resizable=True, background_color="
            state["window"] = win
            try:
                win.events.closed += lambda *a: state.__setitem__("closed", True)
            except Exception:
                pass
            with self._proc_lock:
                self._web_login = state
            threading.Thread(target=self._web_login_poll, args=(state,), daemon=True).start()
            return {"success": True}
        except Exception as exc:
            log_exception("start_minecraft_web_login")
            return {"success": False, "error": f"Couldn't open the sign-in window: {exc}"}
    def cancel_minecraft_web_login(self):
        with self._proc_lock:
            state = self._web_login
            self._web_login = None
        if state:
            state["cancel"] = True
            try:
                state["window"].destroy()
            except Exception:
                pass
        return {"success": True}
    def _web_login_poll(self, state):
        win = state["window"]
        reached_at = None
        try:
            deadline = time.time() + 900
            token = ""
            while time.time() < deadline:
                time.sleep(0.8)
                if state["cancel"]:
                    return
                if state["closed"]:
                    self.emit("mcLoginError", {"message": "The sign-in window was closed before it finished."})
                    return
                try:
                    url = win.get_current_url() or ""
                except Exception:
                    continue
                if not url.startswith(MC_WEB_PROFILE_URL):
                    reached_at = None
                    continue
                if reached_at is None:
                    reached_at = time.time()
                    self.emit("mcLoginStatus", {"status": "Reading your session..."})
                try:
                    token = str(win.evaluate_js(MC_WEB_COOKIE_JS) or "").strip()
                except Exception:
                    token = ""
                if token:
                    break
                if time.time() - reached_at > 20:
                    raise AuthError("You're signed in, but the session cookie wasn't found. "
                                    "minecraft.net may have changed how it works.")
            if not token:
                raise AuthError("Sign-in took too long. Try again.")
            name, account_uuid = profile_for_token(token)
            save_session_account(name, account_uuid, token, jwt_expiry(token))
            try:
                win.destroy()
            except Exception:
                pass
            self.push_state()
            self.emit("mcLoginFinished", {"name": name})
        except AuthError as exc:
            self.emit("mcLoginError", {"message": str(exc)})
            try:
                win.destroy()
            except Exception:
                pass
        except Exception as exc:
            log_exception("_web_login_poll")
            self.emit("mcLoginError", {"message": f"Sign-in failed: {exc}"})
        finally:
            with self._proc_lock:
                if self._web_login is state:
                    self._web_login = None
    def import_official_session(self):
        try:
            sessions = read_official_sessions()
            if not sessions:
                return {"success": False, "error": "The official launcher didn't leave a readable session. "
                        "Newer versions may store it encrypted. You can paste a token instead."}
            now = time.time()
            imported, expired = [], 0
            for sess in sessions:
                if sess["expires"] and sess["expires"] < now + 60:
                    expired += 1
                    continue
                try:
                    name, account_uuid = profile_for_token(sess["token"])
                except AuthError:
                    expired += 1
                    continue
                except Exception:
                    name, account_uuid = sess["name"], str(uuid.UUID(sess["id"]))
                save_session_account(name, account_uuid, sess["token"], sess["expires"])
                imported.append(name)
            if not imported:
                msg = ("The official launcher's session has expired. Open the official launcher "
                       "so it refreshes, then import again.") if expired else "No usable session found."
                return {"success": False, "error": msg}
            self.push_state()
            return {"success": True, "imported": imported}
        except AuthError as exc:
            return {"success": False, "error": str(exc)}
        except Exception as exc:
            log_exception("import_official_session")
            return {"success": False, "error": f"Import failed: {exc}"}
    def add_session_token(self, token):
        try:
            token = (token or "").strip().strip('"')
            if token.lower().startswith("bearer "):
                token = token[7:].strip()
            if len(token) < 40 or " " in token:
                return {"success": False, "error": "That doesn't look like a Minecraft access token."}
            name, account_uuid = profile_for_token(token)
            account = save_session_account(name, account_uuid, token, jwt_expiry(token))
            self.push_state()
            return {"success": True, "account": public_account(account)}
        except AuthError as exc:
            return {"success": False, "error": str(exc)}
        except Exception as exc:
            log_exception("add_session_token")
            return {"success": False, "error": f"Couldn't add the token: {exc}"}
    def _skin_account(self):
        accounts = accounts_store.read()
        acc = find_account(accounts, settings_store.read().get("active_account")) or (accounts[0] if accounts else None)
        if not acc or acc.get("type") != "microsoft":
            raise AuthError("Skins and capes need a Microsoft account. Add one under Accounts.")
        return ensure_fresh_account(acc)
    def get_skin_profile(self):
        try:
            acc = self._skin_account()
            status, prof = http_get_auth(MC_PROFILE_URL, acc["mc_token"])
            if status != 200 or "id" not in prof:
                return {"success": False, "error": _skin_error(status)}
            skins = prof.get("skins") or []
            active = next((x for x in skins if x.get("state") == "ACTIVE"), skins[0] if skins else None)
            capes = [{"id": c.get("id"), "alias": c.get("alias") or "Cape",
                      "active": c.get("state") == "ACTIVE", "image": fetch_texture_uri(c.get("url"))}
                     for c in (prof.get("capes") or [])]
            return {"success": True, "name": prof.get("name"),
                    "variant": str((active or {}).get("variant", "CLASSIC")).lower(),
                    "skin": fetch_texture_uri((active or {}).get("url")), "capes": capes}
        except AuthError as exc:
            return {"success": False, "error": str(exc)}
        except Exception as exc:
            log_exception("get_skin_profile")
            return {"success": False, "error": f"Couldn't load your profile: {exc}"}
    def set_skin(self, image, variant="classic"):
        try:
            png = decode_png_uri(image)
            variant = "slim" if variant == "slim" else "classic"
            acc = self._skin_account()
            body, ctype = multipart_skin(variant, png)
            status, _ = http_send("POST", MC_SKINS_URL, acc["mc_token"], body, ctype)
            if status in (200, 204):
                return {"success": True}
            return {"success": False, "error": _skin_error(status)}
        except (AuthError, ValueError) as exc:
            return {"success": False, "error": str(exc)}
        except Exception as exc:
            log_exception("set_skin")
            return {"success": False, "error": f"Couldn't change the skin: {exc}"}
    def reset_skin(self):
        try:
            acc = self._skin_account()
            status, _ = http_send("DELETE", MC_SKINS_URL + "/active", acc["mc_token"])
            if status in (200, 204):
                return {"success": True}
            return {"success": False, "error": _skin_error(status)}
        except AuthError as exc:
            return {"success": False, "error": str(exc)}
        except Exception as exc:
            log_exception("reset_skin")
            return {"success": False, "error": f"Couldn't reset the skin: {exc}"}
    def set_cape(self, cape_id=None):
        try:
            acc = self._skin_account()
            if cape_id:
                status, _ = http_send("PUT", MC_CAPE_URL, acc["mc_token"],
                                      json.dumps({"capeId": cape_id}).encode("utf-8"), "application/json")
            else:
                status, _ = http_send("DELETE", MC_CAPE_URL, acc["mc_token"])
            if status in (200, 204):
                return {"success": True}
            return {"success": False, "error": _skin_error(status)}
        except AuthError as exc:
            return {"success": False, "error": str(exc)}
        except Exception as exc:
            log_exception("set_cape")
            return {"success": False, "error": f"Couldn't change the cape: {exc}"}
    def lookup_player_skin(self, name):
        try:
            name = (name or "").strip()
            if not re.match(r"^[A-Za-z0-9_]{1,16}$", name):
                return {"success": False, "error": "Enter a valid player name."}
            result = _fetch_named_skin(name)
            if not result:
                return {"success": False, "error": f'No skin found for "{name}". They may use the default skin.'}
            return {"success": True, **result}
        except Exception:
            log_exception("lookup_player_skin")
            return {"success": False, "error": "Couldn't reach Mojang. Check your connection."}
    def list_saved_skins(self):
        try:
            return {"success": True, "skins": list_saved_skins()}
        except Exception:
            log_exception("list_saved_skins")
            return {"success": False, "error": "Couldn't read your saved skins."}
    def save_skin(self, name, image, variant="classic"):
        try:
            png = decode_png_uri(image)
            name = re.sub(r"\s+", " ", (name or "").strip())[:32] or "My skin"
            skin_id = uuid.uuid4().hex[:10]
            (SKINS_DIR / f"{skin_id}.png").write_bytes(png)
            skins_index.update(lambda idx: idx.append({
                "id": skin_id, "name": name,
                "variant": "slim" if variant == "slim" else "classic", "created": time.time()}))
            return {"success": True, "skins": list_saved_skins()}
        except ValueError as exc:
            return {"success": False, "error": str(exc)}
        except Exception:
            log_exception("save_skin")
            return {"success": False, "error": "Couldn't save the skin."}
    def delete_saved_skin(self, skin_id):
        try:
            skin_id = re.sub(r"[^A-Za-z0-9]", "", str(skin_id))
            (SKINS_DIR / f"{skin_id}.png").unlink(missing_ok=True)
            skins_index.update(lambda idx: idx.__setitem__(slice(None), [e for e in idx if e.get("id") != skin_id]))
            return {"success": True, "skins": list_saved_skins()}
        except Exception:
            log_exception("delete_saved_skin")
            return {"success": False, "error": "Couldn't delete the skin."}
    def get_instance_content(self, instance_name, kind="mods"):
        try:
            meta = read_instance_meta(instance_name)
            if not meta:
                return {"success": False, "error": "Instance not found."}
            if kind not in ("mods", "resourcepacks"):
                return {"success": False, "error": "Unknown content type."}
            if kind == "mods" and meta.get("loader", "vanilla") == "vanilla":
                return {"success": True, "items": [], "loader": "vanilla"}
            return {"success": True, "items": list_instance_content(instance_name, kind),
                    "loader": meta.get("loader", "vanilla")}
        except Exception:
            log_exception("get_instance_content")
            return {"success": False, "error": "Couldn't read the folder."}
    def get_content_icon(self, instance_name, kind, filename):
        try:
            return {"success": True, "icon": read_content_icon(instance_name, kind, filename)}
        except Exception:
            return {"success": True, "icon": None}
    def _make_instance(self, name, version, loader, loader_version):
        instance_dir(name).mkdir(parents=True, exist_ok=True)
        meta = {
            "name": name,
            "version": version,
            "loader": loader,
            "loader_version": loader_version if loader in ("fabric", "forge") else None,
            "installed": False,
            "schema": INSTALL_SCHEMA,
            "main_class": None,
            "asset_index": None,
            "arguments": None,
            "minecraft_arguments": None,
            "classpath": [],
            "playtime": 0,
            "icon": None,
            "created": time.time(),
        }
        write_instance_meta(name, meta)
        ensure_game_layout(name, loader)
        return meta
    def create_instance(self, version, loader, loader_version, instance_name):
        try:
            name = sanitize_instance_name(instance_name)
        except ValueError as exc:
            return {"success": False, "error": str(exc)}
        if instance_dir(name).exists():
            return {"success": False, "error": "Instance already exists."}
        if not version:
            return {"success": False, "error": "Select a Minecraft version."}
        if loader in ("fabric", "forge") and not loader_version:
            return {"success": False, "error": f"Select a {loader.capitalize()} version."}
        try:
            self._make_instance(name, version, loader, loader_version)
            self.push_state()
            return {"success": True}
        except Exception as exc:
            log_exception("create_instance")
            return {"success": False, "error": f"Failed to create instance: {exc}"}
    def delete_instance(self, instance_name):
        try:
            with self._proc_lock:
                if instance_name in self._processes:
                    return {"success": False, "error": "Stop the instance before deleting it."}
                if instance_name in self._installing:
                    return {"success": False, "error": "Instance is currently installing."}
            target = instance_dir(instance_name)
            if not target.exists():
                return {"success": False, "error": "Instance not found."}
            shutil.rmtree(target, ignore_errors=True)
            def _clear_active(s):
                if s.get("active_instance") == instance_name:
                    s["active_instance"] = None
            settings_store.update(_clear_active)
            self.push_state()
            return {"success": True}
        except Exception:
            log_exception("delete_instance")
            return {"success": False, "error": "Failed to delete instance."}
    def set_active_instance(self, instance_name):
        try:
            if not read_instance_meta(instance_name):
                return {"success": False, "error": "Instance not found."}
            settings_store.update(lambda s: s.__setitem__("active_instance", instance_name))
            self.push_state()
            return {"success": True}
        except Exception:
            log_exception("set_active_instance")
            return {"success": False, "error": "Failed to set active instance."}
    def set_instance_icon(self, instance_name, icon_key):
        try:
            if not read_instance_meta(instance_name):
                return {"success": False, "error": "Instance not found."}
            icon_key = icon_key if icon_key in INSTANCE_ICONS else None
            update_instance_meta(instance_name, lambda m: m.__setitem__("icon", icon_key))
            self.push_state()
            return {"success": True}
        except Exception:
            log_exception("set_instance_icon")
            return {"success": False, "error": "Couldn't change the icon."}
    def rename_instance(self, old_name, new_name):
        try:
            old_meta = read_instance_meta(old_name)
            if not old_meta:
                return {"success": False, "error": "Instance not found."}
            try:
                new_name = sanitize_instance_name(new_name)
            except ValueError as exc:
                return {"success": False, "error": str(exc)}
            if new_name == old_name:
                return {"success": True}
            if instance_dir(new_name).exists():
                return {"success": False, "error": "An instance with that name already exists."}
            with self._proc_lock:
                if old_name in self._processes or old_name in self._installing:
                    return {"success": False, "error": "Stop or wait for the instance first."}
            shutil.move(str(instance_dir(old_name)), str(instance_dir(new_name)))
            old_meta["name"] = new_name
            write_instance_meta(new_name, old_meta)
            def _fix(s):
                if s.get("active_instance") == old_name:
                    s["active_instance"] = new_name
            settings_store.update(_fix)
            self.push_state()
            return {"success": True}
        except Exception:
            log_exception("rename_instance")
            return {"success": False, "error": "Couldn't rename the instance."}
    def get_instance_screenshots(self, instance_name):
        try:
            meta = read_instance_meta(instance_name)
            if not meta:
                return {"success": False, "error": "Instance not found."}
            folder = instance_game_dir(instance_name) / "screenshots"
            if not folder.is_dir():
                return {"success": True, "screenshots": []}
            shots = []
            for p in sorted(folder.iterdir(), key=lambda p: p.stat().st_mtime, reverse=True):
                if p.is_file() and p.suffix.lower() in (".png", ".jpg", ".jpeg"):
                    shots.append({"file": p.name, "path": p.resolve().as_uri(),
                                 "taken": p.stat().st_mtime})
            return {"success": True, "screenshots": shots}
        except Exception:
            log_exception("get_instance_screenshots")
            return {"success": False, "error": "Couldn't read the screenshots folder."}
    def open_instance_folder(self, instance_name):
        try:
            if not read_instance_meta(instance_name):
                return {"success": False, "error": "Instance not found."}
            meta = read_instance_meta(instance_name) or {}
            ensure_game_layout(instance_name, meta.get("loader", "vanilla"))
            open_in_file_manager(instance_game_dir(instance_name))
            return {"success": True}
        except Exception:
            log_exception("open_instance_folder")
            return {"success": False, "error": "Couldn't open the folder."}
    def launch(self, instance_name):
        try:
            with self._proc_lock:
                if instance_name in self._processes:
                    return {"success": False, "error": "Instance is already running."}
                if instance_name in self._installing:
                    return {"success": False, "error": "Instance is currently installing."}
            accounts = accounts_store.read()
            settings = settings_store.read()
            active_account = find_account(accounts, settings.get("active_account"))
            if not active_account and accounts:
                active_account = accounts[0]
            if not active_account:
                return {"success": False, "error": "Please add an account first."}
            try:
                active_account = ensure_fresh_account(active_account)
            except AuthError as exc:
                return {"success": False, "error": str(exc)}
            meta = read_instance_meta(instance_name)
            if not meta:
                return {"success": False, "error": "Instance not found."}
            needs_install = not is_installed(meta)
            if not needs_install:
                java_path = resolve_java(java_info_from_meta(meta), allow_download=False)
                if java_path:
                    return self._do_launch(instance_name, meta, active_account, java_path)
            with self._proc_lock:
                self._installing.add(instance_name)
            self.push_state()
            threading.Thread(
                target=self._launch_worker,
                args=(instance_name, meta, active_account, needs_install),
                daemon=True,
            ).start()
            return {"success": True, "installing": True}
        except Exception:
            log_exception("launch")
            return {"success": False, "error": "Failed to launch Minecraft."}
    def _launch_worker(self, instance_name, meta, account, needs_install):
        try:
            if needs_install:
                if not Installer(self, meta).run():
                    return
                meta = read_instance_meta(instance_name) or meta
            info = java_info_from_meta(meta)
            java_path = resolve_java(info, allow_download=False)
            if not java_path:
                self.emit("installStarted", {
                    "instance": instance_name,
                    "title": f"Downloading Java {info['majorVersion']}..."})
                tracker = ProgressTracker(self.emit)
                java_path = resolve_java(info, allow_download=True, tracker=tracker)
                self.emit("installFinished", {"instance": instance_name})
            result = self._do_launch(instance_name, meta, account, java_path)
            if not result.get("success"):
                self.emit("launchError", {"instance": instance_name,
                                          "message": result.get("error", "Failed to launch.")})
        except Exception as exc:
            log_exception("_launch_worker")
            self.emit("installError", {"message": str(exc) or "Failed to launch Minecraft."})
        finally:
            with self._proc_lock:
                self._installing.discard(instance_name)
            self.push_state()
    def _do_launch(self, instance_name, meta, account, java_path):
        try:
            if not java_path:
                major = java_info_from_meta(meta)["majorVersion"]
                return {"success": False, "error": f"Java {major} was not found."}
            settings = settings_store.read()
            memory_mb = int(settings.get("memory_mb", 2048))
            command = build_launch_command(meta, account, memory_mb, console_java(java_path))
            game_dir = instance_game_dir(instance_name)
            ensure_game_layout(instance_name, meta.get("loader", "vanilla"))
            log_path = instance_dir(instance_name) / "launch_output.log"
            log_file = open(log_path, "wb")
            logging.info("Launching %s: %s", instance_name, " ".join(command)[:2500])
            try:
                proc = subprocess.Popen(
                    command,
                    cwd=str(game_dir),
                    stdin=subprocess.DEVNULL,
                    stdout=log_file,
                    stderr=subprocess.STDOUT,
                    creationflags=CREATE_NO_WINDOW,
                    shell=False,
                )
            except Exception:
                log_file.close()
                raise
            started = time.time()
            with self._proc_lock:
                self._processes[instance_name] = proc
                self._sessions[instance_name] = started
            update_instance_meta(instance_name, lambda m: m.__setitem__("last_played", started))
            threading.Thread(
                target=self._watch_process,
                args=(instance_name, proc, log_file, log_path, started),
                daemon=True,
            ).start()
            self.push_state()
            return {"success": True}
        except Exception as exc:
            log_exception("_do_launch")
            return {"success": False, "error": f"Failed to launch Minecraft: {exc}"}
    def _watch_process(self, instance_name, proc, log_file, log_path, started):
        code = proc.wait()
        try:
            log_file.close()
        except OSError:
            pass
        elapsed = int(time.time() - started)
        with self._proc_lock:
            self._processes.pop(instance_name, None)
            self._sessions.pop(instance_name, None)
            user_stopped = instance_name in self._stopping
            self._stopping.discard(instance_name)
        if elapsed >= 1:
            try:
                settings_store.update(
                    lambda s: s.__setitem__("playtime_seconds",
                                            int(s.get("playtime_seconds", 0)) + elapsed))
                update_instance_meta(
                    instance_name,
                    lambda m: m.__setitem__("playtime", int(m.get("playtime", 0)) + elapsed))
            except Exception:
                log_exception("playtime update")
        if code != 0 and not user_stopped:
            tail = _log_tail(log_path)
            logging.error("%s exited with code %s\n%s", instance_name, code, tail[-2000:])
            self.emit("launchError", {
                "instance": instance_name,
                "message": f"{instance_name} closed unexpectedly (exit code {code}). {_explain_crash(tail)}",
            })
        self.push_state()
    def stop(self, instance_name):
        try:
            with self._proc_lock:
                proc = self._processes.get(instance_name)
                if proc:
                    self._stopping.add(instance_name)
            if not proc:
                return {"success": False, "error": "Instance is not running."}
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
            return {"success": True}
        except Exception:
            log_exception("stop")
            return {"success": False, "error": "Failed to stop instance."}
    def save_settings(self, patch: dict):
        try:
            def _apply(settings):
                if "memory_mb" in patch:
                    try:
                        mb = int(patch["memory_mb"])
                        settings["memory_mb"] = max(512, min(mb, 32768))
                    except (TypeError, ValueError):
                        pass
                if "java_path" in patch:
                    settings["java_path"] = (patch["java_path"] or "").strip() or None
                if "ms_warning_seen" in patch:
                    settings["ms_warning_seen"] = bool(patch["ms_warning_seen"])
                if "sidebar_collapsed" in patch:
                    settings["sidebar_collapsed"] = bool(patch["sidebar_collapsed"])
            settings = settings_store.update(_apply)
            _JAVA_UI_CACHE["at"] = 0.0
            self.push_state()
            return {"success": True, "settings": settings}
        except Exception:
            log_exception("save_settings")
            return {"success": False, "error": "Failed to save settings."}
    def search_addons(self, query="", project_type="mod"):
        try:
            if project_type not in ADDON_TYPES:
                project_type = "mod"
            facets = [[f"project_type:{project_type}"]]
            params = {
                "query": query or "",
                "limit": "30",
                "facets": json.dumps(facets),
                "index": "relevance" if query else "downloads",
            }
            url = f"{MODRINTH_API}/search?" + urllib.parse.urlencode(params)
            data = http_get_json(url)
            return {"success": True, "hits": data.get("hits", [])}
        except Exception:
            log_exception("search_addons")
            return {"success": False, "error": "Failed to search Modrinth."}
    def get_addon(self, project_id):
        try:
            data = http_get_json(f"{MODRINTH_API}/project/{urllib.parse.quote(project_id)}")
            return {"success": True, "project": data}
        except Exception:
            log_exception("get_addon")
            return {"success": False, "error": "Failed to load addon info."}
    def get_addon_targets(self, project_type="mod"):
        try:
            result = []
            for inst in list_instances():
                loader = inst.get("loader", "vanilla")
                if project_type == "mod" and loader not in ("fabric", "forge"):
                    continue
                result.append({
                    "name": inst["name"], "version": inst["version"],
                    "loader": loader, "loader_version": inst.get("loader_version"),
                    "installed": is_installed(inst),
                })
            return {"success": True, "instances": result}
        except Exception:
            log_exception("get_addon_targets")
            return {"success": False, "error": "Failed to list instances."}
    def _find_version(self, project_id, loader=None, mc_version=None):
        params = {}
        if loader:
            params["loaders"] = json.dumps([loader])
        if mc_version:
            params["game_versions"] = json.dumps([mc_version])
        url = f"{MODRINTH_API}/project/{urllib.parse.quote(project_id)}/version"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        return pick_best_version(http_get_json(url))
    def _download_version_file(self, version, dest_dir: Path):
        file_info = primary_file(version)
        if not file_info:
            raise RuntimeError("That version has no downloadable file.")
        filename = Path(file_info["filename"]).name
        dest = dest_dir / filename
        if not is_within(dest_dir, dest):
            raise RuntimeError("Unsafe file name in addon.")
        http_download(file_info["url"], dest, file_info.get("size"),
                      (file_info.get("hashes") or {}).get("sha1"))
        return filename
    def _install_mod_with_deps(self, version, mods_dir, loader, mc_version, visited, files, depth=0):
        files.append(self._download_version_file(version, mods_dir))
        if depth >= 3:
            return
        for dep in version.get("dependencies", []):
            if dep.get("dependency_type") != "required":
                continue
            pid = dep.get("project_id")
            if not pid or pid in visited:
                continue
            visited.add(pid)
            try:
                if dep.get("version_id"):
                    dep_version = http_get_json(
                        f"{MODRINTH_API}/version/{urllib.parse.quote(dep['version_id'])}")
                else:
                    dep_version = self._find_version(pid, loader, mc_version)
                if dep_version:
                    self._install_mod_with_deps(dep_version, mods_dir, loader, mc_version,
                                                visited, files, depth + 1)
            except Exception:
                log_exception(f"dependency {pid} failed")
    def get_addon_dependencies(self, project_id, instance_name=None):
        try:
            meta = read_instance_meta(instance_name) if instance_name else None
            mc_version = meta["version"] if meta else None
            loader = meta.get("loader") if meta else None
            version = self._find_version(project_id, loader, mc_version)
            if not version:
                return {"success": True, "dependencies": []}
            deps = []
            for dep in version.get("dependencies", []):
                if dep.get("dependency_type") != "required":
                    continue
                pid = dep.get("project_id")
                if not pid:
                    continue
                try:
                    proj = http_get_json(f"{MODRINTH_API}/project/{urllib.parse.quote(pid)}")
                    deps.append({"project_id": pid, "title": proj.get("title", pid),
                                "icon_url": proj.get("icon_url")})
                except Exception:
                    deps.append({"project_id": pid, "title": pid, "icon_url": None})
            return {"success": True, "dependencies": deps}
        except Exception:
            log_exception("get_addon_dependencies")
            return {"success": False, "error": "Couldn't load dependencies."}
    def install_addon(self, project_id, project_type, instance_name, include_deps=True):
        try:
            meta = read_instance_meta(instance_name)
            if not meta:
                return {"success": False, "error": "Instance not found."}
            mc_version = meta["version"]
            loader = meta.get("loader", "vanilla")
            if project_type == "mod":
                if loader not in ("fabric", "forge"):
                    return {"success": False, "error": "Vanilla instances can't run mods."}
                version = self._find_version(project_id, loader, mc_version)
                if not version:
                    return {"success": False,
                            "error": f"No version of this mod supports {loader.capitalize()} {mc_version}."}
                mods_dir = instance_mods_dir(instance_name)
                mods_dir.mkdir(parents=True, exist_ok=True)
                files: list = []
                if include_deps:
                    self._install_mod_with_deps(version, mods_dir, loader, mc_version,
                                                {project_id}, files)
                else:
                    files.append(self._download_version_file(version, mods_dir))
                return {"success": True, "files": files}
            if project_type == "resourcepack":
                note = None
                version = self._find_version(project_id, None, mc_version)
                if not version:
                    version = self._find_version(project_id)
                    note = f"No exact match for {mc_version}, installed the newest version instead."
                if not version:
                    return {"success": False, "error": "That resource pack has no downloadable version."}
                dest_dir = instance_game_dir(instance_name) / "resourcepacks"
                dest_dir.mkdir(parents=True, exist_ok=True)
                name = self._download_version_file(version, dest_dir)
                return {"success": True, "files": [name], "note": note}
            return {"success": False, "error": "Unsupported addon type."}
        except Exception as exc:
            log_exception("install_addon")
            return {"success": False, "error": f"Failed to install: {exc}"}
    def install_modpack(self, project_id):
        with self._proc_lock:
            if self._addon_busy:
                return {"success": False, "error": "Another addon install is already running."}
            self._addon_busy = True
        threading.Thread(target=self._modpack_worker, args=(project_id,), daemon=True).start()
        return {"success": True}
    def _modpack_worker(self, project_id):
        pack_path = None
        try:
            tracker = ProgressTracker(self.emit)
            tracker.event = "addonProgress"
            tracker.set_task("Fetching modpack", force=True)
            project = http_get_json(f"{MODRINTH_API}/project/{urllib.parse.quote(project_id)}")
            versions = http_get_json(f"{MODRINTH_API}/project/{urllib.parse.quote(project_id)}/version")
            version = pick_best_version(versions)
            if not version:
                raise RuntimeError("This modpack has no downloadable versions.")
            files = version.get("files", [])
            pack_file = (next((f for f in files if f.get("filename", "").endswith(".mrpack")), None)
                         or primary_file(version))
            if not pack_file:
                raise RuntimeError("This modpack has no downloadable file.")
            pack_path = TMP_DIR / f"{re.sub(r'[^A-Za-z0-9_.-]', '_', project_id)}.mrpack"
            tracker.reset(pack_file.get("size") or 1_000_000)
            tracker.set_task("Downloading modpack", force=True)
            http_download(pack_file["url"], pack_path, pack_file.get("size"),
                          (pack_file.get("hashes") or {}).get("sha1"), tracker.add)
            with zipfile.ZipFile(pack_path) as zf:
                index = json.loads(zf.read("modrinth.index.json").decode("utf-8"))
                deps = index.get("dependencies", {})
                mc_version = deps.get("minecraft")
                if not mc_version:
                    raise RuntimeError("This modpack doesn't say which Minecraft version it needs.")
                loader, loader_version = "vanilla", None
                if "fabric-loader" in deps:
                    loader, loader_version = "fabric", deps["fabric-loader"]
                elif "forge" in deps:
                    loader, loader_version = "forge", deps["forge"]
                elif "neoforge" in deps:
                    raise RuntimeError("This modpack needs NeoForge, which Crit Launcher doesn't support yet.")
                elif "quilt-loader" in deps:
                    raise RuntimeError("This modpack needs Quilt, which Crit Launcher doesn't support yet.")
                name = unique_instance_name(index.get("name") or project.get("title") or "Modpack")
                self._make_instance(name, mc_version, loader, loader_version)
                game_dir = instance_game_dir(name)
                items = []
                for f in index.get("files", []):
                    if (f.get("env") or {}).get("client") == "unsupported":
                        continue
                    rel = f.get("path", "")
                    urls = f.get("downloads") or []
                    if not rel or not urls or not is_within(game_dir, game_dir / rel):
                        continue
                    items.append(({
                        "path": rel, "url": urls[0], "size": f.get("fileSize"),
                        "sha1": (f.get("hashes") or {}).get("sha1"),
                    }, game_dir))
                tracker.reset(sum(entry_weight(e) for e, _ in items) + 1)
                tracker.set_task("Downloading modpack files", force=True)
                download_all(items, tracker, workers=8)
                tracker.set_task("Applying overrides", force=True)
                for member in zf.namelist():
                    for prefix in ("overrides/", "client-overrides/"):
                        if member.startswith(prefix):
                            rel = member[len(prefix):]
                            if not rel:
                                break
                            dest = game_dir / rel
                            if not is_within(game_dir, dest):
                                break
                            if member.endswith("/"):
                                dest.mkdir(parents=True, exist_ok=True)
                            else:
                                dest.parent.mkdir(parents=True, exist_ok=True)
                                dest.write_bytes(zf.read(member))
                            break
            self.push_state()
            self.emit("addonFinished", {"name": name})
        except Exception as exc:
            log_exception("modpack install failed")
            self.emit("addonError", {"message": str(exc) or "Modpack install failed."})
        finally:
            if pack_path:
                try:
                    Path(pack_path).unlink(missing_ok=True)
                except OSError:
                    pass
            with self._proc_lock:
                self._addon_busy = False
def main():
    import webview  
    api = API()
    index_path = RESOURCE_DIR / "index.html"
    window = webview.create_window(
        "Crit Launcher",
        str(index_path),
        js_api=api,
        width=1080,
        height=700,
        min_size=(900, 580),
        resizable=True,
        background_color="
    )
    api.set_window(window)
    def on_closing():
        pass  
    window.events.closing += on_closing
    webview.start(debug=False)
if __name__ == "__main__":
    main()