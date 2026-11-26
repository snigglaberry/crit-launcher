"""Universal instance scanner: works with any launcher's instance or game folder.

Most launchers keep a normal Minecraft game directory (options.txt, mods, saves,
logs) plus a small metadata file describing the loader. This scanner reads those
metadata formats first and falls back to logs, version JSON and the mods
themselves, so a folder only needs to look like an instance to be importable.
"""

import os
import re
import zipfile
from pathlib import Path

from ..core.system import current_os_name
from ..minecraft.loaders import MOD_LOADERS, neoforge_version_to_mc
from .common import (
    LAUNCHER_LABELS,
    _count_dir,
    _count_images,
    _loader_from_mmc_pack,
    _loader_from_version_json,
    _read_json_safe,
)
from .fastclient import _parse_fastclient_log_line, _read_first_log_line
from .sklauncher import _sk_vanilla_version_from_log

UNIVERSAL_SOURCE_ID = "__instance__"
UNIVERSAL_LAUNCHER = "universal"

# Extra per-instance data that some launchers/modpacks keep outside the usual
# mods/config folders. Imported by the "copy other data" option.
EXTRA_GAME_DIRS = ("defaultconfigs", "kubejs", "scripts", "datapacks", "openloader",
                   "tacz", "journeymap", "xaero", "map", "replaymod", "schematica")

_GAME_SUBDIRS = ("minecraft", ".minecraft")
_WRAPPER_DIRS = ("instances", "Instances", "profiles", "Profiles")
_GAME_DIR_FILES = ("options.txt", "optionsof.txt", "optionsLC.txt", "launcher_profiles.json")
_GAME_DIR_FOLDERS = ("mods", "saves", "resourcepacks", "shaderpacks", "config",
                     "versions", "datapacks", "logs")
_LOADER_PREFIX = re.compile(
    r"^(fabric(?:[-_ ]?loader)?|quilt(?:[-_ ]?loader)?|neo[-_ ]?forge|neoforge|"
    r"liteloader|forge)[-_: ]*(.*)$", re.IGNORECASE)


def _first_str(*values):
    for value in values:
        if isinstance(value, str) and value.strip():
            return value.strip()
    return None


def _norm_loader(value) -> str:
    if isinstance(value, dict):
        value = (value.get("type") or value.get("name") or value.get("loader")
                 or value.get("id"))
    text = str(value or "").strip().lower()
    if not text:
        return "unknown"
    compact = re.sub(r"[\s_-]+", "", text)
    if "neoforge" in compact:
        return "neoforge"
    if "fabric" in compact:
        return "fabric"
    if "quilt" in compact:
        return "quilt"
    if "liteloader" in compact:
        return "liteloader"
    if "forge" in compact:
        return "forge"
    if "vanilla" in compact:
        return "vanilla"
    return compact


def _split_loader(value):
    """Split names such as ``fabric-0.15.7`` or ``neoforge-21.1.0``."""
    text = str(value or "").strip()
    match = _LOADER_PREFIX.match(text)
    if not match:
        return "unknown", None
    version = match.group(2).strip(" -_:") or None
    if version and version.lower() in ("latest", "recommended", "unknown", "any"):
        version = None
    return _norm_loader(match.group(1)), version


def game_dir_for(path: Path) -> Path:
    path = Path(path)
    for name in _GAME_SUBDIRS:
        candidate = path / name
        if candidate.is_dir():
            return candidate
    return path


def _looks_like_game_dir(game_dir: Path) -> bool:
    if not game_dir.is_dir():
        return False
    if any((game_dir / name).is_file() for name in _GAME_DIR_FILES):
        return True
    if (game_dir / "logs" / "latest.log").is_file():
        return True
    if any((game_dir / name).is_dir() for name in ("mods", "saves", "versions")):
        return True
    hits = sum(1 for name in _GAME_DIR_FOLDERS if (game_dir / name).is_dir())
    return hits >= 2


def _same_path(left, right) -> bool:
    if not left or not right:
        return False
    try:
        return Path(left).resolve() == Path(right).resolve()
    except OSError:
        return False


