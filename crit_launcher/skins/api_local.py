"""JS-bridge API: offline-account skins."""

import logging
import re
import urllib.parse
import urllib.request

from ..accounts.ely import ely_skin_for
from ..accounts.store import accounts_store, selected_account
from ..addons.critclient import client_skin_bridge
from ..addons.modrinth import MODRINTH_API
from ..config.paths import SKINS_DIR
from ..config.settings import settings_store
from ..core.fsutil import _write_bytes_atomic
from ..core.http import USER_AGENT, http_get_json
from ..core.images import PNG_SIG, decode_png_uri, png_uri
from ..core.logs import log_exception
from ..instances.store import (
    active_instance,
    instance_game_dir,
    instance_mods_dir,
    list_instances,
    read_instance_meta,
)
from .capes import MOJANG_CAPES, _CAPE_BY_ID, _cape_url
from .critclient_bridge import read_local_skin, skin_variant, write_local_skin
from .custom_skin_loader import (
    CSL_PROJECT,
    CSL_RUN_ONCE_MSG,
    _csl_find_config,
    _csl_installed,
    _csl_local_dir,
    _csl_write_config,
    _mod_present,
)
from .textures import fetch_texture_uri


_CSL_LOADERS = ("fabric", "forge", "neoforge")
_CSL_USERNAME = re.compile(r"[A-Za-z0-9_]{1,16}")


def _offline_account(settings=None):
    if settings is None:
        settings = settings_store.read()
    account = selected_account(accounts_store.read(), settings.get("active_account"))
    if not account or account.get("type") != "offline":
        raise ValueError("Local skins are for offline accounts.")
    return account


def _csl_username(account):
    name = account.get("name") or ""
    if not _CSL_USERNAME.fullmatch(name):
        raise ValueError("This account's username can't be used for a local skin.")
    return name


def _local_backend(meta):
    """Classify enabled mods on disk, without a release check or network access."""
    if not meta or meta.get("loader") not in _CSL_LOADERS:
        return None, False, None
    name = meta["name"]
    bridge = client_skin_bridge(name)
    csl = _csl_installed(name)
    if bridge is None:
        return "csl", csl, None
    issues = []
    if not bridge:
        issues.append("Update Crit Client to use launcher skins.")
    if csl:
        issues.append("Skin loader conflict: Crit Client and CustomSkinLoader are both enabled. Disable one in View addons → Mods.")
    if meta["loader"] != "fabric":
        issues.append("Crit Client's skin loader requires a Fabric instance.")
    return "critclient", not issues, " ".join(issues) or None


