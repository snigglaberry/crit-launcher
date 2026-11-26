"""JS-bridge API: installed content."""

import shutil

from ..core.logs import log_exception
from .content import content_file_path, list_instance_content, read_content_icon, set_mod_enabled
from .store import read_instance_meta


class InstanceContentApi:
    """Mods, resource packs and shaders already inside an instance."""

    def get_instance_content(self, instance_name, kind="mods"):
        try:
            meta = read_instance_meta(instance_name)
            if not meta:
                return {"success": False, "error": "Instance not found."}
            if kind not in ("mods", "resourcepacks", "shaderpacks"):
                return {"success": False, "error": "Unknown content type."}
            if kind == "mods" and meta.get("loader", "vanilla") == "vanilla":
                return {"success": True, "items": [], "loader": "vanilla"}
            return {"success": True, "items": list_instance_content(instance_name, kind),
                    "loader": meta.get("loader", "vanilla")}
        except Exception:
            log_exception("get_instance_content")
            return {"success": False, "error": "Couldn't read the folder."}

    def toggle_instance_mod(self, instance_name, filename, enabled):
        try:
            if not read_instance_meta(instance_name):
                return {"success": False, "error": "Instance not found."}
            if not isinstance(enabled, bool):
                return {"success": False, "error": "Choose whether the mod should be enabled."}
            with self._proc_lock:
                if instance_name in self._processes or instance_name in self._installing:
                    return {"success": False, "error": "Stop the game or wait for installation before changing mods."}
                key = set_mod_enabled(instance_name, filename, enabled)
            return {"success": True, "file": key, "enabled": enabled}
        except ValueError as exc:
            return {"success": False, "error": str(exc)}
        except PermissionError:
            return {"success": False, "error": "Couldn't move the mod - another program is using the file."}
        except Exception:
            log_exception("toggle_instance_mod")
            return {"success": False, "error": "Couldn't move that mod."}

    def delete_instance_content(self, instance_name, kind, filename):
        try:
            if kind not in ("mods", "resourcepacks", "shaderpacks"):
                return {"success": False, "error": "Unknown content type."}
            if not read_instance_meta(instance_name):
                return {"success": False, "error": "Instance not found."}
            with self._proc_lock:
                running = instance_name in self._processes
            if running:
                return {"success": False, "error": "Close the game first - its files are in use."}
            path = content_file_path(instance_name, kind, filename)
            if not (path.exists() or path.is_symlink()) or (kind == "mods" and not path.is_file()):
                return {"success": False, "error": "That file no longer exists."}
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
            else:
                path.unlink()
            return {"success": True}
        except ValueError as exc:
            return {"success": False, "error": str(exc)}
        except PermissionError:
            return {"success": False, "error": "Couldn't delete it - another program is using the file."}
        except Exception:
            log_exception("delete_instance_content")
            return {"success": False, "error": "Couldn't delete that file."}

    def get_content_icon(self, instance_name, kind, filename):
        try:
            return {"success": True, "icon": read_content_icon(instance_name, kind, filename)}
        except Exception:
            return {"success": True, "icon": None}
