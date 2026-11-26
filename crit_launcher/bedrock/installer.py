"""Downloading and starting the latest Bedrock release for a single instance.

Each Bedrock instance owns its own package folder under ``bedrock/instances/<name>``.
The installer only ever resolves the newest release; there is no version picker.
"""

import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from ..core.system import CREATE_NO_WINDOW
from ..instances.store import (
    bedrock_instance_download_dir,
    read_instance_meta,
    update_instance_meta,
)
from ..minecraft.constants import INSTALL_SCHEMA
from .catalog import (
    BedrockRelease,
    PackageIdentity,
    latest_release,
)
from .store_network import resolve_download

GAME_PROCESS = "Minecraft.Windows.exe"
MANIFEST_NAME = "AppxManifest.xml"


class BedrockError(RuntimeError):
    pass


def is_windows() -> bool:
    return sys.platform == "win32"


def _require_windows() -> None:
    if not is_windows():
        raise BedrockError("Minecraft Bedrock can only be installed on Windows.")


def _looks_like_text(path: Path) -> str | None:
    size = path.stat().st_size
    with open(path, "rb") as handle:
        head = handle.read(min(1024, size))
    text = head.lstrip(b"\xef\xbb\xbf \t\r\n").decode("utf-8", "replace").lower()
    for prefix in ("<!doctype", "<html", "<?xml", "{", "[", "error", "not found"):
        if text.startswith(prefix):
            return "the download returned a web page instead of a package"
    if size < 64 * 1024 and head:
        printable = sum(1 for byte in head if byte in (9, 10, 13) or 32 <= byte <= 126)
        if printable / len(head) > 0.9:
            return "the downloaded file is too small and looks like text, not a package"
    return None