class LocalSkinsApi:
    """Offline skins through Crit Client's bridge or CustomSkinLoader."""

    def _csl_ensure(self, instance_name, meta) -> bool:
        if _csl_installed(instance_name):
            return False
        loader, mc = meta.get("loader"), meta.get("version")
        try:
            version = self._find_version(CSL_PROJECT, loader, mc)
        except Exception:
            log_exception("csl lookup")
            raise ValueError("Couldn't reach Modrinth to download CustomSkinLoader. Check your connection.")
        if not version:
            raise ValueError(f"CustomSkinLoader doesn't support {str(loader).capitalize()} {mc}, "
                             "so this version is unsupported for local skins.")
        mods_dir = instance_mods_dir(instance_name)
        mods_dir.mkdir(parents=True, exist_ok=True)
        try:
            self._download_version_file(version, mods_dir)
            for dep in version.get("dependencies", []):
                if dep.get("dependency_type") != "required" or not dep.get("project_id"):
                    continue
                try:
                    proj = http_get_json(f"{MODRINTH_API}/project/{urllib.parse.quote(dep['project_id'])}")
                    if _mod_present(instance_name, proj.get("slug") or proj.get("title") or ""):
                        continue
                    dv = self._find_version(dep["project_id"], loader, mc)
                    if dv:
                        self._download_version_file(dv, mods_dir)
                except Exception:
                    log_exception("csl dependency")
        except Exception:
            log_exception("csl download")
            raise ValueError("Couldn't download CustomSkinLoader. Check your connection and try again.")
        if not _csl_installed(instance_name):
            raise ValueError("CustomSkinLoader didn't download correctly. Try again.")
        return True

    def local_skin_targets(self, kind="skin"):
        if kind not in ("skin", "cape"):
            return {"success": False, "error": "Unknown local skin target type."}
        try:
            targets, blocked, support = [], [], {}
            for inst in list_instances():
                backend, installed, error = _local_backend(inst)
                if not backend or (kind == "cape" and backend != "csl"):
                    continue
                if backend == "csl" and not installed:
                    key = (inst["loader"], inst["version"])
                    if key not in support:
                        try:
                            support[key] = bool(self._find_version(CSL_PROJECT, *key))
                        except Exception:
                            support[key] = None
                    if support[key] is False:
                        continue
                    if support[key] is None:
                        error = "Couldn't check CustomSkinLoader compatibility on Modrinth."
                if error:
                    blocked.append({"name": inst["name"], "error": error})
                else:
                    targets.append({"name": inst["name"], "version": inst["version"], "loader": inst["loader"],
                                    "backend": backend, "installed": installed, "csl": backend == "csl" and installed})
            return {"success": True, "instances": targets, "blocked": blocked}
        except Exception:
            log_exception("local_skin_targets")
            return {"success": False, "error": "Failed to list instances."}

    def local_skin_plan(self, kind="skin"):
        targets = self.local_skin_targets(kind)
        if not targets["success"]:
            return targets
        items = targets["instances"]
        return {"success": True, "total": len(items), "blocked": targets["blocked"],
                "installed": [i["name"] for i in items if i["installed"]],
                "installable": [i for i in items if not i["installed"]]}

    def _local_skin_state_for(self, account, settings, instance_name=None):
        if instance_name is not None:
            meta = read_instance_meta(instance_name)
            if not meta:
                raise ValueError("Instance not found.")
        else:
            # Live previews poll frequently; normally only the selected metadata file is needed.
            preferred = settings.get("active_instance")
            try:
                meta = read_instance_meta(preferred) if isinstance(preferred, str) and preferred else None
            except ValueError:
                meta = None
            if not meta:
                meta = active_instance(list_instances(), preferred)
        backend, installed, error = _local_backend(meta)
        state = {"success": not error, "name": account["name"], "backend": backend, "skin": None,
                 "variant": None, "cape_id": None, "capes": []}
        if error:
            return dict(state, error=error)
        if backend == "critclient":
            record = read_local_skin(meta["name"], account["uuid"])
            if record and record["png"] is not None:
                state.update(skin=png_uri(record["png"]), variant=record["variant"])
        elif backend == "csl":
            username = _csl_username(account)
            skin = SKINS_DIR / f"{username}.png"  # legacy Ely copy, CSL only
            if installed:
                base = _csl_local_dir(meta["name"])
                local = base / "skins" / f"{username}.png"
                if local.is_file():
                    skin = local
                if (base / "capes" / f"{username}.png").is_file():
                    try:
                        cape = _CAPE_BY_ID.get((base / ".crit_cape").read_text(encoding="utf-8").strip())
                        if cape:
                            state.update(cape_id=cape["id"], capes=[{"alias": cape["name"], "active": True}])
                    except OSError:
                        pass
            if skin.is_file():
                state["skin"] = png_uri(skin.read_bytes())
        return state

    def local_skin_state(self, instance_name=None):
        try:
            settings = settings_store.read()
            return self._local_skin_state_for(_offline_account(settings), settings, instance_name)
        except ValueError as exc:
            return {"success": False, "error": str(exc)}
        except Exception as exc:
            log_exception("local_skin_state")
            return {"success": False, "error": f"Couldn't read the local skin: {exc}"}

    def local_use_ely(self, name=None):
        """Copy an Ely.by skin onto the offline account's local skin file."""
        try:
            account = _offline_account()
            source = (name or account["name"]).strip()
            skin = ely_skin_for(source)
            if not skin:
                return {"success": False, "results": [], "error": f"Ely.by has no skin for {source}."}

            result = self._apply_local_skin(account, skin["image"], [i["name"] for i in list_instances()], skin["variant"])
            if result["success"]:
                settings_store.update(lambda s: s.__setitem__("local_ely_source", source))
            return result
        except ValueError as exc:
            return {"success": False, "results": [], "error": str(exc)}
        except Exception:
            log_exception("local_use_ely")
            return {"success": False, "results": [], "error": "Couldn't copy that Ely.by skin."}

    def local_install_csl(self, instance_name):
        try:
            account = _offline_account()
            meta = read_instance_meta(instance_name)
            if not meta:
                raise ValueError("Instance not found.")
            backend, _, error = _local_backend(meta)
            if backend != "csl":
                raise ValueError(error or ("Crit Client already handles launcher skins." if backend == "critclient"
                                           else "CustomSkinLoader needs a compatible Fabric, Forge or NeoForge instance."))
            _csl_username(account)
            return {"success": True, "downloaded": self._csl_ensure(instance_name, meta)}
        except ValueError as exc:
            return {"success": False, "error": str(exc)}
        except Exception as exc:
            log_exception("local_install_csl")
            return {"success": False, "error": f"Couldn't install CustomSkinLoader: {exc}"}

    def _local_bulk(self, account, install, write, write_config=True, write_crit=None):
        wanted = {n for n in (install or []) if isinstance(n, str)}
        results = []
        for inst in list_instances():
            name = inst["name"]
            downloaded = False
            try:
                backend, installed, error = _local_backend(inst)
                if backend == "critclient" and write_crit is not None:
                    if error:
                        raise ValueError(error)
                    write_crit(name, account["uuid"])
                    results.append({"name": name, "ok": True, "downloaded": False})
                    continue
                if backend != "csl" or (not installed and name not in wanted):
                    continue
                username = _csl_username(account)
                if not installed:
                    downloaded = self._csl_ensure(name, inst)
                if write_config:
                    if _csl_find_config(name) is None:
                        logging.info("CustomSkinLoader.json not found for %s (looked under %s)",
                                     name, instance_game_dir(name))
                        prefix = "CustomSkinLoader was installed. " if downloaded else ""
                        results.append({"name": name, "ok": False, "downloaded": downloaded,
                                        "error": prefix + CSL_RUN_ONCE_MSG})
                        continue
                    _csl_write_config(name)
                write(_csl_local_dir(name), username)
                results.append({"name": name, "ok": True, "downloaded": downloaded})
            except ValueError as exc:
                results.append({"name": name, "ok": False, "downloaded": downloaded, "error": str(exc)})
            except Exception as exc:
                log_exception("local skin on " + name)
                results.append({"name": name, "ok": False, "downloaded": downloaded, "error": f"Failed: {exc}"})
        if not results:
            return {"success": False, "results": [], "error": "No eligible instances for this change."}
        return {"success": any(r["ok"] for r in results), "results": results}

    def _apply_local_skin(self, account, image, install, variant):
        png = decode_png_uri(image)
        variant = skin_variant(png, variant)

        def write(base, username):
            _write_bytes_atomic(base / "skins" / f"{username}.png", png)

        def write_crit(name, account_uuid):
            write_local_skin(name, account_uuid, png, variant)

        return self._local_bulk(account, install, write, write_crit=write_crit)

    def local_set_skin(self, image, install=None, variant="classic"):
        try:
            return self._apply_local_skin(_offline_account(), image, install, variant)
        except ValueError as exc:
            return {"success": False, "results": [], "error": str(exc)}
        except Exception as exc:
            log_exception("local_set_skin")
            return {"success": False, "results": [], "error": f"Couldn't set the local skin: {exc}"}

    def local_ely_preview(self, name):
        try:
            skin = ely_skin_for((name or "").strip())
            if not skin:
                return {"success": False, "error": f"No Ely.by skin for {name}."}
            return {"success": True, **skin}
        except Exception:
            log_exception("local_ely_preview")
            return {"success": False, "error": "Couldn't reach Ely.by."}

    def local_capes(self):
        return {"success": True, "capes": [{"id": c["id"], "name": c["name"], "url": _cape_url(c)} for c in MOJANG_CAPES]}

    def local_cape_image(self, cape_id):
        try:
            cape = _CAPE_BY_ID.get(cape_id)
            if not cape:
                return {"success": False, "error": "Unknown cape."}
            uri = fetch_texture_uri(_cape_url(cape))
            return {"success": bool(uri), "image": uri}
        except Exception:
            log_exception("local_cape_image")
            return {"success": False, "error": "Couldn't load that cape."}

    def local_set_cape(self, cape_id=None, install=None):
        try:
            account = _offline_account()
            cape, data = None, None
            if cape_id:
                cape = _CAPE_BY_ID.get(cape_id)
                if not cape:
                    raise ValueError("Unknown cape.")

            def write(base, username):
                nonlocal data
                cape_path = base / "capes" / f"{username}.png"
                if cape is None:
                    cape_path.unlink(missing_ok=True)
                    (base / ".crit_cape").unlink(missing_ok=True)
                else:
                    if data is None:
                        req = urllib.request.Request(_cape_url(cape), headers={"User-Agent": USER_AGENT})
                        try:
                            with urllib.request.urlopen(req, timeout=20) as resp:
                                texture = resp.read(300_001)
                        except OSError:
                            raise ValueError("Couldn't download that cape. Check your connection.") from None
                        if not texture.startswith(PNG_SIG) or len(texture) > 300_000:
                            raise ValueError("That cape texture isn't valid.")
                        data = texture
                    _write_bytes_atomic(cape_path, data)
                    _write_bytes_atomic(base / ".crit_cape", cape["id"].encode("utf-8"))
            return self._local_bulk(account, install if cape else None, write, write_config=bool(cape))
        except ValueError as exc:
            return {"success": False, "results": [], "error": str(exc)}
        except Exception as exc:
            log_exception("local_set_cape")
            return {"success": False, "results": [], "error": f"Couldn't set the cape: {exc}"}

    def local_reset(self):
        try:
            account = _offline_account()

            def write(base, username):
                (base / "skins" / f"{username}.png").unlink(missing_ok=True)
                (base / "capes" / f"{username}.png").unlink(missing_ok=True)
                (base / ".crit_cape").unlink(missing_ok=True)

            if _CSL_USERNAME.fullmatch(account["name"]):
                (SKINS_DIR / f"{account['name']}.png").unlink(missing_ok=True)
            return self._local_bulk(account, None, write, write_config=False,
                                    write_crit=lambda name, uid: write_local_skin(name, uid, None))
        except ValueError as exc:
            return {"success": False, "results": [], "error": str(exc)}
        except Exception as exc:
            log_exception("local_reset")
            return {"success": False, "results": [], "error": f"Couldn't reset the local skin: {exc}"}
