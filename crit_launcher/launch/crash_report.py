"""Reading a crash log, naming the problems and deciding what can be fixed."""

import re
import time
from pathlib import Path

from ..instances.content import read_mod_jar
from ..instances.store import instance_game_dir, instance_mods_dir, read_instance_meta


AUTO_FIXES = {"remove_mod", "install_dependency", "update_mod", "repair_instance"}

# Things a mod can depend on that are not mods themselves; a mod asking for a
# different version of these is simply built for another game/loader version.
GAME_IDS = {"minecraft", "fabricloader", "fabric-loader", "forge", "neoforge",
            "java", "quilt_loader", "quilt-loader"}

_NATIVE_CODES = {0xC0000005, 0xC0000409, 0xC000001D, 0xC0000094, 0xC00000FD}

FORGE_DEP_RE = re.compile(
    r"Mod ID:\s*['\"]?(?P<dep>[^,'\"\s]+)['\"]?,\s*"
    r"Requested by:\s*['\"]?(?P<req>[^,'\"\s]+)['\"]?,\s*"
    r"Expected range:\s*['\"](?P<range>[^'\"]*)['\"],\s*"
    r"Actual version:\s*['\"](?P<actual>[^'\"]*)['\"]",
    re.I,
)

FABRIC_DEP_RE = re.compile(
    r"Mod\s+'(?P<name>[^']+)'(?:\s*\((?P<id>[^)]+)\))?"
    r"[^\n]*?\s+requires\s+(?P<what>[^\n]*?)\s+of\s+(?:mod\s+)?"
    r"(?:'(?P<depname>[^']+)'(?:\s*\((?P<depid>[^)]+)\))?|(?P<depbare>[A-Za-z0-9_.\-]+))"
    r"[^\n]*?(?P<state>missing|not\s+present|not\s+found|isn't\s+installed|"
    r"is\s+not\s+installed|incompatible|but\s+only|is\s+present)",
    re.I,
)

# "Mod 'X' (x) 1.0 is incompatible with the game" - no second mod to offer.
_FABRIC_GAME_RE = re.compile(
    r"Mod\s+'(?P<name>[^']+)'(?:\s*\((?P<id>[^)]+)\))?[^\n]*?\s+"
    r"(?:is\s+incompatible\s+with\s+(?:the\s+game|this\s+version|Minecraft[^\n']*)|"
    r"requires\s+a\s+different\s+version\s+of\s+(?:Minecraft|the\s+game))",
    re.I,
)

_BAD_JAR_RE = re.compile(
    r"zip\s+END\s+header\s+not\s+found|invalid\s+(?:LOC|central\s+directory)|"
    r"Error\s+reading\s+[^\n]*?\.jar|not\s+a\s+valid\s+(?:jar|zip)|"
    r"ZipException|InvalidModFileException|corrupt(?:ed)?\s+(?:mod\s+)?(?:jar|file)",
    re.I,
)

_GRAPHICS_RE = re.compile(
    r"Pixel format not accelerated|Failed to create window|GLFW error|"
    r"Couldn't set pixel format|WGL:\s*(?:Failed|The driver)|"
    r"OpenGL\s+(?:error|is not supported)|"
    r"The driver does not appear to support OpenGL",
    re.I,
)

_FABRIC_PAIR_RE = re.compile(
    r"Mod\s+'(?P<a>[^']+)'(?:\s*\((?P<aid>[^)]+)\))?[^\n]*?\s+"
    r"(?:is\s+incompatible\s+with|conflicts\s+with)\s+"
    r"(?:(?:any\s+\S+\s+version|any\s+version|version\s+\S+)\s+of\s+)?"
    r"(?:mod\s+)?'(?P<b>[^']+)'(?:\s*\((?P<bid>[^)]+)\))?",
    re.I,
)

_SOLUTION_RE = re.compile(
    r"A potential solution has been determined[^\n]*\n(?P<body>.*?)"
    r"(?=\n\s*\n|\n\s*More details:|\Z)",
    re.I | re.S,
)

_DUP_RE = re.compile(
    r"duplicate mods?(?:\s+found)?|DuplicateModsFoundException|duplicate mod id", re.I)
_JAR_RE = re.compile(r"([A-Za-z0-9_.+\-]+\.jar(?:\.disabled)?)")

_FRAME_RE = re.compile(r"\bat\s+([A-Za-z0-9_.$]+)\(")
_SUSPECT_RE = re.compile(r"Suspected Mods:\s*([^\r\n]+)", re.I)
_DESC_RE = re.compile(r"Description:\s*([^\r\n]+)", re.I)
_FAILURE_RE = re.compile(r"Failure message:\s*([^\r\n]+)", re.I)

