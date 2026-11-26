"""Launcher-side Skin Studio protocol, using real instance files but no game or network."""

import base64
import io
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import uuid
import zlib
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import call, patch
from zipfile import ZipFile

from crit_launcher.accounts.store import accounts_store
from crit_launcher.app.facade import API
from crit_launcher.config.settings import settings_store
from crit_launcher.core.images import PNG_SIG, png_uri
from crit_launcher.instances import store
from crit_launcher.launch import command
from crit_launcher.skins import api as skins, api_local, critclient_bridge as bridge
from crit_launcher.skins.custom_skin_loader import CSL_PROJECT, CSL_RUN_ONCE_MSG


def skin_png(height=64, color=0, width=64):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    rows = (b"\0" + bytes([color]) * (width * 3)) * height
    return PNG_SIG + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def jar(path, mod_id="critclient", marker=1):
    path.parent.mkdir(parents=True, exist_ok=True)
    manifest = {"id": mod_id, "version": "0.1"}  # version and filename do not determine bridge support
    if marker is not None:
        manifest["custom"] = {"critclient:launcher_skin_bridge": marker}
    with ZipFile(path, "w") as z:
        z.writestr("fabric.mod.json", json.dumps(manifest))
    return path


class OfflineSkinTests(unittest.TestCase):
    def setUp(self):
        stack = ExitStack()
        self.addCleanup(stack.close)
        self.enter = stack.enter_context
        root = Path(self.enter(tempfile.TemporaryDirectory()))
        self.enter(patch.multiple(store, INSTANCES_DIR=root / "instances", BEDROCK_INSTANCES_DIR=root / "bedrock"))
        self.cache = root / "skins"
        self.cache.mkdir()
        self.enter(patch.object(api_local, "SKINS_DIR", self.cache))
        self.alice = {"name": "Alice", "uuid": uuid.uuid4().hex, "type": "offline"}
        self.bob = {"name": "Bob", "uuid": str(uuid.uuid4()), "type": "offline"}
        self.settings = {"active_account": self.alice["uuid"]}
        self.read_accounts = self.enter(patch.object(accounts_store, "read", return_value=[self.alice, self.bob]))
        self.enter(patch.object(settings_store, "read", side_effect=self.settings.copy))
        self.enter(patch.object(settings_store, "update", side_effect=lambda update: update(self.settings)))
        self.api = API()
        self.lookup = self.enter(patch.object(self.api, "_find_version", return_value=None))
        self.download = self.enter(patch.object(self.api, "_download_version_file"))

    def instance(self, name, loader="fabric", version="1.21.1", **meta):
        store.write_instance_meta(name, dict(meta, name=name, loader=loader, version=version))
        return store.instance_game_dir(name)

    def crit(self, name, marker=1):
        game = self.instance(name)
        jar(game / "mods/renamed.jar", marker=marker)
        return game

    def record(self, name, account):
        return (store.instance_game_dir(name) / "config/skinstudio/offline-skins" /
                f"{uuid.UUID(account['uuid'])}.json")

    def test_enabled_jar_identity_and_exact_csl_install_candidates(self):
        self.crit("Ready")
        for name, marker in (("Old", None), ("String", "1"), ("Bool", True), ("Float", 1.0)):
            self.crit(name, marker)
        other = self.instance("Badged", critclient=True)
        jar(other / "mods/crit-client-1.21.1.jar", "unrelated")
        jar(other / "mods/crit.jar.disabled")
        jar(other / "disabled/crit.jar")
        for loader in ("vanilla", "bedrock", "horizon", "forge", "neoforge"):
            self.instance(loader, loader)
        self.instance("Unsupported version", "fabric", "no-build")
        self.lookup.side_effect = lambda project, loader, mc: {"id": "exact"} if mc == "1.21.1" else None

        plan = self.api.local_skin_plan()
        self.assertEqual(plan["installed"], ["Ready"])
        self.assertEqual({i["name"] for i in plan["installable"]}, {"Badged", "forge", "neoforge"})
        self.assertEqual({i["name"] for i in plan["blocked"]}, {"Old", "String", "Bool", "Float"})
        for item in plan["blocked"]:
            self.assertIn("Update Crit Client to use launcher skins", item["error"])
        self.assertCountEqual(self.lookup.call_args_list, [call(CSL_PROJECT, l, "1.21.1") for l in
                              ("fabric", "forge", "neoforge")] + [call(CSL_PROJECT, "fabric", "no-build")])
        targets = {i["name"]: i for i in self.api.local_skin_targets()["instances"]}
        self.assertTrue(targets["Ready"]["installed"])
        self.assertFalse(targets["Ready"]["csl"])
        self.assertEqual(targets["Ready"]["backend"], "critclient")
        self.assertEqual(targets["Badged"]["backend"], "csl")
        self.lookup.side_effect = OSError("offline")
        self.assertEqual(self.api.local_skin_plan()["installable"], [])

    def test_no_csl_setup_or_capes_for_any_enabled_crit_instance(self):
        self.crit("Ready")
        self.crit("Old", None)
        game = self.crit("Conflict")
        csl = jar(game / "mods/also-renamed.jar", CSL_PROJECT)
        before = {p: p.read_bytes() for p in (game / "mods").iterdir()}
        names = ["Ready", "Old", "Conflict"]
        ensure = self.enter(patch.object(self.api, "_csl_ensure"))
        config = self.enter(patch.object(api_local, "_csl_find_config"))
        cape_fetch = self.enter(patch.object(api_local.urllib.request, "urlopen"))
        for name in names:
            self.assertFalse(self.api.local_install_csl(name)["success"])
        result = self.api.local_set_skin(png_uri(skin_png()), names, "slim")
        by_name = {r["name"]: r for r in result["results"]}
        self.assertEqual(by_name["Ready"], {"name": "Ready", "ok": True, "downloaded": False})
        self.assertIn("Update Crit Client", by_name["Old"]["error"])
        self.assertIn("View addons → Mods", by_name["Conflict"]["error"])
        self.assertIn("both enabled", self.api.local_skin_state("Conflict")["error"])
        self.assertFalse(self.record("Conflict", self.alice).exists())
        self.assertEqual({p: p.read_bytes() for p in (game / "mods").iterdir()}, before)
        self.assertEqual(self.api.local_skin_plan("cape")["total"], 0)
        self.assertEqual(self.api.local_skin_targets("cape")["instances"], [])
        self.assertEqual(self.api.local_set_cape("migrator", names)["results"], [])
        cape_fetch.assert_not_called()
        self.assertTrue(self.api.toggle_instance_mod("Conflict", csl.name, False)["success"])
        self.assertIn("Conflict", self.api.local_skin_plan()["installed"])
        ensure.assert_not_called()
        config.assert_not_called()
        self.lookup.assert_not_called()
        self.download.assert_not_called()

    def test_csl_setup_first_run_cape_and_reset(self):
        game = self.instance("Neo", "neoforge")
        self.crit("Crit")
        self.lookup.return_value = {"id": "neoforge-build"}
        def download(version, folder):
            with ZipFile(folder / "renamed.jar", "w") as z:
                z.writestr("META-INF/neoforge.mods.toml", '[[mods]]\nmodId="customskinloader"\n')
        self.download.side_effect = download
        png = skin_png()
        with patch.object(self.api, "_csl_ensure", wraps=self.api._csl_ensure) as ensure:
            r = self.api.local_set_skin(png_uri(png), ["Crit", "Neo"], "slim")
            self.assertEqual([c.args[0] for c in ensure.call_args_list], ["Neo"])
        self.lookup.assert_called_once_with(CSL_PROJECT, "neoforge", "1.21.1")
        neo = next(i for i in r["results"] if i["name"] == "Neo")
        self.assertTrue(neo["downloaded"])
        self.assertFalse(neo["ok"])
        self.assertIn(CSL_RUN_ONCE_MSG, neo["error"])
        self.assertTrue(self.record("Crit", self.alice).is_file())
        config = game / "CustomSkinLoader/CustomSkinLoader.json"
        self.assertFalse(config.exists())
        config.parent.mkdir()
        config.write_text("{}", encoding="utf-8")
        local = config.parent / "LocalSkin"
        self.assertTrue(all(i["ok"] for i in self.api.local_set_skin(png_uri(png))["results"]))
        self.assertEqual((local / "skins/Alice.png").read_bytes(), png)
        self.assertIsNone(self.api.local_skin_state("Neo")["variant"])
        self.assertEqual(self.api.local_skin_plan("cape")["installed"], ["Neo"])
        with patch.object(api_local.urllib.request, "urlopen", return_value=io.BytesIO(png)):
            self.assertEqual(self.api.local_set_cape("migrator", ["Crit"])["results"],
                             [{"name": "Neo", "ok": True, "downloaded": False}])
        self.assertEqual(self.api.local_skin_state("Neo")["cape_id"], "migrator")
        self.assertEqual(self.api.local_skin_state("Crit")["capes"], [])
        (self.cache / "Alice.png").write_bytes(png)
        self.assertTrue(self.api.local_reset()["success"])
        self.assertFalse((self.cache / "Alice.png").exists())
        self.assertFalse((local / "skins/Alice.png").exists())
        self.assertFalse((local / "capes/Alice.png").exists())
        self.assertFalse((local / ".crit_cape").exists())
        self.assertIsNone(self.api.local_skin_state("Neo")["skin"])

    def test_two_accounts_two_instances_client_edits_and_explicit_reset(self):
        game = self.crit("A")
        self.crit("B")
        first, second = skin_png(), skin_png(color=255)
        uri = png_uri(first)
        self.assertTrue(all(r["ok"] for r in self.api.local_set_skin(uri, [], "slim")["results"]))
        initial = {"version": 1, "variant": "slim", "png": base64.b64encode(first).decode("ascii")}
        a, b = self.record("A", self.alice), self.record("B", self.alice)
        for path in (a, b):
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), initial)
        self.settings["active_account"] = self.bob["uuid"]
        self.assertIsNone(self.api.local_skin_state("A")["skin"])
        self.api.local_set_skin(png_uri(second))
        self.assertEqual(json.loads(a.read_text()), initial)

        self.settings.update(active_account="missing fallback to Alice", active_instance="B")
        a.write_text(json.dumps(dict(initial, variant="classic", clientKey="client-owned")), encoding="utf-8")
        self.assertEqual(self.api.local_skin_state()["variant"], "slim")
        self.assertEqual(self.api.local_skin_state("A")["variant"], "classic")
        self.settings["active_instance"] = "deleted instance"
        self.assertEqual(self.api.local_skin_state()["variant"], "classic")
        self.assertFalse(self.api.local_skin_state("not a real instance")["success"])
        remote = self.enter(patch.object(skins, "ely_skin_for"))
        live = self.api.get_live_skins(["Alice"], "Alice")["active"]
        self.assertEqual((live["image"], live["variant"]), (uri, "classic"))
        csl_old = game / "CustomSkinLoader/LocalSkin/skins/Alice.png"
        csl_old.parent.mkdir(parents=True)
        csl_old.write_bytes(second)
        (self.cache / "Alice.png").write_bytes(second)
        a.unlink()
        self.assertIsNone(self.api.local_skin_state("A")["skin"])
        a.write_text("damaged", encoding="utf-8")
        self.assertIsNone(self.api.local_skin_state("A")["skin"])
        self.assertEqual(a.read_text(), "damaged")
        self.assertEqual(self.api.local_skin_state("B")["skin"], uri)
        self.assertTrue(self.api.local_reset()["success"])
        for name in ("A", "B"):
            self.assertEqual(json.loads(self.record(name, self.alice).read_text()), {"version": 1, "png": None})
            self.assertEqual(bridge.read_local_skin(name, self.bob["uuid"]), {"png": second, "variant": "classic"})
        self.assertEqual(bridge.read_local_skin("A", self.alice["uuid"]), {"png": None, "variant": None})
        live = self.api.get_live_skins(["Alice"], "Alice")
        self.assertIsNone(live["active"]["image"])
        self.assertIsNone(live["faces"]["Alice"])
        remote.assert_not_called()

    def test_ely_lookup_does_not_reselect_offline_account(self):
        self.crit("A")
        self.crit("B")
        def switch_account(source):
            self.assertEqual(source, "ElyPlayer")
            self.settings["active_account"] = self.bob["uuid"]
            return {"image": png_uri(skin_png()), "variant": "slim"}
        with patch.object(api_local, "ely_skin_for", side_effect=switch_account):
            self.assertTrue(self.api.local_use_ely("ElyPlayer")["success"])
        self.read_accounts.assert_called_once()
        for name in ("A", "B"):
            self.assertEqual(bridge.read_local_skin(name, self.alice["uuid"])["variant"], "slim")
            self.assertFalse(self.record(name, self.bob).exists())

    def test_crit_uses_stored_uuid_and_only_csl_uses_username_as_a_path(self):
        self.crit("Crit")
        self.instance("Forge", "forge")
        self.alice["name"] = "../not-a-filename"
        with patch.object(self.api, "_csl_ensure") as ensure:
            result = self.api.local_set_skin(png_uri(skin_png()), ["Forge"], "slim")
            by_name = {r["name"]: r for r in result["results"]}
            self.assertTrue(by_name["Crit"]["ok"])
            self.assertIn("username", by_name["Forge"]["error"])
            ensure.assert_not_called()
        self.assertEqual(self.api.local_skin_state("Crit")["variant"], "slim")
        for account_type in ("microsoft", "ely"):
            self.alice["type"] = account_type
            self.assertFalse(self.api.local_reset()["success"])
            self.assertFalse(self.api.local_set_skin(png_uri(skin_png()))["success"])
            self.assertEqual(bridge.read_local_skin("Crit", self.alice["uuid"])["variant"], "slim")

    def test_rejects_invalid_records_and_normalizes_legacy_models(self):
        self.crit("A")
        uid = self.alice["uuid"]
        self.assertIsNone(bridge.read_local_skin("A", uid))
        png = skin_png(height=32)
        self.assertTrue(self.api.local_set_skin(png_uri(png), [], "slim")["success"])
        path = self.record("A", self.alice)
        initial = json.loads(path.read_text())
        self.assertEqual(initial["variant"], "classic")
        path.write_text(json.dumps(dict(initial, variant="slim")), encoding="utf-8")
        self.assertEqual(bridge.read_local_skin("A", uid), {"png": png, "variant": "classic"})
        for image, model in ((png_uri(png), "auto"), ("not a URI", "classic"), (png_uri(skin_png(width=63)), "slim"),
                             (png_uri(png[:-4]), "classic"), (png_uri(png + bytes(200_001 - len(png))), "classic")):
            with self.subTest(model=model, length=len(image)):
                self.assertFalse(self.api.local_set_skin(image, [], model)["success"])
        self.assertEqual(bridge.read_local_skin("A", uid)["png"], png)
        for changes in ({"version": 2}, {"version": True}, {"version": 1.0}, {"variant": "auto"}, {"png": 5},
                        {"png": png_uri(png)}, {"png": "invalid base64!"}, {"png": "AA=="},
                        {"png": png_uri(skin_png(16)).split(",")[1]}, {"png": png_uri(png + bytes(200_000)).split(",")[1]}):
            with self.subTest(fields=list(changes)):
                path.write_text(json.dumps(dict(initial, **changes)), encoding="utf-8")
                self.assertIsNone(bridge.read_local_skin("A", uid))
        for raw in (b"[]", b'{"version":1}', b"\xff", b" " * 280_000 + b'{"version":1,"png":null}'):
            path.write_bytes(raw)
            self.assertIsNone(bridge.read_local_skin("A", uid))
        with self.assertRaises(ValueError):
            bridge.write_local_skin("A", "../Alice", png)

    def test_atomic_writes_use_unique_siblings_and_leave_old_file_on_failure(self):
        self.crit("A")
        uid, png = self.alice["uuid"], skin_png()
        dest = self.record("A", self.alice)
        with patch.object(bridge.os, "replace", wraps=os.replace) as replace:
            bridge.write_local_skin("A", uid, png)
            bridge.write_local_skin("A", uid, png, "slim")
        sources = [Path(c.args[0]) for c in replace.call_args_list]
        self.assertEqual(len(set(sources)), 2)
        self.assertEqual({p.parent for p in sources}, {dest.parent})
        with patch.object(bridge.os, "replace", side_effect=PermissionError), self.assertRaises(PermissionError):
            bridge.write_local_skin("A", uid, None)
        self.assertEqual(bridge.read_local_skin("A", uid), {"png": png, "variant": "slim"})
        self.assertEqual(list(dest.parent.iterdir()), [dest])

    def test_jvm_property_uses_launched_account_without_touching_identity_or_skin(self):
        game = self.crit("A")
        prefix = "-Dcritclient.launcher.accountType="
        meta = dict(store.read_instance_meta("A"), main_class="test.Main", classpath=[],
                    arguments={"jvm": [prefix + "old", "-Dkeep=true"]})
        options = {"jvm_args": prefix + "spoofed -ea " + prefix + "duplicate"}
        agent = self.enter(patch.object(command, "authlib_injector", return_value="injector.jar"))
        for t in ("microsoft", "ely", "offline"):
            with self.subTest(account=t):
                acc = dict(self.bob, type=t, mc_token="session-token")
                cmd = command.build_launch_command(meta, acc, 2048, "java", options)
                main = cmd.index("test.Main")
                self.assertEqual([a for a in cmd if a.startswith(prefix)], [prefix + t])
                self.assertEqual(cmd[main - 1], prefix + t)
                self.assertIn("-Dkeep=true", cmd[:main])
                self.assertIn("-ea", cmd[:main])
                self.assertEqual("-javaagent:injector.jar=ely.by" in cmd[:main], t == "ely")
                for flag, value in (("--gameDir", str(game)), ("--username", "Bob"),
                                    ("--uuid", acc["uuid"] if t == "offline" else acc["uuid"].replace("-", "")),
                                    ("--accessToken", "0" if t == "offline" else "session-token")):
                    self.assertGreater(cmd.index(flag), main)
                    self.assertEqual(cmd[cmd.index(flag) + 1], value)
        agent.assert_called_once()
        self.read_accounts.assert_not_called()
        self.assertFalse((game / "config/skinstudio").exists())
        for loader, marker in (("forge", 1), ("fabric", None), ("fabric", 0)):
            jar(game / "mods/renamed.jar", marker=marker)
            cmd = command.build_launch_command(dict(meta, loader=loader), self.bob, 2048, "java", options)
            self.assertFalse(any(a.startswith(prefix) for a in cmd))
        jar(game / "mods/renamed.jar")
        self.api.toggle_instance_mod("A", "renamed.jar", False)
        cmd = command.build_launch_command(meta, self.bob, 2048, "java", options)
        self.assertFalse(any(a.startswith(prefix) for a in cmd))


class SkinPageTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "Node is needed for headless page checks")
    def test_windows_and_linux_skin_workflows(self):
        subprocess.run([shutil.which("node"), str(Path(__file__).with_name("skin_bridge_pages.cjs"))], check=True)