def _metadata(path: Path, game_dir: Path):
    """(mc version, loader, loader version, pack name) from common metadata files."""
    for base in (path, game_dir):
        pack = _read_json_safe(base / "mmc-pack.json")
        if isinstance(pack, dict) and isinstance(pack.get("components"), list):
            mc, loader, loader_version = _loader_from_mmc_pack(pack["components"])
            if mc or loader != "vanilla":
                return mc, _norm_loader(loader), loader_version, None

        profile = _read_json_safe(base / "profile.json")
        if isinstance(profile, dict):
            mc = _first_str(profile.get("game_version"), profile.get("gameVersion"),
                            profile.get("mc_version"), profile.get("minecraft"))
            raw_loader = profile.get("loader") or profile.get("mod_loader") \
                or profile.get("modLoader")
            loader_version = _first_str(profile.get("loader_version"),
                                        profile.get("loaderVersion"))
            if mc or raw_loader:
                return mc, _norm_loader(raw_loader), loader_version, \
                    _first_str(profile.get("name"))

        instance = _read_json_safe(base / "minecraftinstance.json")
        if isinstance(instance, dict):
            mc = _first_str(instance.get("gameVersion"), instance.get("minecraftVersion"),
                            instance.get("mcVersion"))
            base_loader = instance.get("baseModLoader") \
                if isinstance(instance.get("baseModLoader"), dict) else {}
            loader, loader_version = _split_loader(
                _first_str(base_loader.get("name"), base_loader.get("id")))
            loader_version = _first_str(
                loader_version, instance.get("fabricVersion"), instance.get("forgeVersion"),
                instance.get("neoForgeVersion"), instance.get("quiltVersion"))
            if mc or loader != "unknown":
                return mc, loader, loader_version, _first_str(instance.get("name"))

        instance = _read_json_safe(base / "instance.json")
        if isinstance(instance, dict):
            version = instance.get("version") \
                if isinstance(instance.get("version"), dict) else {}
            mc = _first_str(instance.get("mcVersion"), instance.get("minecraftVersion"),
                            instance.get("gameVersion"), instance.get("minecraft"),
                            version.get("minecraft"), version.get("minecraftVersion"))
            raw_version = instance.get("version")
            if not mc and isinstance(raw_version, str) and re.match(r"^\d+\.\d+", raw_version):
                mc = raw_version
            raw_loader = instance.get("loader")
            if raw_loader is None:
                raw_loader = instance.get("modLoader")
            if raw_loader is None:
                raw_loader = version.get("loader") or version.get("modLoader")
            loader_version = None
            if isinstance(raw_loader, dict):
                loader = _norm_loader(raw_loader)
                loader_version = _first_str(raw_loader.get("version"),
                                            raw_loader.get("loaderVersion"))
            else:
                loader = _norm_loader(raw_loader)
                loader_version = _first_str(instance.get("loaderVersion"),
                                            version.get("loaderVersion"))
            if loader == "unknown":
                loader, split_version = _split_loader(
                    _first_str(instance.get("loader"), instance.get("modLoader")))
                loader_version = loader_version or split_version
            if mc or loader != "unknown":
                return mc, loader, loader_version, _first_str(instance.get("name"))
    return None, None, None, None


def _read_version_json(game_dir: Path, version_id: str):
    folder = game_dir / "versions" / version_id
    if not folder.is_dir():
        return None
    data = _read_json_safe(folder / f"{version_id}.json")
    if isinstance(data, dict):
        return data
    for candidate in sorted(folder.glob("*.json")):
        data = _read_json_safe(candidate)
        if isinstance(data, dict):
            return data
    return None


def _details_from_version_id(version_id: str, game_dir: Path):
    vjson = _read_version_json(game_dir, version_id)
    if vjson:
        mc_version = vjson.get("inheritsFrom") or vjson.get("id") or version_id
        loader, loader_version = _loader_from_version_json(vjson, mc_version)
        return mc_version, loader, loader_version

    match = re.match(r"^fabric-loader-([\w.]+)-(.+)$", version_id)
    if match:
        return match.group(2), "fabric", match.group(1)
    match = re.match(r"^quilt-loader-([\w.]+)-(.+)$", version_id)
    if match:
        return match.group(2), "quilt", match.group(1)
    match = re.match(r"^(.+?)-forge-([\w.\-]+)$", version_id)
    if match:
        return match.group(1), "forge", match.group(2)
    match = re.match(r"^forge-(\d[\w.\-]*)-([\w.\-]+)$", version_id)
    if match:
        return match.group(1), "forge", match.group(2)
    match = re.match(r"^neoforge-([\w.\-]+)$", version_id)
    if match:
        return neoforge_version_to_mc(match.group(1)), "neoforge", match.group(1)
    if re.match(r"^\d+\.\d+", version_id):
        return version_id, "vanilla", None
    return None, None, None