_CRASH_MARKERS = (
    "Minecraft Crash Report",
    "Incompatible mods found",
    "FormattedException",
    "A potential solution has been determined",
    "Mod resolution encountered",
    "Missing or unsupported mandatory dependencies",
    "Duplicate mods found",
    "Found duplicate mods",
    "Failed to load mods",
    "A fatal error has been detected",
    "EXCEPTION_ACCESS_VIOLATION",
    "UnsupportedClassVersionError",
    "OutOfMemoryError",
    "Exception in thread",
)


def looks_like_crash(text: str) -> bool:
    return any(marker in text for marker in _CRASH_MARKERS)


def _read_text(path, max_bytes: int = 800_000) -> str:
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
            f.seek(max(0, size - max_bytes))
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def _newest_crash_report(instance_name: str, started: float):
    folder = instance_game_dir(instance_name) / "crash-reports"
    if not folder.is_dir():
        return None
    best, best_mtime = None, 0.0
    for path in folder.glob("crash-*.txt"):
        try:
            mtime = path.stat().st_mtime
        except OSError:
            continue
        if mtime < started - 15:
            continue
        if mtime > best_mtime:
            best, best_mtime = path, mtime
    return best


def _mod_index(instance_name: str) -> list:
    folder = instance_mods_dir(instance_name)
    mods = []
    if not folder.is_dir():
        return mods
    for path in folder.iterdir():
        low = path.name.lower()
        if not path.is_file() or not (low.endswith(".jar") or low.endswith(".jar.disabled")):
            continue
        info = read_mod_jar(path)
        mods.append({
            "file": path.name,
            "name": info.get("name"),
            "version": info.get("version"),
            "loader": info.get("loader"),
            "modid": info.get("modid"),
        })
    return mods


def _resolve_mod(mods: list, *keys, strict: bool = False):
    for key in keys:
        if not key:
            continue
        low = str(key).strip().lower()
        if not low:
            continue
        for mod in mods:
            if (mod.get("modid") or "").lower() == low:
                return mod
            if (mod.get("name") or "").lower() == low:
                return mod
            if mod["file"].lower() == low:
                return mod
        if strict:
            continue
        for mod in mods:
            if low in mod["file"].lower():
                return mod
    return None


def _mod_label(mod, fallback: str) -> str:
    if mod:
        return mod.get("name") or mod["file"]
    return fallback


def _missing_version(actual: str) -> bool:
    low = (actual or "").strip().lower()
    return not low or "missing" in low or low in ("null", "none", "[missing]")


def _mod_option(mod: dict) -> dict:
    return {"file": mod["file"], "label": mod.get("name") or mod["file"],
            "version": mod.get("version")}


