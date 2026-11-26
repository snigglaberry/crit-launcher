"""JS-bridge API: instance management."""

import shutil
import sys
import threading
import time

from ..addons.critclient import CRIT_CLIENT_LOADER, latest_client_assets
from ..config.paths import BEDROCK_INSTANCES_DIR, INSTANCES_DIR, SERVERS_DIR
from ..config.settings import settings_store
from ..core.fsutil import _dir_size
from ..core.logs import log_exception
from ..core.system import open_in_file_manager
from ..horizon.constants import HORIZON_MC_VERSION
from ..minecraft.constants import INSTALL_SCHEMA
from ..minecraft.loaders import LOADER_NICE, MOD_LOADERS
from .store import (
    bedrock_instance_dir,
    ensure_game_layout,
    instance_dir,
    instance_game_dir,
    java_instance_dir,
    read_instance_meta,
    sanitize_instance_name,
    update_instance_meta,
    write_instance_meta,
)


class InstancesApi:
    """Creating, renaming, deleting and opening instances."""

    def _make_instance(self, name, version, loader, loader_version):
        meta = {
            "name": name,
            "version": version,
            "loader": loader,
            "loader_version": loader_version if loader in MOD_LOADERS else None,
            "installed": False,
            "schema": INSTALL_SCHEMA,
            "main_class": None,
            "asset_index": None,
            "arguments": None,
            "minecraft_arguments": None,
            "classpath": [],
            "playtime": 0,
            "icon": None,
            "created": time.time(),
        }
        write_instance_meta(name, meta)
        ensure_game_layout(name, loader)
        return meta

    def create_instance(self, version, loader, loader_version, instance_name, critclient=False):
        try:
            name = sanitize_instance_name(instance_name)
        except ValueError as exc:
            return {"success": False, "error": str(exc)}

        if instance_dir(name).exists():
            return {"success": False, "error": "Instance already exists."}
        if loader == "bedrock":
            if critclient:
                return {"success": False, "error": "Crit Client needs Fabric."}
            if not sys.platform.startswith("win"):
                return {"success": False,
                        "error": "Minecraft Bedrock can only be installed on Windows."}
            try:
                self._make_instance(name, "latest", "bedrock", None)
            except Exception as exc:
                log_exception("create_instance (bedrock)")
                return {"success": False, "error": f"Failed to create instance: {exc}"}
            self._start_bedrock(name)
            return {"success": True}
        if not version:
            return {"success": False, "error": "Select a Minecraft version."}
        if loader in MOD_LOADERS and not loader_version:
            return {"success": False, "error": f"Select a {LOADER_NICE[loader]} version."}

        if loader == "horizon":
            
            
            if critclient:
                return {"success": False, "error": "Crit Client needs Fabric."}
            if version != HORIZON_MC_VERSION:
                return {"success": False,
                        "error": f"Horizon only supports Minecraft {HORIZON_MC_VERSION}."}
            try:
                self._make_instance(name, HORIZON_MC_VERSION, "horizon", None)
            except Exception as exc:
                log_exception("create_instance (horizon)")
                return {"success": False, "error": f"Failed to create instance: {exc}"}
            with self._proc_lock:
                self._installing.add(name)
            threading.Thread(target=self._horizon_worker, args=(name,), daemon=True).start()
            self.push_state()
            return {"success": True}

        asset = None
        if critclient:
            if loader != CRIT_CLIENT_LOADER:
                return {"success": False, "error": "Crit Client needs Fabric."}
            try:
                asset = latest_client_assets().get(version)
            except Exception:
                log_exception("critclient lookup")
                return {"success": False, "error": "Couldn't reach the Crit Client server."}
            if not asset:
                return {"success": False,
                        "error": f"Crit Client isn't available for {loader.capitalize()} {version}."}

        try:
            self._make_instance(name, version, loader, loader_version)
            if asset:
                update_instance_meta(name, lambda m: m.__setitem__("critclient", True))
                with self._proc_lock:
                    self._installing.add(name)
                threading.Thread(target=self._critclient_worker, args=(name, version, loader, asset),
                                 daemon=True).start()
            self.push_state()
            return {"success": True}
        except Exception as exc:
            log_exception("create_instance")
            return {"success": False, "error": f"Failed to create instance: {exc}"}

    def delete_instance(self, instance_name):
        try:
            with self._proc_lock:
                if instance_name in self._processes:
                    return {"success": False, "error": "Stop the instance before deleting it."}
                if instance_name in self._installing:
                    return {"success": False, "error": "Instance is currently installing."}
            target = instance_dir(instance_name)
            if not target.exists():
                return {"success": False, "error": "Instance not found."}
            shutil.rmtree(target, ignore_errors=True)

            def _clear_active(s):
                if s.get("active_instance") == instance_name:
                    s["active_instance"] = None
            settings_store.update(_clear_active)

            self.push_state()
            return {"success": True}
        except Exception:
            log_exception("delete_instance")
            return {"success": False, "error": "Failed to delete instance."}

    def set_active_instance(self, instance_name):
        try:
            if not read_instance_meta(instance_name):
                return {"success": False, "error": "Instance not found."}
            settings_store.update(lambda s: s.__setitem__("active_instance", instance_name))
            self.push_state()
            return {"success": True}
        except Exception:
            log_exception("set_active_instance")
            return {"success": False, "error": "Failed to set active instance."}

    def rename_instance(self, old_name, new_name):
        try:
            old_meta = read_instance_meta(old_name)
            if not old_meta:
                return {"success": False, "error": "Instance not found."}
            try:
                new_name = sanitize_instance_name(new_name)
            except ValueError as exc:
                return {"success": False, "error": str(exc)}
            if new_name == old_name:
                return {"success": True}
            if java_instance_dir(new_name).exists() or bedrock_instance_dir(new_name).exists():
                return {"success": False, "error": "An instance with that name already exists."}
            with self._proc_lock:
                if old_name in self._processes or old_name in self._installing:
                    return {"success": False, "error": "Stop or wait for the instance first."}
            old_dir = instance_dir(old_name)
            new_dir = old_dir.parent / new_name
            shutil.move(str(old_dir), str(new_dir))
            old_meta["name"] = new_name
            write_instance_meta(new_name, old_meta)

            def _fix(s):
                if s.get("active_instance") == old_name:
                    s["active_instance"] = new_name
            settings_store.update(_fix)

            self.push_state()
            return {"success": True}
        except Exception:
            log_exception("rename_instance")
            return {"success": False, "error": "Couldn't rename the instance."}

    def get_disk_usage(self, kind):
        try:
            roots = ([INSTANCES_DIR, BEDROCK_INSTANCES_DIR] if kind == "instances"
                     else [SERVERS_DIR])
            out = {}
            for root in roots:
                if not root.is_dir():
                    continue
                for d in root.iterdir():
                    if d.is_dir():
                        out[d.name] = out.get(d.name, 0) + _dir_size(d)
            return {"success": True, "sizes": out}
        except Exception:
            log_exception("get_disk_usage")
            return {"success": False, "sizes": {}}

    def open_instance_folder(self, instance_name):
        try:
            meta = read_instance_meta(instance_name)
            if not meta:
                return {"success": False, "error": "Instance not found."}
            if meta.get("loader") == "bedrock":
                ensure_game_layout(instance_name, "bedrock")
                open_in_file_manager(bedrock_instance_dir(instance_name))
                return {"success": True}
            ensure_game_layout(instance_name, meta.get("loader", "vanilla"))
            open_in_file_manager(instance_game_dir(instance_name))
            return {"success": True}
        except Exception:
            log_exception("open_instance_folder")
            return {"success": False, "error": "Couldn't open the folder."}
