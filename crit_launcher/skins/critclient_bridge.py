"""Crit Client's per-instance, per-offline-account Skin Studio file format."""

import base64
import json
import os
import tempfile
import uuid
from pathlib import Path

from ..core.images import decode_png_uri, skin_png_size
from ..instances.store import instance_config_dir


MAX_JSON_BYTES = 280_000


def _skin_path(instance_name, account_uuid):
    return instance_config_dir(instance_name) / "skinstudio" / "offline-skins" / f"{uuid.UUID(str(account_uuid))}.json"


def skin_variant(png, variant):
    if variant not in ("classic", "slim"):
        raise ValueError("Skin model must be classic or slim.")
    _, height = skin_png_size(png)
    return "classic" if height == 32 else variant


def read_local_skin(instance_name, account_uuid):
    """Return PNG bytes and model, a reset record, or None for an unreadable selection."""
    try:
        with _skin_path(instance_name, account_uuid).open("rb") as f:
            raw = f.read(MAX_JSON_BYTES + 1)
        if len(raw) > MAX_JSON_BYTES:
            return None
        data = json.loads(raw.decode("utf-8"))
        if not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1:
            return None
        encoded = data["png"]
        if encoded is None:
            return {"png": None, "variant": None} if data.get("variant") in (None, "classic", "slim") else None
        if not isinstance(encoded, str):
            return None
        png = base64.b64decode(encoded, validate=True)
        return {"png": png, "variant": skin_variant(png, data.get("variant"))}
    except (KeyError, OSError, ValueError, RecursionError):
        return None


def write_local_skin(instance_name, account_uuid, png, variant="classic"):
    path = _skin_path(instance_name, account_uuid)
    record = {"version": 1, "png": None}
    if png is not None:
        if isinstance(png, str):
            png = decode_png_uri(png)
        record["variant"] = skin_variant(png, variant)
        record["png"] = base64.b64encode(png).decode("ascii")
    raw = json.dumps(record, separators=(",", ":")).encode("utf-8")
    if len(raw) > MAX_JSON_BYTES:
        raise ValueError("Skin selection is too large.")
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.stem + ".", suffix=".tmp", delete=False)
    try:
        with tmp:
            tmp.write(raw)
        os.replace(tmp.name, path)
    finally:
        Path(tmp.name).unlink(missing_ok=True)
