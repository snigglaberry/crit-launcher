"""Signature check for launcher updates."""

import base64
import binascii
import re

from ..core.ed25519 import horizon_ed25519_verify
from ..core.http import http_get_bytes


UPDATE_TRUSTED_KEYS = ("YJH854U8vf4kYLjtoVcbZcpiFvABV5SCNE7Ftnh0/Vg=",)
UPDATE_SUMS = "SHA256SUMS"
UPDATE_SIG = "SHA256SUMS.sig"


def update_signed_sha256(release: dict, asset_name: str) -> str:
    """sha256 for asset_name, but only from a signature-verified SHA256SUMS.

    This is the check that stops a hijacked release from running code: the sums
    list is only trusted when it carries a valid signature from a baked-in key.
    """
    assets = {a.get("name"): a for a in release.get("assets") or []}
    if UPDATE_SUMS not in assets or UPDATE_SIG not in assets:
        raise RuntimeError("This release isn't signed, so it wasn't installed.")
    sums = http_get_bytes(assets[UPDATE_SUMS]["browser_download_url"], limit=1 << 20)
    signature = http_get_bytes(assets[UPDATE_SIG]["browser_download_url"], limit=1 << 20)

    # GitHub calls this tag_name; the launcher's changelog and its cache call it tag.
    return signed_asset_sha256(release.get("tag_name") or release.get("tag"), sums, signature, asset_name)


def release_message(tag: str, sums: bytes) -> bytes:
    if not isinstance(tag, str) or not tag or any(c.isspace() for c in tag):
        raise RuntimeError("The release has no valid tag to verify.")
    return f"crit launcher release {tag}\n".encode("utf-8") + sums


def signed_asset_sha256(tag: str, sums: bytes, signature: bytes, asset_name: str) -> str:
    """Verify the exact manifest bytes before returning an asset's checksum."""
    message = release_message(tag, sums)
    keys = [base64.b64decode(k) for k in UPDATE_TRUSTED_KEYS]
    signed = False
    for line in signature.decode("utf-8", "replace").splitlines():
        try:
            raw = base64.b64decode(line.strip(), validate=True)
        except (binascii.Error, ValueError):
            continue
        if len(raw) == 64 and any(horizon_ed25519_verify(k, message, raw) for k in keys):
            signed = True
            break
    if not signed:
        raise RuntimeError("The update's signature didn't match, so it wasn't installed.")

    for line in sums.decode("utf-8", "replace").splitlines():
        parts = line.strip().split(None, 1)
        if (len(parts) == 2 and parts[1].lstrip("*") == asset_name
                and re.fullmatch(r"[0-9a-fA-F]{64}", parts[0])):
            return parts[0].lower()
    raise RuntimeError("The updater isn't in the signed checksums, so it wasn't installed.")
