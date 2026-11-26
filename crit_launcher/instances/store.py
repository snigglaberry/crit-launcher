"""Instance folders and instance.json metadata.

Java instances live in ``instances/``. Bedrock instances are kept in their own
``bedrock/instances/`` tree so the two games never share files, while the rest of the
launcher still talks about both through the helpers in this module.
"""

import json
import os
import re
import threading
from pathlib import Path

from ..config.paths import BEDROCK_INSTANCES_DIR, INSTANCES_DIR
from ..minecraft.constants import INSTALL_SCHEMA
from ..minecraft.loaders import MOD_LOADERS


_INVALID_NAME_RE = re.compile(r'[<>:"/\\|?*\x00-\x1f]')
_meta_lock = threading.RLock()


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


def java_instance_dir(name: str) -> Path:
    """Folder for a Java Edition instance (what `instance_dir` used to always return)."""
    return INSTANCES_DIR / sanitize_instance_name(name)


def bedrock_instance_dir(name: str) -> Path:
    """Folder for a Bedrock instance, deliberately outside the Java instance tree."""
    return BEDROCK_INSTANCES_DIR / sanitize_instance_name(name)


def instance_dir(name: str) -> Path:
    """The folder an existing instance lives in, or the Java path for a new one."""
    safe = sanitize_instance_name(name)
    java = INSTANCES_DIR / safe
    if java.is_dir():
        return java
    bedrock = BEDROCK_INSTANCES_DIR / safe
    if bedrock.is_dir():
        return bedrock
    return java


def _meta_path(root: Path, name: str) -> Path:
    return root / sanitize_instance_name(name) / "instance.json"


def read_instance_meta(name: str) -> dict | None:
    for root in (INSTANCES_DIR, BEDROCK_INSTANCES_DIR):
        meta_path = _meta_path(root, name)
        if not meta_path.exists():
            continue
        try:
            return json.loads(meta_path.read_text(encoding="utf-8"))
        except Exception:
            return None
    return None


def write_instance_meta(name: str, meta: dict):
    root = BEDROCK_INSTANCES_DIR if meta.get("loader") == "bedrock" else INSTANCES_DIR
    path = _meta_path(root, name)
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


def _list_root(root: Path) -> list:
    result = []
    if not root.exists():
        return result
    for child in sorted(root.iterdir(), key=lambda p: p.name.lower()):
        if not child.is_dir():
            continue
        meta_path = child / "instance.json"
        if not meta_path.exists():
            continue
        try:
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        except Exception:
            continue
        meta.setdefault("name", child.name)
        result.append(meta)
    return result


def list_instances() -> list:
    merged = {}
    for meta in _list_root(INSTANCES_DIR) + _list_root(BEDROCK_INSTANCES_DIR):
        merged.setdefault(str(meta.get("name", "")), meta)
    result = list(merged.values())
    result.sort(key=lambda m: str(m.get("name", "")).lower())
    return result


def active_instance(instances, preferred):
    return next((i for i in instances if i["name"] == preferred), instances[0] if instances else None)


def instance_game_dir(name: str) -> Path:
    return instance_dir(name) / "game"


def bedrock_instance_download_dir(name: str) -> Path:
    return bedrock_instance_dir(name) / "download"


def instance_mods_dir(name: str) -> Path:
    return instance_game_dir(name) / "mods"


def instance_config_dir(name: str) -> Path:
    return instance_game_dir(name) / "config"


def ensure_game_layout(name: str, loader: str):
    if loader == "bedrock":
        bedrock_instance_download_dir(name).mkdir(parents=True, exist_ok=True)
        return
    game = instance_game_dir(name)
    folders = ["resourcepacks", "shaderpacks", "saves"]
    if loader in MOD_LOADERS:
        folders += ["mods", "config"]
    for f in folders:
        (game / f).mkdir(parents=True, exist_ok=True)


def unique_instance_name(base: str) -> str:
    cleaned = _INVALID_NAME_RE.sub("-", base or "").replace("..", "-").strip().rstrip(".")
    cleaned = (cleaned or "Modpack")[:56]
    candidate = cleaned
    n = 2
    while java_instance_dir(candidate).exists() or bedrock_instance_dir(candidate).exists():
        candidate = f"{cleaned} ({n})"
        n += 1
    return candidate
