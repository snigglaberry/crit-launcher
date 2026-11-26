"""Finding the newest Minecraft Bedrock release from the public package catalogs.

Crit Launcher only offers the latest Bedrock release, so this module deliberately
returns one release instead of a version list. The catalog formats follow the public
GdkLinks catalog (modern .msixvc builds) and raythnetwork's UWP list (older .Appx
builds), which is also what BedrockLauncher uses.
"""

import json
import platform
import re
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

from ..config.paths import BEDROCK_CACHE_DIR
from ..core.http import http_get_text

GDK_LINKS_URLS = (
    "https://raw.githubusercontent.com/MinecraftBedrockArchiver/GdkLinks/master/urls.json",
    "https://www.raythnetwork.co.uk/gdk.urls.min.json",
    "https://www.raythnetwork.co.uk/gdk.urls.json",
)
COMMUNITY_UWP_URL = "https://www.raythnetwork.co.uk/versions.php?type=json"
CATALOG_MAX_AGE = 6 * 3600

PUBLISHER_ID = "8wekyb3d8bbwe"
IDENTITY_NAME = {"release": "Microsoft.MinecraftUWP"}


def version_tuple(text: str) -> Optional[tuple[int, int, int, int]]:
    match = re.match(r"^(\d+)\.(\d+)\.(\d+)\.(\d+)", text or "")
    return tuple(int(part) for part in match.groups()) if match else None  # type: ignore[return-value]


def game_to_package_version(name: str) -> Optional[tuple[int, int, int, int]]:
    """1.21.120.4 is packaged as 1.21.12004.0 (Major.Minor.Build*100+Revision.0)."""
    parsed = version_tuple(name)
    if not parsed or parsed[3] > 99:
        return None
    return (parsed[0], parsed[1], parsed[2] * 100 + parsed[3], 0)


def package_to_game_version(version: tuple[int, int, int, int]) -> str:
    if version[3] == 0 and version[2] >= 1 and version[1] >= 14:
        return f"{version[0]}.{version[1]}.{version[2] // 100}.{version[2] % 100}"
    return ".".join(map(str, version))


def machine_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64", "x64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86", "i386", "i686"):
        return "x86"
    return machine


@dataclass(frozen=True)
class PackageIdentity:
    name: str
    version: tuple[int, int, int, int]
    arch: str
    publisher: str

    @property
    def family(self) -> str:
        return f"{self.name}_{self.publisher}"

    @property
    def full_name(self) -> str:
        return (f"{self.name}_{'.'.join(map(str, self.version))}_"
                f"{self.arch}__{self.publisher}")

    def key(self) -> tuple:
        return (self.name.lower(), self.version, self.arch.lower(), self.publisher.lower())

    @staticmethod
    def parse(full_name: str) -> Optional["PackageIdentity"]:
        parts = (full_name or "").strip().split("_")
        if len(parts) != 5 or not parts[0] or parts[3] or not parts[4]:
            return None
        version = version_tuple(parts[1])
        if not version or parts[1].count(".") != 3:
            return None
        if parts[2].lower() not in ("x64", "x86", "arm64", "arm"):
            return None
        return PackageIdentity(parts[0].strip(), version, parts[2].strip().lower(),
                               parts[4].strip().lower())

    @staticmethod
    def from_url(url: str) -> Optional["PackageIdentity"]:
        filename = url.split("?", 1)[0].rsplit("/", 1)[-1]
        if not filename.lower().endswith(".msixvc"):
            return None
        return PackageIdentity.parse(filename[:-len(".msixvc")])


@dataclass(frozen=True)
class BedrockRelease:
    name: str
    kind: str                       # gdk | uwp
    arch: str
    identity: str = ""              # GDK package full name
    urls: tuple[str, ...] = ()      # GDK mirrors
    update_id: str = ""             # UWP Windows Update id

    @property
    def sort_key(self) -> tuple:
        return (version_tuple(self.name) or (0, 0, 0, 0), self.kind == "gdk")