def _jar_implementation_version(path: Path):
    try:
        with zipfile.ZipFile(path) as archive:
            raw = archive.read("META-INF/MANIFEST.MF").decode("utf-8", "ignore")
    except (OSError, KeyError, zipfile.BadZipFile):
        return None
    for line in raw.splitlines():
        if line.lower().startswith("implementation-version:"):
            return line.split(":", 1)[1].strip() or None
    return None


def _details_from_game(game_dir: Path):
    logs = game_dir / "logs"
    log_info = _parse_fastclient_log_line(_read_first_log_line(logs))
    if log_info:
        loader, mc_version, loader_version = log_info
        return mc_version, _norm_loader(loader), loader_version
    mc_version = _sk_vanilla_version_from_log(logs)
    if mc_version:
        return mc_version, "vanilla", None

    # Technic-style packs keep the Forge build in bin/modpack.jar.
    impl = _jar_implementation_version(game_dir / "bin" / "modpack.jar")
    if impl:
        match = re.match(r"^(\d+\.\d+(?:\.\d+)?)-(.+)$", impl)
        if match:
            return match.group(1), "forge", match.group(2)
        if re.match(r"^\d+\.\d+", impl):
            return impl, "forge", None

    profiles = _read_json_safe(game_dir / "launcher_profiles.json")
    if isinstance(profiles, dict):
        entries = profiles.get("profiles") if isinstance(profiles.get("profiles"), dict) else {}
        ordered = []
        for entry in entries.values():
            if not isinstance(entry, dict):
                continue
            version_id = _first_str(entry.get("lastVersionId"), entry.get("versionId"))
            if version_id:
                ordered.append((entry, version_id))
        ordered.sort(key=lambda pair: 0 if _same_path(pair[0].get("gameDir"), game_dir) else 1)
        for _entry, version_id in ordered:
            mc_version, loader, loader_version = _details_from_version_id(version_id, game_dir)
            if mc_version:
                return mc_version, loader, loader_version

    versions_dir = game_dir / "versions"
    if versions_dir.is_dir():
        candidates = []
        for sub in versions_dir.iterdir():
            if not sub.is_dir():
                continue
            try:
                candidates.append((sub.stat().st_mtime, sub.name))
            except OSError:
                continue
        for _mtime, name in sorted(candidates, reverse=True):
            mc_version, loader, loader_version = _details_from_version_id(name, game_dir)
            if mc_version:
                return mc_version, loader, loader_version
    return None, None, None


def _guess_loader_from_mods_dir(mods: Path):
    if not mods.is_dir():
        return None
    try:
        jars = sorted(p for p in mods.iterdir()
                      if p.is_file() and p.suffix.lower() == ".jar")[:12]
    except OSError:
        return None
    found = None
    for jar in jars:
        try:
            with zipfile.ZipFile(jar) as archive:
                names = set(archive.namelist())
        except (OSError, zipfile.BadZipFile):
            continue
        if "quilt.mod.json" in names:
            return "quilt"
        if "META-INF/neoforge.mods.toml" in names:
            return "neoforge"
        if "fabric.mod.json" in names:
            found = found or "fabric"
        if "litemod.json" in names:
            found = found or "liteloader"
        if "META-INF/mods.toml" in names:
            if any(name.startswith("net/neoforged/") for name in names):
                return "neoforge"
            found = found or "forge"
    return found


def _guess_loader_from_mods(game_dir: Path):
    return _guess_loader_from_mods_dir(game_dir / "mods")


