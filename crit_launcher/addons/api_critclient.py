"""JS-bridge API: Crit Client."""

import logging
import threading

from ..core.logs import log_exception
from ..core.progress import ProgressTracker
from ..instances.store import (
    instance_mods_dir,
    list_instances,
    read_instance_meta,
    sanitize_instance_name,
)
from .critclient import (
    CRIT_CLIENT_LOADER,
    CRIT_CLIENT_MODS,
    client_jar_status,
    install_client_jar,
    latest_client_assets,
)
from .curseforge import CurseForgeError, _cf_resolve_slug, curseforge_install_to


class CritClientApi:
    """Crit Client's JAR and the separate bundled mod set for new instances."""

    def get_critclient_info(self):
        try:
            assets = latest_client_assets()
        except Exception:
            log_exception("get_critclient_info")
            return {"success": False, "error": "Couldn't reach GitHub to check Crit Client versions."}

        statuses = {}
        for meta in list_instances() if assets else ():
            name = meta.get("name")
            asset = assets.get(meta.get("version")) if meta.get("loader") == CRIT_CLIENT_LOADER else None
            if not name or not asset:
                continue
            try:
                statuses[name] = client_jar_status(instance_mods_dir(name), asset)
            except (OSError, ValueError):
                log_exception(f"check Crit Client in {name}")
        versions = {CRIT_CLIENT_LOADER: {mc: asset.url for mc, asset in assets.items()}} if assets else {}
        return {"success": True, "versions": versions, "statuses": statuses}

    def install_critclient(self, instance_name):
        """Start an install or update of only the Crit Client JAR."""
        try:
            name = sanitize_instance_name(instance_name)
        except (AttributeError, ValueError):
            return {"success": False, "error": "Invalid instance name."}
        with self._proc_lock:
            if self._closing:
                return {"success": False, "error": "The launcher is closing."}
            if name in self._processes:
                return {"success": False, "error": "Stop the instance before updating Crit Client."}
            if name in self._installing or name in self._importing:
                return {"success": False, "error": "Wait for the instance to finish installing."}
            meta = read_instance_meta(name)
            if not meta:
                return {"success": False, "error": "Instance not found."}
            if meta.get("loader") != CRIT_CLIENT_LOADER:
                return {"success": False, "error": "Crit Client needs Fabric."}
            self._installing.add(name)

        try:
            threading.Thread(target=self._critclient_jar_worker, args=(name, meta.get("version")),
                             daemon=True).start()
            return {"success": True}
        except Exception:
            with self._proc_lock:
                self._installing.discard(name)
            log_exception("start Crit Client JAR install")
            return {"success": False, "error": "Couldn't start the Crit Client install."}

    def _critclient_jar_worker(self, name, mc_version):
        result = {"instance": name, "success": False}
        try:
            asset = latest_client_assets().get(mc_version)
            if asset is None:
                result["message"] = f"Crit Client isn't available for Minecraft {mc_version}."
                return
            mods_dir = instance_mods_dir(name)
            status = client_jar_status(mods_dir, asset)
            if status != "current":
                install_client_jar(mods_dir, asset)
            message = ("Crit Client is up to date." if status == "current" else
                       f"Crit Client {'installed' if status == 'missing' else 'updated'} for {name}.")
            result.update(success=True, message=message)
        except FileExistsError as exc:
            result["message"] = str(exc)
        except PermissionError:
            result["message"] = "Couldn't replace the Crit Client JAR. Close any games using it and try again."
        except Exception:
            log_exception("install Crit Client JAR")
            result["message"] = "Couldn't install the Crit Client JAR. Check your connection and try again."
        finally:
            with self._proc_lock:
                self._installing.discard(name)
            self.emit("critClientFinished", result)
            self.push_state()

    def _critclient_worker(self, name, mc_version, loader, asset):
        try:
            self.emit("installStarted", {"instance": name, "title": f"Installing Crit Client into {name}..."})
            tracker = ProgressTracker(self.emit)
            mods_dir = instance_mods_dir(name)
            tracker.reset(asset.size or 1)
            tracker.set_task("Downloading Crit Client", force=True)
            install_client_jar(mods_dir, asset, tracker.add)

            tracker.set_task("Checking which mods are available", force=True)
            picks = []
            for slug in CRIT_CLIENT_MODS:
                try:
                    if slug.startswith("cf:"):
                        picks.append((slug, None))
                        continue
                    version = self._find_version(slug, loader, mc_version)
                except Exception:
                    log_exception(f"crit client mod lookup: {slug}")
                    version = None
                if version:
                    picks.append((slug, version))
                else:
                    logging.info("Crit Client: %s has no %s build for %s, skipping",
                                 slug, loader, mc_version)

            visited, cf_visited, files = set(), set(), []
            tracker.reset(len(picks) + 1, unit="steps")
            for slug, version in picks:
                tracker.set_task(f"Downloading {slug.replace('cf:', '')}", force=True)
                try:
                    if version is None:
                        mod_id = _cf_resolve_slug(slug[3:])
                        curseforge_install_to(mod_id, mods_dir, mc_version, loader,
                                              cf_visited, 0, True)
                        tracker.add(1)
                        continue
                    pid = version.get("project_id")
                    if pid in visited:
                        tracker.add(1)
                        continue
                    visited.add(pid)
                    self._install_mod_with_deps(version, mods_dir, loader, mc_version,
                                                visited, files)
                except CurseForgeError as exc:
                    logging.info("Crit Client: skipping %s (%s)", slug, exc)
                except Exception:
                    log_exception(f"crit client mod install failed: {slug}")
                tracker.add(1)
            tracker.add(1)

            message = f"Crit Client installed into {name}"
            if len(picks) < len(CRIT_CLIENT_MODS):
                missing = len(CRIT_CLIENT_MODS) - len(picks)
                message += f" ({missing} mod{'s' if missing != 1 else ''} unavailable for {mc_version})"
            self.emit("installFinished", {"instance": name, "message": message})

        except Exception as exc:
            log_exception("crit client install failed")
            self.emit("installError", {
                "message": f"Crit Client install failed: {exc}"
            })
        finally:
            with self._proc_lock:
                self._installing.discard(name)
            self.push_state()