def _download(url: str, dest: Path, label: str, tracker=None) -> None:
    tmp = dest.with_name(dest.name + ".part")
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp.unlink(missing_ok=True)
    request = urllib.request.Request(url, headers={
        "User-Agent": "CritLauncher/1.0 (+https://critlauncher.xyz)"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response, open(tmp, "wb") as out:
            total = int(response.headers.get("Content-Length") or 0)
            if tracker is not None:
                tracker.set_task(f"Downloading {label}", force=True)
                if total:
                    tracker.reset(total)
            while True:
                chunk = response.read(1 << 20)
                if not chunk:
                    break
                out.write(chunk)
                if tracker is not None:
                    tracker.add(len(chunk))
        size = tmp.stat().st_size
        if size == 0:
            raise BedrockError("the downloaded package is empty")
        if total and size != total:
            raise BedrockError(f"the download is incomplete ({size} of {total} bytes)")
        problem = _looks_like_text(tmp)
        if problem:
            raise BedrockError(problem)
        os.replace(tmp, dest)
    except (urllib.error.URLError, OSError) as exc:
        raise BedrockError(f"download failed: {exc}") from exc
    finally:
        tmp.unlink(missing_ok=True)


def _remove_old_packages(folder: Path, keep: Path) -> None:
    if not folder.is_dir():
        return
    for item in folder.iterdir():
        if item == keep or not item.is_file():
            continue
        if item.suffix.lower() in (".msixvc", ".appx", ".part"):
            try:
                item.unlink()
            except OSError:
                pass


def _download_release(release: BedrockRelease, instance_name: str, tracker=None) -> Path:
    download_dir = bedrock_instance_download_dir(instance_name)
    download_dir.mkdir(parents=True, exist_ok=True)
    if release.kind == "gdk":
        identity = PackageIdentity.parse(release.identity)
        if not identity:
            raise BedrockError("the catalog entry for the latest Bedrock build is invalid")
        dest = download_dir / f"{identity.full_name}.msixvc"
        if dest.is_file() and dest.stat().st_size > 0 and not _looks_like_text(dest):
            _remove_old_packages(download_dir, dest)
            return dest
        last_error = None
        for url in release.urls:
            try:
                _download(url, dest, f"Bedrock {release.name}", tracker)
                last_error = None
                break
            except BedrockError as exc:
                last_error = exc
        if last_error or not dest.is_file():
            raise BedrockError(f"could not download Bedrock {release.name}: {last_error}")
    else:
        dest = download_dir / f"Minecraft-{release.name}.Appx"
        if dest.is_file() and zipfile.is_zipfile(dest):
            _remove_old_packages(download_dir, dest)
            return dest
        try:
            link = resolve_download(release.update_id)
        except Exception as exc:
            raise BedrockError(str(exc)) from exc
        _download(link, dest, f"Bedrock {release.name}", tracker)
    _remove_old_packages(download_dir, dest)
    return dest


def ensure_latest(instance_name: str, tracker=None, refresh: bool = False) -> tuple[BedrockRelease, Path]:
    """Download the newest release into this instance's own folder and record it."""
    _require_windows()
    release = latest_release(refresh=refresh)
    if not release:
        raise BedrockError("could not find a downloadable Minecraft Bedrock release")
    package = _download_release(release, instance_name, tracker)
    update_instance_meta(instance_name, lambda meta: meta.update({
        "version": release.name,
        "loader": "bedrock",
        "loader_version": None,
        "installed": True,
        "schema": INSTALL_SCHEMA,
        "bedrock_kind": release.kind,
        "bedrock_arch": release.arch,
        "bedrock_identity": release.identity,
        "bedrock_update_id": release.update_id,
        "bedrock_package": package.name,
        "bedrock_updated": time.time(),
    }))
    return release, package


def _powershell(command: str, what: str) -> str:
    executable = (Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
                  / "WindowsPowerShell" / "v1.0" / "powershell.exe")
    process = subprocess.run(
        [str(executable), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
         "-Command", command],
        capture_output=True, text=True, errors="replace", creationflags=CREATE_NO_WINDOW,
    )
    if process.returncode != 0:
        detail = "\n".join(part.strip() for part in (process.stdout, process.stderr)
                           if part.strip())
        raise BedrockError(f"{what} failed.\n{detail}".strip())
    return process.stdout


def _ps_quote(value: str) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def _packages(name: str) -> list[dict]:
    output = _powershell(
        "$p = @(Get-AppxPackage -Name " + _ps_quote(name) + " | Select-Object "
        "PackageFullName,PackageFamilyName,InstallLocation,"
        "@{n='Version';e={$_.Version.ToString()}}); "
        "if ($p.Count -eq 0) { '[]' } else { ConvertTo-Json -InputObject $p -Compress }",
        "package query",
    )
    try:
        data = json.loads(output.strip() or "[]")
    except ValueError as exc:
        raise BedrockError("Windows returned an unreadable package list") from exc
    if isinstance(data, dict):
        data = [data]
    return [item for item in data if isinstance(item, dict)]


def _same_path(left: str, right: str) -> bool:
    if not left or not right:
        return False
    return (os.path.normcase(os.path.abspath(left)).rstrip("\\/")
            == os.path.normcase(os.path.abspath(right)).rstrip("\\/"))


def _version_tuple(text: str):
    try:
        return tuple(int(part) for part in str(text).split(".")[:4])
    except ValueError:
        return (0, 0, 0, 0)


def _installed_identity(packages: list[dict], identity: PackageIdentity) -> dict | None:
    for package in packages:
        parsed = PackageIdentity.parse(package.get("PackageFullName") or "")
        if parsed and parsed.key() == identity.key():
            return package
        if (package.get("PackageFamilyName") or "").lower() == identity.family.lower():
            if _version_tuple(package.get("Version") or "") >= identity.version:
                return package
    return None


def _gaming_services_installed() -> bool:
    return bool(_packages("Microsoft.GamingServices"))


def _read_manifest(folder: str | Path) -> tuple[str, tuple[int, int, int, int], str] | None:
    try:
        root = ET.parse(Path(folder) / MANIFEST_NAME).getroot()
    except (OSError, ET.ParseError):
        return None
    identity = next((element for element in root.iter()
                     if element.tag.rsplit("}", 1)[-1] == "Identity"), None)
    application = next((element for element in root.iter()
                        if element.tag.rsplit("}", 1)[-1] == "Application"), None)
    if identity is None:
        return None
    version = _version_tuple(identity.get("Version") or "")
    return (identity.get("Name") or "", version,
            (application.get("Id") if application is not None else None) or "")


def _install_gdk(release: BedrockRelease, instance_name: str) -> dict:
    identity = PackageIdentity.parse(release.identity)
    if not identity:
        raise BedrockError("the latest Bedrock build has no usable package identity")
    packages = _packages(identity.name)
    installed = _installed_identity(packages, identity)
    if installed:
        return installed
    if not _gaming_services_installed():
        raise BedrockError(
            "Gaming Services is required to install modern Bedrock builds. "
            "Install it from the Microsoft Store (opening the Xbox app also installs it).")
    package_path = _download_release(release, instance_name)
    _powershell(
        "Add-AppxPackage -ForceApplicationShutdown -Path " + _ps_quote(str(package_path)),
        "Bedrock package installation",
    )
    installed = _installed_identity(_packages(identity.name), identity)
    if not installed:
        raise BedrockError("Windows did not report the Bedrock package as installed.")
    return installed


def _developer_mode_enabled() -> bool:
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r"SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock") as key:
            return winreg.QueryValueEx(key, "AllowDevelopmentWithoutDevLicense")[0] == 1
    except (OSError, ImportError):
        return False


def _extract_appx(package_path: Path, folder: Path) -> None:
    folder = folder.resolve()
    if folder.exists():
        shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True, exist_ok=True)
    try:
        with zipfile.ZipFile(package_path) as archive:
            for info in archive.infolist():
                target = (folder / info.filename).resolve()
                if target != folder and folder not in target.parents:
                    raise BedrockError("the downloaded package contains an unsafe path")
                if info.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as source, open(target, "wb") as out:
                    shutil.copyfileobj(source, out, 1 << 20)
    except zipfile.BadZipFile as exc:
        raise BedrockError("the downloaded Bedrock package is not a valid .Appx archive") from exc
    manifest = folder / MANIFEST_NAME
    if not manifest.is_file():
        raise BedrockError("the Bedrock package has no AppxManifest.xml")
    (folder / "AppxSignature.p7x").unlink(missing_ok=True)


