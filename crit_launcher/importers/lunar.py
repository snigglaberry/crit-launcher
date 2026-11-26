"""Lunar Client instance scanner.

Lunar keeps per-profile mods and logs under ``~/.lunarclient/profiles`` and the
shared game folder (options, saves, resource packs) somewhere else, normally
``%APPDATA%/.minecraft``. A profile is therefore importable as one instance made
of both folders.
"""

import re
from pathlib import Path

from ..minecraft.loaders import MOD_LOADERS
from .common import _count_dir, _count_images, _launcher_roots, _read_json_safe
from .fastclient import _parse_fastclient_log_line, _read_first_log_line
from .sklauncher import _sk_vanilla_version_from_log
from .universal import (
    _details_from_game,
    _guess_loader_from_mods_dir,
    _norm_loader,
    extra_data_count,
)

LUNAR_ROOT_SOURCE_ID = "__lunar__"

# Lunar names some profile folders after the short Minecraft version.
_LUNAR_SHORT_VERSIONS = {
    "1.7": "1.7.10",
    "1.8": "1.8.9",
    "1.9": "1.9.4",
    "1.10": "1.10.2",
    "1.11": "1.11.2",
    "1.12": "1.12.2",
}
_LUNAR_DIR_RE = re.compile(
    r"^(fabric|forge|neoforge|quilt|vanilla)[-_ ]?(\d[\w.\-]*)$", re.IGNORECASE)
_LUNAR_MRPACK_RE = re.compile(
    r"(fabric|forge|neoforge|quilt)[-_ ]?(\d[\w.\-]*)", re.IGNORECASE)


def lunar_game_dir(root: Path):
    """The shared game folder configured in Lunar's launcher.json."""
    data = _read_json_safe(Path(root) / "settings" / "launcher.json")
    settings = data.get("settings") if isinstance(data, dict) else None
    if isinstance(settings, dict):
        game_dir = settings.get("gameDirectory")
        if isinstance(game_dir, str) and game_dir.strip():
            candidate = Path(game_dir)
            if candidate.is_dir():
                return candidate
    for candidate in _launcher_roots("minecraft"):
        if candidate.is_dir():
            return candidate
    return None


def lunar_profile_dir(root: Path, source_id: str) -> Path:
    return Path(root) / "profiles" / source_id


def lunar_mods_dir(root: Path, source_id: str) -> Path:
    """The folder holding a profile's mod jars (Lunar nests it per loader)."""
    mods = lunar_profile_dir(root, source_id) / "mods"
    if not mods.is_dir():
        return mods
    try:
        subdirs = [p for p in mods.iterdir() if p.is_dir()]
    except OSError:
        return mods
    if not subdirs:
        return mods
    try:
        return max(subdirs, key=lambda p: p.stat().st_mtime)
    except OSError:
        return subdirs[0]


def _split_lunar_dir(name: str):
    match = _LUNAR_DIR_RE.match(name.strip())
    if match:
        return _norm_loader(match.group(1)), match.group(2)
    text = name.strip()
    if re.match(r"^\d+\.\d+", text):
        return None, text
    return None, None


def _profile_mods(profile_dir: Path):
    mods = profile_dir / "mods"
    if not mods.is_dir():
        return None, None, None
    try:
        subdirs = [p for p in mods.iterdir() if p.is_dir()]
    except OSError:
        subdirs = []
    if not subdirs:
        return mods, None, None
    try:
        mods_dir = max(subdirs, key=lambda p: p.stat().st_mtime)
    except OSError:
        mods_dir = subdirs[0]
    loader, version = _split_lunar_dir(mods_dir.name)
    return mods_dir, loader, version


def _profile_mrpack(profile_dir: Path):
    try:
        packs = sorted(profile_dir.glob("*.mrpack"),
                       key=lambda p: p.stat().st_mtime, reverse=True)
    except OSError:
        return None, None
    for pack in packs:
        match = _LUNAR_MRPACK_RE.search(pack.stem)
        if match:
            return _norm_loader(match.group(1)), match.group(2)
    return None, None


def _profile_details(profile_dir: Path, shared_game_dir):
    mods_dir, loader, version = _profile_mods(profile_dir)
    loader_version = None
    if not version or not loader:
        pack_loader, pack_version = _profile_mrpack(profile_dir)
        loader = loader or pack_loader
        version = version or pack_version

    if not version or not loader:
        log_info = _parse_fastclient_log_line(_read_first_log_line(profile_dir / "logs"))
        if log_info:
            log_loader, log_version, log_loader_version = log_info
            loader = loader or _norm_loader(log_loader)
            version = version or log_version
            loader_version = loader_version or log_loader_version
        if not version:
            version = _sk_vanilla_version_from_log(profile_dir / "logs")

    if not version and shared_game_dir:
        detected_mc, detected_loader, detected_loader_version = _details_from_game(shared_game_dir)
        version = detected_mc
        loader = loader or detected_loader
        loader_version = loader_version or detected_loader_version

    if version in _LUNAR_SHORT_VERSIONS:
        version = _LUNAR_SHORT_VERSIONS[version]
    if not version and profile_dir.name in _LUNAR_SHORT_VERSIONS:
        version = _LUNAR_SHORT_VERSIONS[profile_dir.name]

    if not loader and mods_dir:
        loader = _guess_loader_from_mods_dir(mods_dir)
    if not loader:
        has_mods = bool(mods_dir and mods_dir.is_dir()
                        and any(mods_dir.glob("*.jar")))
        loader = "unknown" if has_mods else "vanilla"
    return mods_dir, version, _norm_loader(loader), loader_version


def _scan_lunar(root: Path) -> list:
    root = Path(root)
    profiles_dir = root / "profiles"
    if not profiles_dir.is_dir():
        return []
    shared_game_dir = lunar_game_dir(root)
    found = []
    for sub in sorted(profiles_dir.iterdir(), key=lambda p: p.name.lower()):
        if not sub.is_dir():
            continue
        mods_dir, version, loader, loader_version = _profile_details(sub, shared_game_dir)
        data_dir = shared_game_dir if shared_game_dir and shared_game_dir.is_dir() else sub
        found.append({
            "source_id": sub.name,
            "name": sub.name,
            "version": version,
            "loader": loader,
            "loader_version": loader_version,
            "has_options": (data_dir / "options.txt").is_file()
            or (data_dir / "optionsLC.txt").is_file(),
            "mods_count": _count_dir(mods_dir, ".jar") if loader in MOD_LOADERS else 0,
            "resourcepacks_count": _count_dir(data_dir / "resourcepacks"),
            "saves_count": _count_dir(data_dir / "saves"),
            "shaderpacks_count": _count_dir(data_dir / "shaderpacks"),
            "schematics_count": _count_dir(data_dir / "schematics"),
            "screenshots_count": _count_images(data_dir / "screenshots"),
            "config_count": _count_dir(data_dir / "config") if loader in MOD_LOADERS else 0,
            "extras_count": extra_data_count(data_dir),
        })
    return found