def fill_instance_details(inst: dict, game_dir: Path, mods_dir: Path = None):
    """Fill in missing version/loader details from an instance's game folder."""
    if not game_dir or not game_dir.is_dir():
        return inst
    if inst.get("version") and inst.get("loader") not in (None, "", "unknown"):
        return inst
    mc, loader, loader_version, _name = _metadata(game_dir, game_dir)
    detected_mc, detected_loader, detected_loader_version = _details_from_game(game_dir)
    mc = inst.get("version") or mc or detected_mc
    loader = inst.get("loader")
    mods = mods_dir or (game_dir / "mods")
    if (not loader or loader in ("unknown", "vanilla")) and detected_loader in MOD_LOADERS:
        loader = detected_loader
        loader_version = inst.get("loader_version") or loader_version or detected_loader_version
    elif not loader or loader == "unknown":
        loader = detected_loader or loader or "unknown"
        loader_version = inst.get("loader_version") or loader_version or detected_loader_version
    else:
        loader_version = inst.get("loader_version") or loader_version or detected_loader_version
    if loader in ("unknown", "vanilla") and mods.is_dir():
        guessed = _guess_loader_from_mods_dir(mods)
        if guessed:
            loader = guessed
    if loader == "unknown" and not mods.is_dir():
        loader = "vanilla"
    loader = _norm_loader(loader)
    inst["version"] = mc or inst.get("version")
    inst["loader"] = loader
    inst["loader_version"] = loader_version or inst.get("loader_version")
    if loader in MOD_LOADERS:
        if not inst.get("mods_count"):
            inst["mods_count"] = _count_dir(mods, ".jar")
        if not inst.get("config_count"):
            inst["config_count"] = _count_dir(game_dir / "config")
    return inst


def extra_data_count(game_dir: Path) -> int:
    return sum(_count_dir(game_dir / name) for name in EXTRA_GAME_DIRS)


def describe_instance(path: Path, source_id: str = UNIVERSAL_SOURCE_ID, name=None):
    """Describe one instance folder, or return None when it isn't an instance."""
    path = Path(path)
    if not path.is_dir():
        return None
    game_dir = game_dir_for(path)
    mc_version, loader, loader_version, pack_name = _metadata(path, game_dir)
    if not _looks_like_game_dir(game_dir) and not (mc_version or loader):
        return None

    detected_mc, detected_loader, detected_loader_version = _details_from_game(game_dir)
    mc_version = mc_version or detected_mc
    if (not loader or loader in ("unknown", "vanilla")) and detected_loader in MOD_LOADERS:
        loader = detected_loader
        loader_version = loader_version or detected_loader_version
    elif not loader or loader == "unknown":
        loader = detected_loader or "unknown"
        loader_version = loader_version or detected_loader_version
    elif not loader_version:
        loader_version = detected_loader_version

    if loader in ("unknown", "vanilla") and (game_dir / "mods").is_dir():
        guessed = _guess_loader_from_mods(game_dir)
        if guessed:
            loader = guessed
    loader = _norm_loader(loader)
    if loader == "unknown" and not (game_dir / "mods").is_dir():
        loader = "vanilla"

    return {
        "source_id": source_id,
        "name": pack_name or name or path.name,
        "version": mc_version,
        "loader": loader,
        "loader_version": loader_version,
        "has_options": (game_dir / "options.txt").is_file()
        or (game_dir / "optionsof.txt").is_file(),
        "mods_count": _count_dir(game_dir / "mods", ".jar"),
        "resourcepacks_count": _count_dir(game_dir / "resourcepacks"),
        "saves_count": _count_dir(game_dir / "saves"),
        "shaderpacks_count": _count_dir(game_dir / "shaderpacks"),
        "schematics_count": _count_dir(game_dir / "schematics"),
        "screenshots_count": _count_images(game_dir / "screenshots"),
        "config_count": _count_dir(game_dir / "config") if loader in MOD_LOADERS else 0,
        "extras_count": extra_data_count(game_dir),
    }


