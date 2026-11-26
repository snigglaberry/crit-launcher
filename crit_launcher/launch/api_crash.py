"""JS-bridge API: crash reports and the fixes offered in the crash popup."""

import json
import urllib.parse
from pathlib import Path

from ..addons.modrinth import MODRINTH_API
from ..core.fsutil import is_within
from ..core.http import http_get_json
from ..core.logs import log_exception
from ..instances.store import instance_mods_dir, read_instance_meta
from ..minecraft.installer import Installer
from .crash_report import AUTO_FIXES, finalize_report


class CrashApi:
    """Showing what went wrong and fixing the parts that can be fixed."""

    def get_crash_report(self):
        try:
            return {"success": True, "report": self._crash_report}
        except Exception:
            log_exception("get_crash_report")
            return {"success": False, "error": "Couldn't read the crash report."}

    def dismiss_crash_report(self):
        try:
            self._crash_report = None
            return {"success": True}
        except Exception:
            log_exception("dismiss_crash_report")
            return {"success": False}

    def fix_crash_issue(self, issue_id):
        try:
            report = self._crash_report
            issue = self._find_issue(report, issue_id)
            if not issue:
                return {"success": False, "error": "That problem is no longer in the report."}
            if issue.get("fix") == "choose_mod":
                return {"success": False,
                        "error": "Pick which mod you want to keep first."}
            self._apply_fix(report, issue)
            finalize_report(report)
            if not issue.get("fixed"):
                return {"success": False,
                        "error": issue.get("fix_error") or "That fix didn't work.",
                        "report": report, "issue": issue}
            return {"success": True, "report": report, "issue": issue}
        except Exception as exc:
            log_exception("fix_crash_issue")
            return {"success": False, "error": str(exc) or "Couldn't fix that."}

    def fix_all_crash_issues(self):
        try:
            report = self._crash_report
            if not report:
                return {"success": False, "error": "There is no crash report to fix."}
            fixed = failed = needs_choice = skipped = 0
            for issue in list(report.get("issues") or []):
                if issue.get("fixed"):
                    continue
                fix = issue.get("fix")
                if fix in AUTO_FIXES:
                    self._apply_fix(report, issue)
                    if issue.get("fixed"):
                        fixed += 1
                    else:
                        failed += 1
                elif fix == "choose_mod":
                    needs_choice += 1
                else:
                    skipped += 1
            finalize_report(report)
            return {"success": True, "report": report, "fixed": fixed, "failed": failed,
                    "needs_choice": needs_choice, "skipped": skipped}
        except Exception as exc:
            log_exception("fix_all_crash_issues")
            return {"success": False, "error": str(exc) or "Couldn't fix the crash."}

    def resolve_crash_conflict(self, issue_id, keep_file):
        try:
            report = self._crash_report
            issue = self._find_issue(report, issue_id)
            if not issue or issue.get("fix") != "choose_mod":
                return {"success": False, "error": "That problem is no longer in the report."}
            keep = Path(str(keep_file or "")).name
            options = issue.get("options") or []
            if not any(option.get("file") == keep for option in options):
                return {"success": False, "error": "Pick one of the mods in the list."}
            removed = []
            for option in options:
                name = option.get("file")
                if name == keep:
                    continue
                ok, message = self._remove_mod_file(report.get("instance"), name)
                if not ok:
                    issue["fix_error"] = message
                    finalize_report(report)
                    return {"success": False, "error": message, "report": report}
                removed.append(name)
            issue["fixed"] = True
            issue["fix_note"] = (f"Kept {keep}" +
                                 (f" and removed {len(removed)} other file"
                                  f"{'s' if len(removed) != 1 else ''}." if removed else "."))
            finalize_report(report)
            return {"success": True, "report": report, "issue": issue}
        except Exception as exc:
            log_exception("resolve_crash_conflict")
            return {"success": False, "error": str(exc) or "Couldn't resolve that."}

    # ---- internals -------------------------------------------------------

    @staticmethod
    def _find_issue(report, issue_id):
        if not report:
            return None
        for issue in report.get("issues") or []:
            if issue.get("id") == issue_id:
                return issue
        return None

    def _apply_fix(self, report, issue):
        fix = issue.get("fix")
        if issue.get("fixed") or fix not in AUTO_FIXES:
            return
        issue.pop("fix_error", None)
        name = report.get("instance")
        try:
            if fix == "remove_mod":
                ok, message = self._remove_mod_file(name, issue.get("file"))
            elif fix == "install_dependency":
                ok, message = self._install_dependency(name, issue)
            elif fix == "update_mod":
                ok, message = self._update_mod(name, issue)
            else:
                ok, message = self._repair_instance(name)
        except Exception as exc:
            log_exception(f"crash fix {fix}")
            ok, message = False, str(exc) or "That fix failed."
        if ok:
            issue["fixed"] = True
            issue["fix_note"] = message
        else:
            issue["fix_error"] = message

    def _remove_mod_file(self, instance_name, filename):
        if not instance_name:
            return False, "Instance not found."
        with self._proc_lock:
            running = instance_name in self._processes
        if running:
            return False, "Close the game first - its files are in use."
        folder = instance_mods_dir(instance_name)
        name = Path(str(filename or "")).name
        path = folder / name
        if not name or not is_within(folder, path):
            return False, "That mod file isn't in the mods folder."
        if not path.is_file():
            return True, f"{name} was already removed."
        try:
            path.unlink()
        except PermissionError:
            return False, "Couldn't delete it - another program is using the file."
        return True, f"Removed {name}."

    def _install_dependency(self, instance_name, issue):
        modid = issue.get("modid") or issue.get("requested_by")
        if not modid:
            return False, "That dependency has no name."
        meta = read_instance_meta(instance_name)
        if not meta:
            return False, "Instance not found."
        with self._proc_lock:
            running = instance_name in self._processes
        if running:
            return False, "Close the game first - its files are in use."

        hit = self._find_modrinth_mod(modid, meta.get("loader"), meta.get("version"))
        if not hit:
            return False, (f"Couldn't find '{modid}' on Modrinth. "
                           "Download it manually and put it in the mods folder.")
        result = self.install_addon(hit["project_id"], "mod", instance_name, True)
        if not result.get("success"):
            return False, result.get("error") or f"Couldn't install {modid}."
        title = hit.get("title") or modid
        extra = len(result.get("files") or [])
        note = f"Installed {title}" + (f" (+{extra - 1} dependenc{'y' if extra == 2 else 'ies'})"
                                       if extra > 1 else "") + "."
        return True, note

    def _update_mod(self, instance_name, issue):
        modid = issue.get("modid")
        old_file = issue.get("file")
        if not modid:
            return False, "That mod has no name."
        meta = read_instance_meta(instance_name)
        if not meta:
            return False, "Instance not found."
        with self._proc_lock:
            running = instance_name in self._processes
        if running:
            return False, "Close the game first - its files are in use."

        hit = self._find_modrinth_mod(modid, meta.get("loader"), meta.get("version"))
        if not hit:
            return False, (f"Couldn't find a matching version of '{modid}' on Modrinth. "
                           "Download one manually and put it in the mods folder.")
        result = self.install_addon(hit["project_id"], "mod", instance_name, True)
        if not result.get("success"):
            return False, result.get("error") or f"Couldn't install {modid}."

        installed = result.get("files") or []
        title = hit.get("title") or modid
        note = f"Installed a matching version of {title}."
        if old_file and old_file not in installed:
            old_path = instance_mods_dir(instance_name) / Path(old_file).name
            if old_path.is_file():
                ok, message = self._remove_mod_file(instance_name, old_file)
                if not ok:
                    return False, (f"Installed a new version but couldn't remove the old "
                                   f"file: {message}")
                note = f"Replaced {old_file} with a matching version of {title}."
        return True, note

    def _find_modrinth_mod(self, modid, loader, mc_version):
        def search(with_facets):
            params = {"query": str(modid), "limit": "10"}
            if with_facets:
                facets = [["project_type:mod"]]
                if loader in ("fabric", "forge", "neoforge", "quilt"):
                    facets.append([f"categories:{loader}"])
                if mc_version:
                    facets.append([f"versions:{mc_version}"])
                params["facets"] = json.dumps(facets)
            url = f"{MODRINTH_API}/search?" + urllib.parse.urlencode(params)
            try:
                return http_get_json(url).get("hits") or []
            except Exception:
                return []

        hits = search(True) or search(False)
        if not hits:
            return None

        def norm(value):
            return "".join(ch for ch in str(value or "").lower() if ch.isalnum())

        low = str(modid).strip().lower()
        target = norm(modid)
        for hit in hits:
            if str(hit.get("slug") or "").strip().lower() == low:
                return hit
        for hit in hits:
            if str(hit.get("title") or "").strip().lower() == low:
                return hit
        for hit in hits:
            if target and target in (norm(hit.get("slug")), norm(hit.get("title"))):
                return hit
        return hits[0]

    def _repair_instance(self, instance_name):
        meta = read_instance_meta(instance_name)
        if not meta:
            return False, "Instance not found."
        with self._proc_lock:
            running = instance_name in self._processes
        if running:
            return False, "Close the game first - its files are in use."
        if not Installer(self, meta).run():
            return False, "The repair didn't finish. Check the install message for details."
        return True, "Repaired the game files."
