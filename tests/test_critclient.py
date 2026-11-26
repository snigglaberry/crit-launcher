import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from crit_launcher.addons import api_critclient, critclient
from crit_launcher.app.facade import API
from crit_launcher.instances import store


def write_jar(path, mod_id, version):
    with ZipFile(path, "w") as jar:
        jar.writestr("fabric.mod.json", json.dumps({"id": mod_id, "version": version}))
    return path.read_bytes()


class CritClientTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.mods = self.root / "mods"
        self.mods.mkdir()
        self.jar = self.mods / "crit-client-26.2.jar"
        self.latest = write_jar(self.root / self.jar.name, "critclient", "1.1")
        self.url = f"https://github.com/{critclient.CRIT_CLIENT_REPO}/releases/download/v2/{self.jar.name}"
        self.asset = critclient.ClientAsset(self.jar.name, self.url, len(self.latest),
                                           hashlib.sha256(self.latest).hexdigest())

    def test_latest_release_uses_minecraft_version_and_checksum(self):
        asset = {"name": self.asset.name, "size": self.asset.size,
                 "browser_download_url": self.url, "digest": "sha256:" + self.asset.sha256}
        response = {"tag_name": "v2", "assets": [
            dict(asset, browser_download_url="https://example.com/crit-client-26.2.jar"),
            dict(asset, name="source.zip"), asset,
        ]}
        with patch.object(critclient, "_release_cache", (0.0, {})), \
                patch.object(critclient, "http_get_json", return_value=response) as get:
            self.assertEqual(critclient.latest_client_assets(), {"26.2": self.asset})
            self.assertEqual(critclient.latest_client_assets(), {"26.2": self.asset})
            get.assert_called_once()

    def test_detects_same_name_and_size_update(self):
        self.assertEqual(critclient.client_jar_status(self.mods, self.asset), "missing")
        old = write_jar(self.jar, "critclient", "1.0")
        self.assertEqual(len(old), len(self.latest))
        self.assertEqual(critclient.client_jar_status(self.mods, self.asset), "outdated")
        self.jar.write_bytes(self.latest)
        self.assertEqual(critclient.client_jar_status(self.mods, self.asset), "current")

    def test_replaces_only_crit_client_and_disables_a_renamed_copy(self):
        write_jar(self.jar, "critclient", "1.0")
        old_jar = self.mods / "imported.jar"
        old = write_jar(old_jar, "critclient", "0.9")
        other = self.mods / "crit-client-1.21.11.jar"
        other_bytes = write_jar(other, "othermod", "1.0")
        with patch.object(critclient, "http_download", side_effect=self._download) as download:
            critclient.install_client_jar(self.mods, self.asset)
            download.assert_called_once()
        self.assertEqual(self.jar.read_bytes(), self.latest)
        self.assertEqual(other.read_bytes(), other_bytes)
        self.assertFalse(old_jar.exists())
        self.assertEqual(old_jar.with_suffix(".jar.disabled").read_bytes(), old)
        self.assertEqual(critclient.client_jar_status(self.mods, self.asset), "current")

    def test_invalid_download_leaves_old_jar_intact(self):
        old = write_jar(self.jar, "critclient", "1.0")
        with patch.object(critclient, "http_download", side_effect=lambda url, path, **kw: path.write_bytes(b"bad")):
            with self.assertRaises(OSError):
                critclient.install_client_jar(self.mods, self.asset)
        self.assertEqual(self.jar.read_bytes(), old)
        self.assertEqual(list(self.mods.iterdir()), [self.jar])

    def test_failed_replace_restores_renamed_client(self):
        old = self.mods / "old.jar"
        old_bytes = write_jar(old, "critclient", "1.0")
        with patch.object(critclient, "http_download", side_effect=self._download), \
                patch.object(critclient.os, "replace", side_effect=PermissionError):
            with self.assertRaises(PermissionError):
                critclient.install_client_jar(self.mods, self.asset)
        self.assertEqual(old.read_bytes(), old_bytes)
        self.assertEqual(list(self.mods.iterdir()), [old])

    def test_existing_instance_api_uses_jar_only(self):
        api = API()
        with patch.object(store, "INSTANCES_DIR", self.root / "instances"), \
                patch.object(store, "BEDROCK_INSTANCES_DIR", self.root / "bedrock"), \
                patch.object(api_critclient, "latest_client_assets", return_value={"26.2": self.asset}), \
                patch.object(critclient, "http_download", side_effect=self._download) as download, \
                patch.object(api, "_find_version") as lookup_mod, \
                patch.object(api, "emit") as emit, \
                patch.object(api_critclient.threading, "Thread") as worker:
            api._make_instance("Fabric", "26.2", "fabric", "0.19.3")
            api._make_instance("Vanilla", "26.2", "vanilla", None)
            api._make_instance("Other version", "1.21.11", "fabric", "0.18.1")
            self.assertEqual(api.get_critclient_info()["statuses"], {"Fabric": "missing"})
            self.assertFalse(api.install_critclient("Vanilla")["success"])
            api._processes["Fabric"] = object()
            self.assertFalse(api.install_critclient("Fabric")["success"])
            api._processes.clear()
            self.assertTrue(api.install_critclient("Fabric")["success"])
            worker.return_value.start.assert_called_once()
            self.assertFalse(api.install_critclient("Fabric")["success"])
            fn = worker.call_args.kwargs
            fn["target"](*fn["args"])
            download.assert_called_once()
            lookup_mod.assert_not_called()
            self.assertNotIn("Fabric", api._installing)
            self.assertTrue(emit.call_args.args[1]["success"])
            self.assertEqual(api.get_critclient_info()["statuses"], {"Fabric": "current"})

    def _download(self, url, path, **kwargs):
        self.assertEqual(url, self.url)
        self.assertEqual(kwargs.get("expected_sha1"), self.asset.sha256)
        path.write_bytes(self.latest)
