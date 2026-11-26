"""Crit Client release assets, local JARs, and the default mod set."""

import json
import os
import re
import tempfile
import threading
import time
import zlib
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from urllib.parse import urlparse
from zipfile import BadZipFile, ZipFile

from ..core.fsutil import file_sha256
from ..core.http import http_download, http_get_json
from ..instances.store import instance_mods_dir

CRIT_CLIENT_REPO = "snigglaberry/crit-client"
CRIT_CLIENT_LATEST_RELEASE_URL = (
    f"https://api.github.com/repos/{CRIT_CLIENT_REPO}/releases/latest")
CRIT_CLIENT_LOADER = "fabric"

_ASSET_NAME = re.compile(r"crit-client-(\d+(?:\.\d+)*)\.jar", re.IGNORECASE)
_ASSET_DIGEST = re.compile(r"sha256:([0-9a-f]{64})", re.IGNORECASE)
_CACHE_SECONDS = 300
_release_cache = (0.0, {})
_release_lock = threading.Lock()


@dataclass(frozen=True)
class ClientAsset:
    name: str
    url: str
    size: int | None
    sha256: str | None


def latest_client_assets() -> dict[str, ClientAsset]:
    """Return the latest GitHub release's assets, keyed by Minecraft version."""
    global _release_cache
    with _release_lock:
        expires, versions = _release_cache
        if time.monotonic() < expires:
            return versions

        data = http_get_json(CRIT_CLIENT_LATEST_RELEASE_URL, timeout=10, retries=0)
        versions = {}
        for asset in (data.get("assets") or []) if isinstance(data, dict) else []:
            if not isinstance(asset, dict):
                continue
            name, url = str(asset.get("name") or ""), asset.get("browser_download_url")
            match = _ASSET_NAME.fullmatch(name)
            if not match or not isinstance(url, str):
                continue
            parsed = urlparse(url)
            if (parsed.scheme != "https" or parsed.netloc != "github.com" or
                    not parsed.path.startswith(f"/{CRIT_CLIENT_REPO}/releases/download/") or
                    parsed.path.rsplit("/", 1)[-1] != name):
                continue
            digest = _ASSET_DIGEST.fullmatch(str(asset.get("digest") or ""))
            size = asset.get("size")
            versions.setdefault(match[1], ClientAsset(
                name, url, size if isinstance(size, int) and size > 0 else None,
                digest[1].lower() if digest else None,
            ))
        _release_cache = (time.monotonic() + _CACHE_SECONDS, versions)
        return versions


@lru_cache(maxsize=4096)
def _cached_fabric_info(path: Path, size: int, modified: int, changed: int) -> tuple[str | None, bool]:
    try:
        with ZipFile(path) as jar:
            with jar.open("fabric.mod.json") as manifest:
                data = json.loads(manifest.read(64 * 1024))
                if isinstance(data, dict) and isinstance(data.get("id"), str):
                    custom = data.get("custom")
                    bridge = custom.get("critclient:launcher_skin_bridge") if isinstance(custom, dict) else None
                    return data["id"], type(bridge) is int and bridge == 1
    except (BadZipFile, EOFError, KeyError, OSError, ValueError, RuntimeError, zlib.error):
        pass
    return None, False


def fabric_mod_info(path: Path) -> tuple[str | None, bool]:
    try:
        stat = path.stat()
    except FileNotFoundError:
        return None, False
    return _cached_fabric_info(path, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns)


def _client_jars(mods_dir: Path) -> list[Path]:
    if not mods_dir.is_dir():
        return []
    jars = []
    for path in mods_dir.iterdir():
        if path.suffix.lower() != ".jar" or not path.is_file():
            continue
        if fabric_mod_info(path)[0] == "critclient":
            jars.append(path)
    return jars


def client_skin_bridge(name: str) -> bool | None:
    """True for bridge v1, False for an older enabled client, None for no client."""
    jars = _client_jars(instance_mods_dir(name))
    return all(fabric_mod_info(path)[1] for path in jars) if jars else None


def client_jar_status(mods_dir: Path, asset: ClientAsset) -> str:
    jars = _client_jars(mods_dir)
    if not jars:
        return "missing"
    # The filename refers to the Minecraft version, not the Crit Client build.
    for path in jars:
        if asset.size is not None and path.stat().st_size != asset.size:
            return "outdated"
        if asset.sha256 is not None and file_sha256(path) != asset.sha256:
            return "outdated"
    return "current" if asset.sha256 and len(jars) == 1 else "installed"


def install_client_jar(mods_dir: Path, asset: ClientAsset, progress_cb=None) -> None:
    """Download and verify the JAR before replacing anything in the mods folder."""
    mods_dir.mkdir(parents=True, exist_ok=True)
    dest = mods_dir / asset.name
    with tempfile.TemporaryDirectory(prefix=".crit-client-", dir=mods_dir.parent) as tmp:
        staged = Path(tmp) / asset.name
        http_download(asset.url, staged, expected_sha1=asset.sha256, progress_cb=progress_cb)
        if ((asset.size is not None and staged.stat().st_size != asset.size)
                or fabric_mod_info(staged)[0] != "critclient"):
            raise IOError("The Crit Client download isn't a valid JAR.")
        previous = _client_jars(mods_dir)
        if (dest.exists() or dest.is_symlink()) and dest not in previous:
            raise FileExistsError(f"Another file is already named {asset.name} in this instance.")
        backups = []
        try:
            for old in previous:
                if old == dest:
                    continue
                # Keep renamed or imported copies as disabled backups and avoid loading
                # two mods with the same ID. Leave all unrelated JARs alone.
                backup = old.with_name(old.name + ".disabled")
                n = 2
                while backup.exists() or backup.is_symlink():
                    backup = old.with_name(f"{old.stem}-{n}.jar.disabled")
                    n += 1
                old.rename(backup)
                backups.append((old, backup))
            os.replace(staged, dest)
        except OSError:
            for old, backup in reversed(backups):
                backup.rename(old)
            raise

# Modrinth slugs, plus "cf:<slug>" for CurseForge projects. A mod is skipped when it
# has no build for the instance's Minecraft version and loader.
CRIT_CLIENT_MODS = [
    "appleskin",
    "badoptimizations",
    "c2me-fabric",
    "chat-heads",
    "chunky",
    "cloth-config",
    "clumps",
    "dynamic-fps",
    "entityculling",
    "fabric-api",
    "fabric-language-kotlin",
    "ferrite-core",
    "forge-config-api-port",
    "fzzy-config",
    "get-it-together-drops",
    "immediatelyfast",
    "iris",
    "konkrete",
    "krypton",
    "lambdynamiclights",
    "lithium",
    "CVT4pFB2",
    "moreculling",
    "mouse-tweaks",
    "noisium",
    "cf:obe",
    "particle-core",
    "placeholder-api",
    "quick-pack",
    "resourceful-config",
    "scalablelux",
    "sodium-extra",
    "sodium",
    "structure-layout-optimizer",
    "vmp-fabric",
    "yacl",
    "zconfig",
]