def parse_gdk_links(text: str) -> list[BedrockRelease]:
    """GdkLinks urls.json: {channel: {version: [urls] | {arch: [urls]}}}."""
    data = json.loads(text)
    releases: list[BedrockRelease] = []

    def add(name: str, urls: list, arch_hint: Optional[str]) -> None:
        urls = list(dict.fromkeys(u for u in urls if isinstance(u, str) and u.strip()))
        if not urls:
            return
        identities = [PackageIdentity.from_url(u) for u in urls]
        if any(identity is None for identity in identities):
            return
        identity = identities[0]
        assert identity is not None
        if any(other.key() != identity.key() for other in identities if other is not None):
            return
        if (identity.name.lower() != IDENTITY_NAME["release"].lower()
                or identity.publisher != PUBLISHER_ID):
            return
        if game_to_package_version(name) != identity.version:
            return
        if arch_hint and arch_hint.lower() != identity.arch:
            return
        releases.append(BedrockRelease(
            name=name,
            kind="gdk",
            arch=identity.arch,
            identity=identity.full_name,
            urls=tuple(url for url in urls
                       if (PackageIdentity.from_url(url) or identity).key() == identity.key()),
        ))

    section = data.get("release")
    if not isinstance(section, dict):
        return releases
    for raw_name, value in section.items():
        name = str(raw_name).strip()
        if not version_tuple(name):
            continue
        if isinstance(value, list):
            add(name, value, None)
        elif isinstance(value, dict):
            for arch, urls in value.items():
                if isinstance(urls, list):
                    add(name, urls, str(arch))
    return releases


def parse_community_uwp(text: str) -> list[BedrockRelease]:
    """raythnetwork list: [[version, updateId, type, arch, packageType], ...].

    type 0 is the release channel and packageType 0 is the old .Appx package.
    """
    releases: list[BedrockRelease] = []
    for item in json.loads(text):
        if not isinstance(item, list) or len(item) < 3:
            continue
        name, update_id, channel_type = item[0], item[1], item[2]
        arch = item[3] if len(item) >= 4 and item[3] else "???"
        package_type = item[4] if len(item) >= 5 else 0
        if package_type != 0 or channel_type != 0:
            continue
        if not name or not update_id or not version_tuple(str(name)):
            continue
        releases.append(BedrockRelease(str(name), "uwp", str(arch), update_id=str(update_id)))
    return releases


def _fetch_catalog(urls: Iterable[str], cache: Path, refresh: bool) -> str | None:
    if not refresh and cache.exists() and time.time() - cache.stat().st_mtime < CATALOG_MAX_AGE:
        try:
            return cache.read_text(encoding="utf-8")
        except OSError:
            pass
    last_error = None
    for url in urls:
        try:
            text = http_get_text(url)
            json.loads(text)
        except Exception as exc:
            last_error = exc
            continue
        try:
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_text(text, encoding="utf-8")
        except OSError:
            pass
        return text
    if cache.exists():
        try:
            return cache.read_text(encoding="utf-8")
        except OSError:
            pass
    if last_error:
        return None
    return None


def latest_release(refresh: bool = False) -> Optional[BedrockRelease]:
    """The newest release-channel Bedrock package for this PC's architecture."""
    candidates: list[BedrockRelease] = []
    gdk_text = _fetch_catalog(GDK_LINKS_URLS, BEDROCK_CACHE_DIR / "gdk_links.json", refresh)
    if gdk_text:
        try:
            candidates += parse_gdk_links(gdk_text)
        except (ValueError, TypeError):
            pass
    uwp_text = _fetch_catalog((COMMUNITY_UWP_URL,),
                              BEDROCK_CACHE_DIR / "uwp_community.json", refresh)
    if uwp_text:
        try:
            candidates += parse_community_uwp(uwp_text)
        except (ValueError, TypeError):
            pass

    arch = machine_arch()
    compatible = [release for release in candidates
                  if release.arch.lower() in (arch, "???", "neutral")]
    return max(compatible, key=lambda release: release.sort_key) if compatible else None