def _norm_dep(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", str(name or "").lower())


def finalize_report(report: dict) -> dict:
    """Recompute the counts the page uses after issues are fixed."""
    issues = report.get("issues") or []
    auto = sum(1 for i in issues if i.get("fix") in AUTO_FIXES and not i.get("fixed"))
    choices = sum(1 for i in issues if i.get("fix") == "choose_mod" and not i.get("fixed"))
    fixed = sum(1 for i in issues if i.get("fixed"))
    open_issues = [i for i in issues if not i.get("fixed")]

    report["auto_fixable"] = auto
    report["needs_choice"] = choices
    report["fixed_count"] = fixed
    report["can_fix_all"] = auto > 0
    if not open_issues and fixed:
        report["summary"] = (f"All {fixed} issue{'s' if fixed != 1 else ''} fixed. "
                             "Try launching again.")
    elif len(open_issues) == 1:
        report["summary"] = open_issues[0]["title"]
    elif open_issues:
        report["summary"] = f"{len(open_issues)} problems found."
    else:
        report["summary"] = "Minecraft crashed."
    return report


def analyze_crash(instance_name: str, log_path, started: float, exit_code=None) -> dict:
    """Build a report of everything that looks wrong about the last launch."""
    meta = read_instance_meta(instance_name) or {}
    loader = meta.get("loader", "vanilla")

    log_text = _read_text(log_path)
    crash_path = _newest_crash_report(instance_name, started)
    crash_text = _read_text(crash_path) if crash_path else ""
    text = log_text + "\n" + crash_text
    mods = _mod_index(instance_name)

    issues = []
    seen = set()

    def add(issue: dict):
        key = issue.pop("_key", None) or (issue.get("type"), issue.get("title"))
        if key in seen:
            return
        seen.add(key)
        issue.setdefault("fixed", False)
        issue.setdefault("fix", None)
        issue.setdefault("fixable", issue.get("fix") is not None)
        issue["id"] = f"crash-{len(issues) + 1}"
        issues.append(issue)

    _forge_dependencies(text, mods, add)
    _failure_message_deps(text, mods, add)
    _fabric_dependencies(text, mods, add)
    _fabric_solutions(text, mods, issues, add)
    _fabric_incompatibilities(text, mods, issues, add)
    _fabric_game_incompat(text, mods, issues, add)
    _duplicate_mods(text, mods, add)
    _duplicate_modids(mods, issues, add)
    _mixin_failures(text, mods, add)
    _explicit_mod_failures(text, mods, add)
    _corrupt_jars(text, mods, add)
    _java_problems(text, add)
    _memory_problems(text, add)
    _native_crashes(text, exit_code, add)
    _graphics_problems(text, add)
    _broken_install(text, add)
    _suspected_mods(text, mods, issues, add)
    _stack_trace_mods(text, mods, issues, add)

    if not issues and re.search(r"Incompatible mods found|incompatible mod set",
                                text, re.I):
        add({
            "_key": ("fabric-set",),
            "type": "mod_conflict",
            "title": "Mods are incompatible",
            "detail": _crash_detail(crash_text or text),
            "fix": None,
        })

    if not issues:
        detail = _crash_detail(crash_text or text)
        add({
            "type": "unknown",
            "title": "Minecraft crashed",
            "detail": detail,
            "fix": None,
        })

    report = {
        "instance": instance_name,
        "version": meta.get("version"),
        "loader": loader,
        "loader_version": meta.get("loader_version"),
        "exit_code": exit_code,
        "crash_time": time.time(),
        "log_path": str(log_path),
        "crash_report_path": str(crash_path) if crash_path else None,
        "issues": issues,
    }
    return finalize_report(report)


def _forge_dependencies(text: str, mods: list, add):
    for match in FORGE_DEP_RE.finditer(text):
        dep = match.group("dep")
        requested_by = match.group("req")
        expected = match.group("range").strip()
        actual = match.group("actual").strip()
        if requested_by.lower() in GAME_IDS:
            continue
        requester = _resolve_mod(mods, requested_by)
        label = _mod_label(requester, requested_by)

        if dep.lower() == "java":
            add({
                "_key": ("java",),
                "type": "java",
                "title": "Wrong Java version",
                "detail": (f"{label} needs Java {expected or 'a newer version'}, but "
                           f"{actual or 'an older Java'} is being used. Clear the custom "
                           "Java path in Settings so Crit Launcher picks the right one."),
                "fix": None,
            })
            continue

        if _missing_version(actual):
            if dep.lower() in GAME_IDS:
                add({
                    "_key": ("game", dep.lower(), requested_by.lower()),
                    "type": "incompatible_mod",
                    "title": f"Mod incompatible: {label}",
                    "detail": (f"{label} needs {dep} {expected or 'another version'}, "
                               "which isn't installed. It was probably made for a "
                               "different Minecraft or loader version."),
                    "fix": "remove_mod" if requester else None,
                    "file": requester["file"] if requester else None,
                    "mod_name": label,
                })
            else:
                add({
                    "_key": ("dep", dep.lower()),
                    "type": "missing_dependency",
                    "title": f"Missing dependency: {dep}",
                    "detail": (f"{label} needs {dep}"
                               f"{f' {expected}' if expected else ''}. "
                               "It can be downloaded from Modrinth."),
                    "fix": "install_dependency",
                    "modid": dep,
                    "requested_by": requested_by,
                })
        else:
            installed = _resolve_mod(mods, dep, strict=True)
            if dep.lower() in GAME_IDS:
                add({
                    "_key": ("game", dep.lower(), requested_by.lower()),
                    "type": "incompatible_mod",
                    "title": f"Mod incompatible: {label}",
                    "detail": (f"{label} needs {dep} {expected or 'a different version'}, "
                               f"but {actual} is installed. It was built for a different "
                               "Minecraft or loader version."),
                    "fix": "remove_mod" if requester else None,
                    "file": requester["file"] if requester else None,
                    "mod_name": label,
                })
            elif installed:
                add({
                    "_key": ("depver", dep.lower(), actual.lower()),
                    "type": "incompatible_mod",
                    "title": f"Outdated dependency: {_mod_label(installed, dep)}",
                    "detail": (f"{label} needs {dep} {expected or 'a different version'}, "
                               f"but {actual} is installed. Updating the dependency to a "
                               "version that matches this instance should fix the crash."),
                    "fix": "update_mod",
                    "file": installed["file"],
                    "modid": dep,
                    "requested_by": requested_by,
                })
            else:
                add({
                    "_key": ("depver", dep.lower(), actual.lower()),
                    "type": "incompatible_mod",
                    "title": f"Incompatible mod: {label}",
                    "detail": (f"{label} needs {dep} {expected or 'a different version'}, "
                               f"but {actual} is installed."),
                    "fix": "remove_mod" if requester else None,
                    "file": requester["file"] if requester else None,
                    "mod_name": label,
                })


def _fabric_dependencies(text: str, mods: list, add):
    for match in FABRIC_DEP_RE.finditer(text):
        requester_name = match.group("name")
        requester_id = match.group("id") or requester_name
        dep = (match.group("depid") or match.group("depbare")
               or match.group("depname") or "").strip()
        what = (match.group("what") or "").strip()
        state = (match.group("state") or "").strip()
        requester = _resolve_mod(mods, requester_id, requester_name)
        label = _mod_label(requester, requester_name)

        if not dep:
            continue
        if requester_id.lower() in GAME_IDS:
            continue

        if dep.lower() == "java":
            add({
                "_key": ("java",),
                "type": "java",
                "title": "Wrong Java version",
                "detail": (f"{label} needs Java {what or 'a newer version'}, but an "
                           "older Java is being used. Clear the custom Java path in "
                           "Settings so Crit Launcher picks the right one."),
                "fix": None,
            })
            continue

        if dep.lower() in GAME_IDS:
            add({
                "_key": ("game", dep.lower(), requester_id.lower()),
                "type": "incompatible_mod",
                "title": f"Mod incompatible: {label}",
                "detail": (f"{label} needs {what or 'another version'} of {dep}, which "
                           "isn't installed. It was probably made for a different "
                           "Minecraft or loader version."),
                "fix": "remove_mod" if requester else None,
                "file": requester["file"] if requester else None,
                "mod_name": label,
            })
            continue

        installed = _resolve_mod(mods, dep)
        present = bool(re.search(r"but\s+only|only\s+version|is\s+present", state, re.I))
        if present and installed:
            add({
                "_key": ("depver", dep.lower()),
                "type": "incompatible_mod",
                "title": f"Outdated dependency: {_mod_label(installed, dep)}",
                "detail": (f"{label} needs {what or 'a newer version'} of {dep}, but "
                           f"{_mod_label(installed, dep)} is installed. Updating it to a "
                           "version that matches this instance should fix the crash."),
                "fix": "update_mod",
                "file": installed["file"],
                "modid": dep,
                "requested_by": requester_id,
            })
        else:
            add({
                "_key": ("dep", dep.lower()),
                "type": "missing_dependency",
                "title": f"Missing dependency: {dep}",
                "detail": (f"{label} needs {what or 'a compatible version'} of {dep}. "
                           "It can be downloaded from Modrinth."),
                "fix": "install_dependency",
                "modid": dep,
                "requested_by": requester_id,
            })


def _fabric_solutions(text: str, mods: list, issues: list, add):
    """Fabric prints a plain-language "Install X" list before the details."""
    for block in _SOLUTION_RE.finditer(text):
        for raw in block.group("body").splitlines():
            line = raw.strip()
            if not line:
                continue
            line = line.lstrip("-*\u2022 ").strip()
            if not line:
                continue

            install = re.match(r"Install\s+(?P<name>.+?),\s*(?P<ver>.+?)\.?$",
                               line, re.I)
            if install:
                name = install.group("name").strip().strip("'\"")
                inner = re.match(r"(.+?)\s*\(([^)]+)\)$", name)
                if inner:
                    name = inner.group(2).strip()
                if name:
                    norm = _norm_dep(name)
                    covered = False
                    for existing in issues:
                        if existing.get("type") != "missing_dependency":
                            continue
                        other = _norm_dep(existing.get("modid") or "")
                        if other and (other == norm or other in norm or norm in other):
                            covered = True
                            break
                    if covered:
                        continue
                    add({
                        "_key": ("dep", name.lower()),
                        "type": "missing_dependency",
                        "title": f"Missing dependency: {name}",
                        "detail": (f"Fabric says installing {name} "
                                   f"({install.group('ver').strip()}) should fix this. "
                                   "It can be downloaded from Modrinth."),
                        "fix": "install_dependency",
                        "modid": name,
                    })
                continue

            action = re.match(
                r"(?P<verb>Remove|Replace|Update)\s+mod\s+'(?P<name>[^']+)'"
                r"(?:\s*\((?P<id>[^)]+)\))?", line, re.I)
            if action:
                mod = _resolve_mod(mods, action.group("id"), action.group("name"))
                if not mod:
                    continue
                if any(i.get("file") == mod["file"] for i in issues):
                    continue
                verb = action.group("verb").lower()
                if verb == "remove":
                    add({
                        "_key": ("solution", mod["file"]),
                        "type": "incompatible_mod",
                        "title": f"Incompatible mod: {_mod_label(mod, mod['file'])}",
                        "detail": ("Fabric says this mod can't be used as installed, so it "
                                   "has to be removed."),
                        "fix": "remove_mod",
                        "file": mod["file"],
                        "mod_name": _mod_label(mod, mod["file"]),
                    })
                else:
                    modid = mod.get("modid") or action.group("id") or action.group("name")
                    add({
                        "_key": ("solution", mod["file"]),
                        "type": "incompatible_mod",
                        "title": f"Wrong version: {_mod_label(mod, mod['file'])}",
                        "detail": ("Fabric says this mod needs to be replaced with a "
                                   "version that matches this instance. It can be "
                                   "downloaded from Modrinth."),
                        "fix": "update_mod",
                        "file": mod["file"],
                        "modid": modid,
                    })


def _fabric_incompatibilities(text: str, mods: list, issues: list, add):
    for match in _FABRIC_PAIR_RE.finditer(text):
        first = _resolve_mod(mods, match.group("aid"), match.group("a"))
        second = _resolve_mod(mods, match.group("bid"), match.group("b"))
        if first and second and first["file"] != second["file"]:
            if any(i.get("type") == "mod_conflict"
                   and {first["file"], second["file"]} <=
                   {o.get("file") for o in (i.get("options") or [])} for i in issues):
                continue
            options = [_mod_option(first), _mod_option(second)]
            add({
                "_key": ("pair", tuple(sorted(o["file"] for o in options))),
                "type": "mod_conflict",
                "title": "Two mods are incompatible",
                "detail": (f"{options[0]['label']} and {options[1]['label']} can't run "
                           "together. Pick the one you want to keep; the other will be "
                           "removed."),
                "fix": "choose_mod",
                "options": options,
            })
        else:
            only = first or second
            if only and not any(i.get("fix") == "remove_mod"
                                and i.get("file") == only["file"] for i in issues):
                add({
                    "_key": ("pairone", only["file"]),
                    "type": "incompatible_mod",
                    "title": f"Incompatible mod: {_mod_label(only, only['file'])}",
                    "detail": ("Fabric says this mod is incompatible with another mod "
                               "or with this game version."),
                    "fix": "remove_mod",
                    "file": only["file"],
                    "mod_name": _mod_label(only, only["file"]),
                })


def _fabric_game_incompat(text: str, mods: list, issues: list, add):
    for match in _FABRIC_GAME_RE.finditer(text):
        mod = _resolve_mod(mods, match.group("id"), match.group("name"))
        if not mod or any(i.get("file") == mod["file"] for i in issues):
            continue
        add({
            "_key": ("gameincompat", mod["file"]),
            "type": "incompatible_mod",
            "title": f"Mod incompatible: {_mod_label(mod, mod['file'])}",
            "detail": ("Fabric says this mod can't run with this Minecraft or loader "
                       "version. Removing it should let the game start."),
            "fix": "remove_mod",
            "file": mod["file"],
            "mod_name": _mod_label(mod, mod["file"]),
        })


def _failure_message_deps(text: str, mods: list, add):
    match = _FAILURE_RE.search(text)
    if not match:
        return
    line = match.group(1).strip()
    if FABRIC_DEP_RE.search(line) or FORGE_DEP_RE.search(line):
        return

    req = re.search(r"requires\s+(?P<what>[^\n]*?)\s+of\s+(?:mod\s+)?"
                    r"'?(?P<dep>[A-Za-z0-9_.\-]+)'?", line, re.I)
    if req:
        dep = req.group("dep").strip()
        what = (req.group("what") or "").strip()
    else:
        req = re.search(r"requires\s+'?(?P<dep>[A-Za-z0-9_.\-]+)'?", line, re.I)
        if not req:
            return
        dep = req.group("dep").strip()
        what = ""
    if dep.lower() in ("any", "a", "an", "the", "version", "later", "newer"):
        return

    who = re.match(r"(?:Mod\s+)?'?(?P<name>[^'(\s]+)'?(?:\s*\((?P<id>[^)]+)\))?",
                   line, re.I)
    requester_id = (who.group("id") or who.group("name")) if who else dep
    requester = _resolve_mod(mods, requester_id)
    label = _mod_label(requester, requester_id)

    if dep.lower() in GAME_IDS:
        add({
            "_key": ("game", dep.lower(), requester_id.lower()),
            "type": "incompatible_mod",
            "title": f"Mod incompatible: {label}",
            "detail": (f"{label} needs {dep} {what or 'another version'}, which isn't "
                       "installed."),
            "fix": "remove_mod" if requester else None,
            "file": requester["file"] if requester else None,
            "mod_name": label,
        })
    else:
        installed = _resolve_mod(mods, dep)
        if installed:
            add({
                "_key": ("depver", dep.lower()),
                "type": "incompatible_mod",
                "title": f"Outdated dependency: {_mod_label(installed, dep)}",
                "detail": (f"{label} needs {dep} {what or 'a different version'}. "
                           "Updating the dependency should fix the crash."),
                "fix": "update_mod",
                "file": installed["file"],
                "modid": dep,
                "requested_by": requester_id,
            })
        else:
            add({
                "_key": ("dep", dep.lower()),
                "type": "missing_dependency",
                "title": f"Missing dependency: {dep}",
                "detail": (f"{label} needs {dep}{f' {what}' if what else ''}. "
                           "It can be downloaded from Modrinth."),
                "fix": "install_dependency",
                "modid": dep,
                "requested_by": requester_id,
            })


def _duplicate_mods(text: str, mods: list, add):
    match = _DUP_RE.search(text)
    if not match:
        return
    segment = text[max(0, match.start() - 200): match.start() + 8000]
    files = []
    for name in _JAR_RE.findall(segment):
        if name not in files:
            files.append(name)
    known = [f for f in files if any(m["file"] == f for m in mods)]
    files = known or files
    if len(files) < 2:
        return

    groups = {}
    for name in files:
        mod = next((m for m in mods if m["file"] == name), None)
        key = ((mod.get("modid") or mod.get("name") or name) if mod else name).lower()
        groups.setdefault(key, []).append(mod or {"file": name, "name": name})

    made = False
    for group in groups.values():
        if len(group) < 2:
            continue
        made = True
        options = [_mod_option(m) for m in group]
        add({
            "_key": ("dup", tuple(sorted(o["file"] for o in options))),
            "type": "mod_conflict",
            "title": f"Duplicate mod: {options[0]['label']}",
            "detail": ("These files provide the same mod. Pick the one you want to "
                       "keep; the others will be removed."),
            "fix": "choose_mod",
            "options": options,
        })
    if not made:
        options = [_mod_option(next((m for m in mods if m["file"] == f),
                                    {"file": f, "name": f})) for f in files]
        add({
            "_key": ("dup", tuple(sorted(o["file"] for o in options))),
            "type": "mod_conflict",
            "title": "Duplicate mod files",
            "detail": ("The same mod is installed more than once. Pick the file you "
                       "want to keep; the others will be removed."),
            "fix": "choose_mod",
            "options": options,
        })


def _duplicate_modids(mods: list, issues: list, add):
    """Catch duplicates from the mods folder even when the log doesn't list them."""
    groups = {}
    for mod in mods:
        if mod["file"].lower().endswith(".disabled"):
            continue
        key = (mod.get("modid") or "").strip().lower()
        if key:
            groups.setdefault(key, []).append(mod)

    already = {o.get("file") for i in issues if i.get("type") == "mod_conflict"
               for o in (i.get("options") or [])}
    for group in groups.values():
        if len(group) < 2:
            continue
        files = {m["file"] for m in group}
        if files & already:
            continue
        options = [_mod_option(m) for m in group]
        add({
            "_key": ("dupmodid", tuple(sorted(files))),
            "type": "mod_conflict",
            "title": f"Duplicate mod: {options[0]['label']}",
            "detail": ("More than one file provides this mod. Pick the one you want to "
                       "keep; the others will be removed."),
            "fix": "choose_mod",
            "options": options,
        })


def _mixin_failures(text: str, mods: list, add):
    if "mixin" not in text.lower():
        return
    if not re.search(r"(mixin apply.*failed|InvalidMixinException|MixinTransformerError|"
                     r"mixin.*failed|Mixin apply failed)", text, re.I | re.S):
        return
    candidates = []
    for pattern in (
        r"Mixin apply for mod ([^\s]+) failed",
        r"Mixin apply for mod ([^\s]+)",
        r"mixin config (?:mixins?\.)?([A-Za-z0-9_.\-]+)\.json",
        r"mixins?\.([A-Za-z0-9_.\-]+)\.json",
        r"Mixin config ([A-Za-z0-9_.\-]+) requires",
    ):
        candidates += re.findall(pattern, text, re.I)

    resolved = {}
    for name in candidates:
        mod = _resolve_mod(mods, name)
        if mod:
            resolved[mod["file"]] = mod
    for mod in resolved.values():
        add({
            "_key": ("mixin", mod["file"]),
            "type": "mod_crash",
            "title": f"Mod crashed while loading: {_mod_label(mod, mod['file'])}",
            "detail": ("A mixin from this mod failed to apply. That usually means it "
                       "doesn't support this Minecraft or loader version."),
            "fix": "remove_mod",
            "file": mod["file"],
            "mod_name": _mod_label(mod, mod["file"]),
        })


def _explicit_mod_failures(text: str, mods: list, add):
    names = []
    patterns = (
        r"(?:Mod file|mod file)\s+\S*?([A-Za-z0-9_.+\-]+\.jar)\s+"
        r"(?:failed|is invalid|is missing|is not valid|could not be loaded)",
        r"Failed to load mod(?: file)?\s+['\"]?([A-Za-z0-9_.+\-]+)",
        r"The mod ['\"]([^'\"]+)['\"] (?:has failed|failed) to load",
        r"Mod ([A-Za-z0-9_.+\-]+) has failed to load",
        r"Could not load mod\s+['\"]?([A-Za-z0-9_.+\-]+)",
    )
    for pattern in patterns:
        names += re.findall(pattern, text, re.I)
    for chunk in re.findall(r"Failed to load mods?:\s*([^\r\n]+)", text, re.I):
        names += [part.strip(" '\"") for part in chunk.split(",") if part.strip()]

    for name in names:
        mod = _resolve_mod(mods, name)
        if not mod:
            continue
        add({
            "_key": ("load", mod["file"]),
            "type": "mod_crash",
            "title": f"Mod failed to load: {_mod_label(mod, mod['file'])}",
            "detail": "Minecraft couldn't load this mod, so it has to be removed.",
            "fix": "remove_mod",
            "file": mod["file"],
            "mod_name": _mod_label(mod, mod["file"]),
        })


def _corrupt_jars(text: str, mods: list, add):
    match = _BAD_JAR_RE.search(text)
    if not match:
        return
    segment = text[max(0, match.start() - 400): match.start() + 2000]
    found = {}
    for name in _JAR_RE.findall(segment):
        mod = next((m for m in mods if m["file"] == name), None)
        if mod:
            found[mod["file"]] = mod
    for mod in found.values():
        add({
            "_key": ("badjar", mod["file"]),
            "type": "corrupt_mod",
            "title": f"Broken mod file: {_mod_label(mod, mod['file'])}",
            "detail": ("This mod's jar is damaged or isn't a valid mod, so Minecraft "
                       "can't read it. Removing it lets the game start again."),
            "fix": "remove_mod",
            "file": mod["file"],
            "mod_name": _mod_label(mod, mod["file"]),
        })


def _java_problems(text: str, add):
    if re.search(r"UnsupportedClassVersionError|class file version|"
                 r"compiled by a more recent version of the Java Runtime", text, re.I):
        add({
            "_key": ("java",),
            "type": "java",
            "title": "Wrong Java version",
            "detail": ("Minecraft is running on an older Java than this version needs. "
                       "Clear the custom Java path in Settings so Crit Launcher can "
                       "pick the right one."),
            "fix": None,
        })


def _memory_problems(text: str, add):
    if re.search(r"OutOfMemoryError|GC overhead limit exceeded|Java heap space", text, re.I):
        add({
            "_key": ("memory",),
            "type": "performance",
            "title": "The game ran out of memory",
            "detail": ("Minecraft used all the memory it was allowed. Raise the maximum "
                       "memory in Settings, then try again."),
            "fix": None,
        })


def _native_crashes(text: str, exit_code, add):
    code = (int(exit_code) & 0xFFFFFFFF) if isinstance(exit_code, int) else None
    if code in _NATIVE_CODES or re.search(
            r"EXCEPTION_ACCESS_VIOLATION|EXCEPTION_ILLEGAL_INSTRUCTION|"
            r"A fatal error has been detected by the Java Runtime Environment|SIGSEGV",
            text, re.I):
        add({
            "_key": ("native",),
            "type": "performance",
            "title": "The game crashed suddenly",
            "detail": ("Minecraft stopped at a low level, which is usually a graphics "
                       "driver problem, a hardware problem or running out of resources "
                       "rather than a broken mod."),
            "fix": None,
        })


def _graphics_problems(text: str, add):
    if not _GRAPHICS_RE.search(text):
        return
    add({
        "_key": ("graphics",),
        "type": "performance",
        "title": "The graphics driver failed",
        "detail": ("Minecraft couldn't create its window or use your graphics card. "
                   "Update your graphics driver and make sure the game is using the "
                   "right GPU. This isn't caused by a mod."),
        "fix": None,
    })


def _broken_install(text: str, add):
    match = re.search(r"(?:NoClassDefFoundError|ClassNotFoundException):\s*([^\s]+)", text)
    if match and re.match(r"^(net/minecraft|net/minecraftforge|com/mojang|org/lwjgl)",
                          match.group(1)):
        add({
            "_key": ("install",),
            "type": "corrupt_instance",
            "title": "Game files are missing",
            "detail": ("Minecraft couldn't find part of its own files. Repairing the "
                       "instance downloads them again without touching your mods."),
            "fix": "repair_instance",
        })
    elif re.search(r"Could not find or load main class|java\.lang\.module\.FindException", text):
        add({
            "_key": ("install",),
            "type": "corrupt_instance",
            "title": "Game files are missing",
            "detail": ("The game's own files look incomplete. Repairing the instance "
                       "downloads them again without touching your mods."),
            "fix": "repair_instance",
        })


def _suspected_mods(text: str, mods: list, issues: list, add):
    match = _SUSPECT_RE.search(text)
    if not match:
        return
    raw = match.group(1).strip()
    if not raw or raw.lower().startswith("none"):
        return
    if any(i.get("type") in ("missing_dependency", "incompatible_mod",
                             "mod_crash", "mod_conflict") for i in issues):
        return

    parsed = []
    for part in raw.split(","):
        part = part.strip()
        if not part:
            continue
        inner = re.match(r"(.+?)\s*\(([^)]+)\)$", part)
        if inner:
            parsed.append((inner.group(1).strip(), inner.group(2).strip()))
        else:
            parsed.append((part, part))

    resolved = []
    for name, modid in parsed:
        mod = _resolve_mod(mods, modid, name)
        if mod and all(m["file"] != mod["file"] for m in resolved):
            resolved.append(mod)

    if len(resolved) == 1:
        mod = resolved[0]
        add({
            "_key": ("suspect", mod["file"]),
            "type": "mod_crash",
            "title": f"Mod crashed: {_mod_label(mod, mod['file'])}",
            "detail": ("Minecraft's crash report points at this mod. Removing it is "
                       "the quickest way to get the game running again."),
            "fix": "remove_mod",
            "file": mod["file"],
            "mod_name": _mod_label(mod, mod["file"]),
        })
    elif len(resolved) >= 2:
        options = [_mod_option(m) for m in resolved[:4]]
        names = ", ".join(o["label"] for o in options)
        add({
            "_key": ("suspect", tuple(sorted(o["file"] for o in options))),
            "type": "mod_conflict",
            "title": "Two mods are fighting",
            "detail": (f"The crash report points at {names}. Pick the one you want to "
                       "keep; the other will be removed."),
            "fix": "choose_mod",
            "options": options,
        })
    elif not issues:
        names = ", ".join(name for name, _ in parsed)
        add({
            "_key": ("suspects",),
            "type": "mod_crash",
            "title": "A mod may be causing this",
            "detail": f"The crash report points at {names}, but the mod file couldn't be matched.",
            "fix": None,
        })


def _stack_trace_mods(text: str, mods: list, issues: list, add):
    if issues:
        return
    found = {}
    for class_name in _FRAME_RE.findall(text)[:120]:
        parts = {p for p in re.split(r"[.$]", class_name.lower()) if p}
        for mod in mods:
            modid = (mod.get("modid") or "").lower()
            if len(modid) > 2 and modid in parts:
                found[mod["file"]] = mod
    if len(found) == 1:
        mod = next(iter(found.values()))
        add({
            "_key": ("frame", mod["file"]),
            "type": "mod_crash",
            "title": f"Mod crashed: {_mod_label(mod, mod['file'])}",
            "detail": ("The crash happened inside this mod's code. Removing it should "
                       "let the game start again."),
            "fix": "remove_mod",
            "file": mod["file"],
            "mod_name": _mod_label(mod, mod["file"]),
        })


def _crash_detail(text: str) -> str:
    for pattern in (_FAILURE_RE, _DESC_RE):
        match = pattern.search(text)
        if match and match.group(1).strip():
            prefix = "Minecraft crashed while: " if pattern is _DESC_RE else ""
            return (prefix + match.group(1).strip())[:400]

    lines = [line.strip() for line in text.splitlines()]
    for line in lines:
        if line.startswith("at "):
            continue
        match = re.match(r"^(?:[A-Za-z0-9_.$]+(?:Exception|Error)):\s*(.+)$", line)
        if match:
            return match.group(1).strip()[:400]

    for line in lines:
        if re.match(r"^(Incompatible mods found!|Mod resolution|"
                    r"Missing or unsupported mandatory dependencies|"
                    r"Duplicate mods found|Found duplicate mods|"
                    r"A fatal error has been detected)", line, re.I):
            return line[:400]

    for line in reversed(lines):
        if not line or len(line) > 500 or line.startswith("at "):
            continue
        if re.search(r"(Exception|Error|Caused by:)", line):
            return line
    return "The game closed unexpectedly. Check the console for the full log."
