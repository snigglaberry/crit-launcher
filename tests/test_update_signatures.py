import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from crit_launcher.core.http import http_download
from crit_launcher.updates import api_changelog, signature


# Public test vector for the manifest below; the test key is not trusted by the launcher.
TEST_KEY = "YOkTv9SfIfhXnpAHGMxTWVWDwkli+9YhlM6E4tL/ApY="
TEST_SIG = b"mfOlV+4xc8HdRaz0IZJKKr6FezPJ81LObsjWelP6iedUUlrQdCzsNKPWjYMIJj5m7FqKoZqxxkOG6zS2ZW5pCw==\n"


class UpdateSignatureTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.digest = hashlib.sha256(b"test installer").hexdigest()
        self.sums = (self.digest + "  setup.exe\n").encode()
        self.files = {"SHA256SUMS": self.sums, "SHA256SUMS.sig": TEST_SIG, "setup.exe": b"test installer"}
        for name, data in self.files.items():
            (self.root / name).write_bytes(data)
        self.release = {"tag_name": "v1-test", "assets": [
            {"name": name, "browser_download_url": (self.root / name).as_uri()} for name in self.files]}

    def test_changelog_result_verifies_and_a_replaced_installer_is_rejected(self):
        with patch.object(api_changelog, "http_get_json", return_value=self.release), \
                patch.object(api_changelog, "CHANGELOG_CACHE", self.root / "cache.json"), \
                patch.object(api_changelog, "_changelog_mem", {}):
            release = api_changelog.ChangelogApi().get_changelog(force=True)
        with patch.object(signature, "UPDATE_TRUSTED_KEYS", (TEST_KEY,)):
            expected = signature.update_signed_sha256(release, "setup.exe")
        self.assertEqual(expected, self.digest)
        installed = self.root / "update/setup.exe"
        http_download(release["setup_url"], installed, expected_sha1=expected)
        (self.root / "setup.exe").write_bytes(b"changed installer")
        with self.assertRaisesRegex(OSError, "Checksum mismatch"):
            http_download(release["setup_url"], installed, expected_sha1=expected)
        self.assertEqual(installed.read_bytes(), b"test installer")

    def test_rejects_wrong_key_tag_manifest_or_unlisted_asset(self):
        with self.assertRaises(RuntimeError):
            signature.update_signed_sha256(self.release, "setup.exe")
        with patch.object(signature, "UPDATE_TRUSTED_KEYS", (TEST_KEY,)):
            for tag, sums, name in (("v2-test", self.sums, "setup.exe"), (None, self.sums, "setup.exe"),
                                    ("v1-test", self.sums + b"\n", "setup.exe"), ("v1-test", self.sums, "crit.exe")):
                with self.subTest(tag=tag, name=name), self.assertRaises(RuntimeError):
                    signature.signed_asset_sha256(tag, sums, TEST_SIG, name)
