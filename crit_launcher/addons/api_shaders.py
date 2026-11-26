"""JS-bridge API: standalone Iris installation."""

from ..core.logs import log_exception
from ..instances.store import instance_mods_dir, read_instance_meta


class ShadersApi:
    """Install Iris when explicitly requested."""

    def install_iris(self, instance_name):
        try:
            meta = read_instance_meta(instance_name)
            if not meta or meta.get("loader") != "fabric":
                return {"success": False, "error": "Iris needs a Fabric instance."}
            version = self._find_version("iris", "fabric", meta["version"])
            if not version:
                return {"success": False, "error": f"Iris doesn't support Minecraft {meta['version']} yet."}
            mods_dir = instance_mods_dir(instance_name)
            mods_dir.mkdir(parents=True, exist_ok=True)
            files: list = []
            self._install_mod_with_deps(version, mods_dir, "fabric", meta["version"], {"YL57xq9U"}, files)
            return {"success": True, "files": files}
        except Exception as exc:
            log_exception("install_iris")
            return {"success": False, "error": f"Failed to install Iris: {exc}"}