def scan_universal(root: Path) -> list:
    """Find an instance at ``root`` itself, or instance folders directly inside it."""
    root = Path(root)
    if not root.is_dir():
        return []
    direct = describe_instance(root, UNIVERSAL_SOURCE_ID)
    if direct:
        return [direct]
    found = []
    try:
        children = sorted(root.iterdir(), key=lambda p: p.name.lower())
    except OSError:
        return []
    for sub in children:
        if not sub.is_dir():
            continue
        instance = describe_instance(sub, sub.name)
        if instance:
            found.append(instance)
    return found


def universal_known_roots():
    """Folders used by launchers that don't have a dedicated scanner yet."""
    system = current_os_name()
    home = Path.home()
    if system == "windows":
        appdata = Path(os.environ.get("APPDATA") or (home / "AppData" / "Roaming"))
        local = Path(os.environ.get("LOCALAPPDATA") or (home / "AppData" / "Local"))
        documents = home / "Documents"
        return [
            ("Minecraft Launcher", appdata / ".minecraft"),
            ("ATLauncher", appdata / "ATLauncher" / "instances"),
            ("GDLauncher", appdata / "gdlauncher_next" / "instances"),
            ("GDLauncher", appdata / "GDLauncher" / "instances"),
            ("CurseForge", documents / "curseforge" / "minecraft" / "Instances"),
            ("CurseForge", documents / "CurseForge" / "minecraft" / "Instances"),
            ("Technic", appdata / ".technic" / "modpacks"),
            ("FTB App", local / "FTBApp" / "instances"),
            ("MultiMC", appdata / "MultiMC" / "instances"),
        ]
    if system == "osx":
        support = home / "Library" / "Application Support"
        return [
            ("Minecraft Launcher", home / "Library" / "Application Support" / "minecraft"),
            ("ATLauncher", support / "ATLauncher" / "instances"),
            ("GDLauncher", support / "gdlauncher_next" / "instances"),
            ("CurseForge", home / "Documents" / "curseforge" / "minecraft" / "Instances"),
            ("Technic", support / ".technic" / "modpacks"),
            ("FTB App", support / "FTBApp" / "instances"),
            ("MultiMC", support / "MultiMC" / "instances"),
        ]
    data = Path(os.environ.get("XDG_DATA_HOME") or (home / ".local" / "share"))
    config = Path(os.environ.get("XDG_CONFIG_HOME") or (home / ".config"))
    return [
        ("Minecraft Launcher", home / ".minecraft"),
        ("ATLauncher", data / "ATLauncher" / "instances"),
        ("GDLauncher", config / "gdlauncher_next" / "instances"),
        ("CurseForge", home / "Documents" / "curseforge" / "minecraft" / "Instances"),
        ("Technic", home / ".technic" / "modpacks"),
        ("FTB App", home / ".ftba" / "instances"),
        ("MultiMC", data / "multimc" / "instances"),
    ]


def scan_universal_known_roots() -> list:
    """Universal sources for common launchers that have no dedicated scanner."""
    sources = []
    seen = set()
    for label, root in universal_known_roots():
        try:
            key = root.resolve()
        except OSError:
            key = root
        if key in seen or not root.is_dir():
            continue
        seen.add(key)
        try:
            instances = scan_universal(root)
        except Exception:
            continue
        if instances:
            sources.append({
                "launcher": UNIVERSAL_LAUNCHER,
                "label": f"{label} (universal)",
                "root": str(root),
                "instances": instances,
                "custom": True,
                "discovered": True,
            })
    return sources


def scan_universal_source(root: Path, force_instance: bool = False):
    """Build an import source for an arbitrary folder (or None if nothing matches)."""
    root = Path(root)
    instances = scan_universal(root)
    if instances:
        return {"launcher": UNIVERSAL_LAUNCHER,
                "label": f"{LAUNCHER_LABELS[UNIVERSAL_LAUNCHER]} · {root.name}",
                "root": str(root), "instances": instances, "custom": True}
    if force_instance:
        return None
    for wrapper in _WRAPPER_DIRS:
        sub = root / wrapper
        if not sub.is_dir():
            continue
        instances = scan_universal(sub)
        if instances:
            return {"launcher": UNIVERSAL_LAUNCHER,
                    "label": f"{LAUNCHER_LABELS[UNIVERSAL_LAUNCHER]} · {sub.name}",
                    "root": str(sub), "instances": instances, "custom": True}
    return None
