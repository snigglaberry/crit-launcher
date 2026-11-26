# Guards the crash log parser that feeds the crash popup.
# Run from the project root:  python3 -m unittest discover -s tests
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from crit_launcher.launch.crash_report import (  # noqa: E402
    _duplicate_mods,
    _fabric_dependencies,
    _forge_dependencies,
    _memory_problems,
    finalize_report,
    looks_like_crash,
)


def collector():
    issues = []

    def add(issue):
        issue.pop("_key", None)
        issue.setdefault("fix", None)
        issue.setdefault("fixed", False)
        issue["id"] = f"crash-{len(issues) + 1}"
        issues.append(issue)

    return issues, add


class CrashParserTests(unittest.TestCase):
    def test_forge_missing_dependency(self):
        issues, add = collector()
        _forge_dependencies(
            "Missing or unsupported mandatory dependencies:\n"
            "\tMod ID: 'geckolib', Requested by: 'somemod', "
            "Expected range: '[4.0,)', Actual version: '[MISSING]'",
            [], add)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]["type"], "missing_dependency")
        self.assertEqual(issues[0]["fix"], "install_dependency")
        self.assertEqual(issues[0]["modid"], "geckolib")

    def test_forge_wrong_minecraft_version(self):
        issues, add = collector()
        _forge_dependencies(
            "Mod ID: 'minecraft', Requested by: 'somemod', "
            "Expected range: '[1.20.1,1.21)', Actual version: '1.21.1'",
            [], add)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]["type"], "incompatible_mod")

    def test_fabric_missing_dependency(self):
        issues, add = collector()
        _fabric_dependencies(
            "\t - Mod 'Some Mod' (somemod) 1.0 requires any version of "
            "'cloth-config', which is missing!",
            [], add)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]["type"], "missing_dependency")
        self.assertEqual(issues[0]["modid"], "cloth-config")

    def test_fabric_wrong_minecraft_version(self):
        issues, add = collector()
        _fabric_dependencies(
            "\t - Mod 'Some Mod' (somemod) 1.0 requires version 1.20.1 of "
            "minecraft, which is missing!",
            [], add)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]["type"], "incompatible_mod")

    def test_duplicate_mods_offer_a_choice(self):
        issues, add = collector()
        mods = [
            {"file": "foo-1.0.jar", "name": "Foo", "version": "1.0", "modid": "foo"},
            {"file": "foo-2.0.jar", "name": "Foo", "version": "2.0", "modid": "foo"},
        ]
        _duplicate_mods("Duplicate mods found!\n\tfoo-1.0.jar\n\tfoo-2.0.jar", mods, add)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]["type"], "mod_conflict")
        self.assertEqual(issues[0]["fix"], "choose_mod")
        self.assertEqual(len(issues[0]["options"]), 2)

    def test_out_of_memory_is_not_fixable(self):
        issues, add = collector()
        _memory_problems("java.lang.OutOfMemoryError: Java heap space", add)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]["type"], "performance")
        self.assertIsNone(issues[0]["fix"])

    def test_finalize_counts_fixable_issues(self):
        report = finalize_report({"issues": [
            {"id": "a", "title": "Missing dependency", "fix": "install_dependency"},
            {"id": "b", "title": "Duplicate mod", "fix": "choose_mod"},
            {"id": "c", "title": "Out of memory", "fix": None},
        ]})
        self.assertTrue(report["can_fix_all"])
        self.assertEqual(report["auto_fixable"], 1)
        self.assertEqual(report["needs_choice"], 1)

    def test_crash_markers(self):
        self.assertTrue(looks_like_crash("---- Minecraft Crash Report ----"))
        self.assertTrue(looks_like_crash("Mod resolution encountered an incompatible mod set!"))
        self.assertFalse(looks_like_crash("Stopping!"))


if __name__ == "__main__":
    unittest.main()
