# Guards how library lists are turned into download entries.
# Run from the project root:  python3 -m unittest discover -s tests
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from crit_launcher.minecraft.libraries import collect_library_entries  # noqa: E402


def lib(name):
    return {"name": name, "url": "https://maven.minecraftforge.net/"}


class LibraryEntryTests(unittest.TestCase):
    def test_launch_entries_keep_one_jar_per_artifact(self):
        entries, _ = collect_library_entries([lib("a.b:c:1.0"), lib("a.b:c:2.0")])
        self.assertEqual([e["path"] for e in entries], ["a/b/c/1.0/c-1.0.jar"])

    def test_installer_entries_keep_each_version(self):
        entries, _ = collect_library_entries(
            [lib("net.minecraftforge:srgutils:0.6.2"),
             lib("net.minecraftforge:srgutils:0.6.6")], dedupe=False)
        self.assertEqual(
            [e["path"] for e in entries],
            ["net/minecraftforge/srgutils/0.6.2/srgutils-0.6.2.jar",
             "net/minecraftforge/srgutils/0.6.6/srgutils-0.6.6.jar"])

    def test_installer_entries_skip_exact_duplicates(self):
        entries, _ = collect_library_entries(
            [lib("net.minecraftforge:srgutils:0.6.6"),
             lib("net.minecraftforge:srgutils:0.6.6")], dedupe=False)
        self.assertEqual([e["path"] for e in entries],
                         ["net/minecraftforge/srgutils/0.6.6/srgutils-0.6.6.jar"])


if __name__ == "__main__":
    unittest.main()
