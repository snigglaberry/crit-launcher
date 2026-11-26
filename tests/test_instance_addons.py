import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.parse import parse_qs, urlsplit
from zipfile import ZipFile

from crit_launcher.addons import api as addons, api_mods, curseforge
from crit_launcher.app.facade import API
from crit_launcher.instances import store


class InstanceAddonTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        roots = patch.multiple(store, INSTANCES_DIR=root / "instances", BEDROCK_INSTANCES_DIR=root / "bedrock")
        roots.start()
        self.addCleanup(roots.stop)
        self.api = API()

    def instance(self, loader):
        self.api._make_instance(loader, "1.20.1", loader, None)
        return store.instance_game_dir(loader)

    def test_toggle_round_trip_survives_restart(self):
        game = self.instance("fabric")
        mod = game / "mods" / "Example.JAR"
        with ZipFile(mod, "w") as jar:
            jar.writestr("fabric.mod.json", json.dumps({"name": "Example", "icon": "icon.png"}))
            jar.writestr("icon.png", b"\x89PNG\r\n\x1a\n")
        original = mod.read_bytes()
        self.assertTrue(self.api.toggle_instance_mod("fabric", mod.name, False)["success"])
        self.assertFalse(mod.exists())
        self.assertEqual((game / "disabled" / mod.name).read_bytes(), original)

        api = API()
        parked, = api.get_instance_content("fabric")["items"]
        self.assertFalse(parked["enabled"])
        self.assertEqual(parked["name"], "Example")
        self.assertTrue(api.get_content_icon("fabric", "mods", parked["file"])["icon"].startswith("data:image/png;"))
        self.assertTrue(api.toggle_instance_mod("fabric", parked["file"], True)["success"])
        self.assertEqual(mod.read_bytes(), original)
        self.assertFalse((game / "disabled" / mod.name).exists())
        active, = api.get_instance_content("fabric")["items"]
        self.assertTrue(active["enabled"])
        self.assertEqual(active["file"], mod.name)

    def test_legacy_mod_and_conflicting_filenames(self):
        game = self.instance("forge")
        active = game / "mods" / "example.jar"
        legacy = active.with_suffix(".jar.disabled")
        legacy.write_bytes(b"original")
        item, = self.api.get_instance_content("forge")["items"]
        self.assertFalse(item["enabled"])
        self.assertTrue(self.api.toggle_instance_mod("forge", item["file"], True)["success"])
        self.assertEqual(active.read_bytes(), b"original")
        key = self.api.toggle_instance_mod("forge", active.name, False)["file"]
        active.write_bytes(b"second install")

        self.assertFalse(self.api.toggle_instance_mod("forge", key, True)["success"])
        self.assertFalse(self.api.toggle_instance_mod("forge", active.name, False)["success"])
        self.assertEqual((game / "disabled" / active.name).read_bytes(), b"original")
        self.assertTrue(self.api.delete_instance_content("forge", "mods", key)["success"])
        self.assertEqual(active.read_bytes(), b"second install")

    def test_toggle_requires_an_idle_instance_and_a_mod_file(self):
        game = self.instance("fabric")
        mod = game / "mods" / "example.jar"
        mod.touch()
        self.api._processes["fabric"] = object()
        self.assertFalse(self.api.toggle_instance_mod("fabric", mod.name, False)["success"])
        self.api._processes.clear()
        self.api._installing.add("fabric")
        self.assertFalse(self.api.toggle_instance_mod("fabric", mod.name, False)["success"])
        self.api._installing.clear()
        for name in ("../mods/example.jar", "disabled/../mods/example.jar", "mods\\example.jar", "missing.jar"):
            with self.subTest(name=name):
                self.assertFalse(self.api.toggle_instance_mod("fabric", name, False)["success"])
        self.assertTrue(mod.is_file())

    def test_disabled_mod_can_be_identified_and_updated_in_place(self):
        game = self.instance("fabric")
        mod = game / "mods" / "example-1.jar"
        mod.write_bytes(b"version 1")
        key = self.api.toggle_instance_mod("fabric", mod.name, False)["file"]
        found = {hashlib.sha1(b"version 1").hexdigest(): {"id": "v1", "project_id": "example"}}
        with patch.object(api_mods, "http_post_json", return_value=found), \
                patch.object(api_mods, "http_get_json", return_value=[{"id": "example"}]):
            identified = self.api.identify_mods("fabric")["mods"][key]
        version = {"project_id": "example", "game_versions": ["1.20.1"], "loaders": ["fabric"],
                   "files": [{"filename": "example-2.jar", "url": "example/v2"}]}
        with patch.object(api_mods, "http_get_json", return_value=version), \
                patch.object(api_mods, "http_download", side_effect=lambda url, path, *_: path.write_bytes(b"version 2")):
            r = self.api.switch_mod_version("fabric", key, identified["project_id"], "v2")
        self.assertTrue(r["success"], r)
        self.assertEqual(list((game / "mods").iterdir()), [])
        self.assertFalse((game / "disabled" / mod.name).exists())
        self.assertEqual((game / "disabled" / "example-2.jar").read_bytes(), b"version 2")
        item, = self.api.get_instance_content("fabric")["items"]
        self.assertEqual(item["file"], r["file"])
        self.assertFalse(item["enabled"])

    def test_shader_targets_include_unmodded_java_instances(self):
        for loader in ("vanilla", "fabric", "forge", "bedrock"):
            self.instance(loader)
        targets = self.api.get_addon_targets("shader")["instances"]
        self.assertEqual({i["name"] for i in targets}, {"vanilla", "fabric", "forge"})
        self.assertNotIn("vanilla", [i["name"] for i in self.api.get_addon_targets("mod")["instances"]])
        for project in ("shader", "cf:42"):
            self.assertFalse(self.api.install_addon(project, "shader", "bedrock")["success"])
        self.assertFalse((store.bedrock_instance_dir("bedrock") / "game").exists())

    def test_shader_install_downloads_only_the_pack_from_either_source(self):
        game = self.instance("vanilla")

        def download(url, path, *_):
            with ZipFile(path, "w") as pack:
                pack.writestr("shaders/shaders.properties", "")

        version = {"version_type": "release", "loaders": ["optifine"],
                   "files": [{"filename": "modrinth.zip", "url": "example/pack"}]}
        with patch.object(addons, "http_get_json", return_value=[version]) as get, \
                patch.object(addons, "http_download", side_effect=download):
            self.assertTrue(self.api.install_addon("shader", "shader", "vanilla")["success"])
            self.assertEqual(parse_qs(urlsplit(get.call_args.args[0]).query), {"game_versions": ['["1.20.1"]']})

        cf_file = {"id": 12, "gameVersions": ["1.20.1"], "fileName": "curseforge.zip",
                   "downloadUrl": "https://example.com/pack"}
        with patch.object(curseforge, "_cf_get", return_value=[cf_file]), \
                patch.object(curseforge, "http_download", side_effect=download), \
                patch.object(curseforge, "_cf_dependencies") as deps:
            self.assertTrue(self.api.install_addon("cf:42", "shader", "vanilla")["success"])
            deps.assert_not_called()
        items = self.api.get_instance_content("vanilla", "shaderpacks")["items"]
        self.assertEqual({i["file"] for i in items}, {"modrinth.zip", "curseforge.zip"})
        self.assertFalse((game / "mods").exists())