def _install_uwp(release: BedrockRelease, instance_name: str) -> dict:
    if not _developer_mode_enabled():
        raise BedrockError(
            "Developer Mode is required to register older Bedrock packages. "
            "Turn it on under Settings > Privacy & security > For developers.")
    package_path = _download_release(release, instance_name)
    folder = bedrock_instance_download_dir(instance_name) / "uwp"
    _extract_appx(package_path, folder)
    manifest = _read_manifest(folder)
    if not manifest:
        raise BedrockError("the Bedrock package manifest could not be read")
    existing = _packages(manifest[0])
    for package in existing:
        if not _same_path(package.get("InstallLocation") or "", str(folder)):
            raise BedrockError(
                f"another Minecraft package is already installed at "
                f"{package.get('InstallLocation') or 'an unknown location'}. "
                "Uninstall it before using this Bedrock instance.")
    _powershell(
        "Add-AppxPackage -ForceApplicationShutdown -Register "
        + _ps_quote(str(folder / MANIFEST_NAME)),
        "Bedrock package registration",
    )
    for package in _packages(manifest[0]):
        if _same_path(package.get("InstallLocation") or "", str(folder)):
            return package
    raise BedrockError("Windows did not report the Bedrock package as registered.")


def install_release(release: BedrockRelease, instance_name: str) -> dict:
    _require_windows()
    return _install_gdk(release, instance_name) if release.kind == "gdk" \
        else _install_uwp(release, instance_name)


def is_game_running() -> bool:
    if not is_windows():
        return False
    output = subprocess.run(
        ["tasklist", "/FI", f"IMAGENAME eq {GAME_PROCESS}", "/NH"],
        capture_output=True, text=True, errors="replace", creationflags=CREATE_NO_WINDOW,
    ).stdout
    return GAME_PROCESS.lower() in output.lower()


def launch_release(release: BedrockRelease, instance_name: str) -> None:
    _require_windows()
    if is_game_running():
        raise BedrockError("Minecraft is already running. Close it first.")
    package = install_release(release, instance_name)
    folder = package.get("InstallLocation") or ""
    manifest = _read_manifest(folder) if folder else None
    app_id = (manifest[2] if manifest else "") or ("Game" if release.kind == "gdk" else "App")
    family = package.get("PackageFamilyName") or ""
    if not family:
        raise BedrockError("Windows did not return the installed Bedrock package family.")
    subprocess.Popen(["explorer.exe", f"shell:AppsFolder\\{family}!{app_id}"],
                     creationflags=CREATE_NO_WINDOW)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if is_game_running():
            update_instance_meta(instance_name, lambda meta: meta.__setitem__(
                "last_played", time.time()))
            return
        time.sleep(1)
    raise BedrockError(
        "Windows was asked to start Bedrock, but the game did not appear within 60 seconds. "
        "Check the Microsoft Store or Xbox app for sign-in or update prompts.")


def instance_has_package(instance_name: str) -> bool:
    meta = read_instance_meta(instance_name) or {}
    # Older metadata stored an absolute path. Look up its filename locally so a
    # moved instance never depends on its old launcher folder.
    package = Path(meta.get("bedrock_package") or "").name
    return (Path(package).suffix.lower() in (".msixvc", ".appx")
            and (bedrock_instance_download_dir(instance_name) / package).is_file())
