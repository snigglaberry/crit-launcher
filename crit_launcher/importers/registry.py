"""Scanner registry and import source resolution."""

from pathlib import Path

from ..core.logs import log_exception
from .common import LAUNCHER_LABELS, _launcher_roots
from .fastclient import FASTCLIENT_ROOT_SOURCE_ID, _scan_fastclient
from .lunar import _scan_lunar, lunar_game_dir, lunar_mods_dir
from .minecraft import _scan_minecraft
from .modrinth_app import _scan_modrinth
from .prism import _prism_game_dir, _scan_prismlauncher
from .sklauncher import _scan_sklauncher, _sk_game_dir
from .universal import (
    UNIVERSAL_SOURCE_ID,
    _same_path,
    fill_instance_details,
    game_dir_for,
    scan_universal,
    scan_universal_known_roots,
    scan_universal_source,
)


_LAUNCHER_SCANNERS = {
    "prismlauncher": _scan_prismlauncher,
    "polymc": _scan_prismlauncher,
    "sklauncher": _scan_sklauncher,
    "fastclient": _scan_fastclient,
    "modrinth": _scan_modrinth,
    "minecraft": _scan_minecraft,
    "lunar": _scan_lunar,
    "universal": scan_universal,
}

# Launchers whose scanner can be used to probe an unknown folder.
_PROBE_LAUNCHERS = ("prismlauncher", "polymc", "sklauncher", "fastclient", "modrinth")


def _enrich_instances(launcher: str, root: Path, instances: list):
    for inst in instances:
        try:
            game_dir = _instance_game_dir(launcher, inst.get("source_id"), root)
            if game_dir:
                fill_instance_details(inst, game_dir)
        except Exception:
            log_exception(f"detecting {launcher} instance details")
    return instances


def scan_all_launchers() -> list:
    sources = []
    for launcher, scanner in _LAUNCHER_SCANNERS.items():
        for root in _launcher_roots(launcher):
            if not root.is_dir():
                continue
            try:
                instances = scanner(root)
            except Exception:
                log_exception(f"scanning {launcher} at {root}")
                continue
            if instances:
                _enrich_instances(launcher, root, instances)
                sources.append({
                    "launcher": launcher, "label": LAUNCHER_LABELS[launcher],
                    "root": str(root), "instances": instances,
                })
            break
    seen = set()
    for source in sources:
        try:
            seen.add(Path(source["root"]).resolve())
        except OSError:
            pass
    for source in scan_universal_known_roots():
        try:
            if Path(source["root"]).resolve() in seen:
                continue
        except OSError:
            pass
        sources.append(source)
    return sources


def _guess_launcher_for_dir(path: Path):
    path = Path(path)
    name = path.name.lower()
    for key, hints in (("lunar", ("lunarclient", ".lunarclient", "lunar client")),
                       ("modrinth", ("modrinth", "theseus")), ("sklauncher", ("sklauncher",)),
                       ("fastclient", ("fastclient", "fast client")), ("polymc", ("polymc",)),
                       ("prismlauncher", ("prism",)),
                       ("minecraft", (".minecraft",))):
        if any(h in name for h in hints):
            return key
    if (path / "profiles").is_dir() and (path / "settings" / "launcher.json").is_file():
        return "lunar"
    if (path / ".lunarclient").is_dir():
        return "lunar"
    for key in _PROBE_LAUNCHERS:
        scanner = _LAUNCHER_SCANNERS.get(key)
        if not scanner:
            continue
        try:
            if scanner(path):
                return key
        except Exception:
            continue
    if (path / "launcher_profiles.json").is_file() or name in (".minecraft", "minecraft"):
        return "minecraft"
    return None


def scan_custom_launcher_dir(launcher: str, root: Path) -> dict:
    scanner = _LAUNCHER_SCANNERS.get(launcher)
    if not scanner:
        raise ValueError("Unknown launcher type.")
    instances = scanner(root)
    _enrich_instances(launcher, root, instances)
    return {"launcher": launcher, "label": LAUNCHER_LABELS[launcher], "root": str(root),
            "instances": instances, "custom": True}


def _lunar_root_for_game_dir(path: Path):
    """When a picked folder is Lunar's shared .minecraft, use its Lunar root."""
    for root in _launcher_roots("lunar"):
        if not root.is_dir():
            continue
        game_dir = lunar_game_dir(root)
        if game_dir and _same_path(game_dir, path):
            return root
    return None


def auto_import_source(root: Path):
    """Pick the best scanner for an arbitrary folder, falling back to universal."""
    root = Path(root)
    lunar_root = _lunar_root_for_game_dir(root)
    if lunar_root:
        try:
            source = scan_custom_launcher_dir("lunar", lunar_root)
        except Exception:
            source = None
        if source and source["instances"]:
            return source
    guess = _guess_launcher_for_dir(root)
    if guess:
        try:
            source = scan_custom_launcher_dir(guess, root)
        except Exception:
            source = None
        if source and source["instances"]:
            return source
    try:
        return scan_universal_source(root)
    except Exception:
        log_exception("auto_import_source")
        return None


def _find_scanned_instance(launcher: str, source_id: str, custom_root=None):
    if custom_root:
        root = Path(custom_root)
        scanner = _LAUNCHER_SCANNERS.get(launcher)
        if not scanner or not root.is_dir():
            return None, None
        for inst in scanner(root):
            if inst["source_id"] == source_id:
                game_dir = _instance_game_dir(launcher, source_id, root)
                if game_dir:
                    fill_instance_details(inst, game_dir)
                return inst, root
        return None, None
    for root in _launcher_roots(launcher):
        if not root.is_dir():
            continue
        scanner = _LAUNCHER_SCANNERS.get(launcher)
        if not scanner:
            return None, None
        for inst in scanner(root):
            if inst["source_id"] == source_id:
                game_dir = _instance_game_dir(launcher, source_id, root)
                if game_dir:
                    fill_instance_details(inst, game_dir)
                return inst, root
        return None, None
    return None, None


def _instance_game_dir(launcher: str, source_id: str, root: Path):
    if launcher in ("prismlauncher", "polymc"):
        return _prism_game_dir(root / "instances" / source_id)
    if launcher == "sklauncher":
        return _sk_game_dir(root / "instances" / source_id) or (root / "instances" / source_id)
    if launcher == "modrinth":
        return root / "profiles" / source_id
    if launcher == "fastclient":
        return root if source_id == FASTCLIENT_ROOT_SOURCE_ID else root / "profiles" / source_id
    if launcher == "minecraft":
        return game_dir_for(root)
    if launcher == "lunar":
        return lunar_game_dir(root)
    if launcher == "universal":
        base = root if source_id == UNIVERSAL_SOURCE_ID else root / source_id
        return game_dir_for(base)
    return None


def _source_game_dir(launcher: str, source_id: str, root: Path):
    if launcher == "lunar":
        return lunar_game_dir(root)
    return _instance_game_dir(launcher, source_id, root)


def _source_paths(launcher: str, source_id: str, root: Path):
    game_dir = _source_game_dir(launcher, source_id, root)
    if game_dir is None:
        return None, None, None, None, None, None, None, None
    mods_dir = game_dir / "mods"
    if launcher == "lunar":
        mods_dir = lunar_mods_dir(root, source_id)
    return (game_dir / "options.txt", mods_dir,
            game_dir / "resourcepacks", game_dir / "saves", game_dir / "config",
            game_dir / "shaderpacks", game_dir / "schematics", game_dir / "screenshots")
