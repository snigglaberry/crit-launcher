"""Sign or verify release assets in one directory, using the launcher's update format."""

import argparse
import base64
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from crit_launcher.core.fsutil import _write_bytes_atomic, file_sha256
from crit_launcher.updates import signature


def sign_assets(tag, assets, key_path):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError("Expected an Ed25519 private key.")
    sums = "".join(f"{file_sha256(p)}  {p.name}\n" for p in assets).encode("utf-8")
    sig = base64.b64encode(key.sign(signature.release_message(tag, sums))) + b"\n"
    # Reject a private key that doesn't match the one shipped in the launcher.
    signature.signed_asset_sha256(tag, sums, sig, assets[0].name)
    folder = assets[0].parent
    _write_bytes_atomic(folder / signature.UPDATE_SUMS, sums)
    _write_bytes_atomic(folder / signature.UPDATE_SIG, sig)


def verify_assets(tag, assets):
    folder = assets[0].parent
    sums = (folder / signature.UPDATE_SUMS).read_bytes()
    sig = (folder / signature.UPDATE_SIG).read_bytes()
    for path in assets:
        if file_sha256(path) != signature.signed_asset_sha256(tag, sums, sig, path.name):
            raise ValueError(f"Checksum mismatch for {path.name}.")
        print(f"Verified {path.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    sign = commands.add_parser("sign", help="Write SHA256SUMS and SHA256SUMS.sig beside the assets.")
    sign.add_argument("--key", type=Path, required=True, help="Path to the private PKCS#8 PEM file.")
    for sub in (sign, commands.add_parser("verify", help="Check the signature and every listed binary.")):
        sub.add_argument("--tag", required=True, help="Exact GitHub release tag, e.g. v1.9.")
        sub.add_argument("assets", nargs="+", type=Path)
    args = parser.parse_args()
    assets = sorted((p.resolve() for p in args.assets), key=lambda p: p.name)
    if len({p.parent for p in assets}) != 1 or len(set(assets)) != len(assets):
        parser.error("Supply distinct release binaries from one directory.")
    if any(p.suffix.lower() not in (".exe", ".appimage") or "\n" in p.name or "\r" in p.name for p in assets):
        parser.error("Expected .exe or .AppImage release asset names.")
    try:
        if args.command == "sign":
            sign_assets(args.tag, assets, args.key)
        verify_assets(args.tag, assets)
    except (OSError, ValueError, RuntimeError, ImportError) as exc:
        parser.exit(1, f"{exc}\n")


if __name__ == "__main__":
    main()
