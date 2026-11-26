# Bedrock catalog parsing. Run from the project root: python -m unittest discover -s tests
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from crit_launcher.bedrock.catalog import (  # noqa: E402
    game_to_package_version,
    parse_community_uwp,
    parse_gdk_links,
)


class BedrockCatalogTests(unittest.TestCase):
    def test_gdk_entry_becomes_a_release(self):
        identity = "Microsoft.MinecraftUWP_1.26.6501.0_x64__8wekyb3d8bbwe"
        text = json.dumps({"release": {
            "1.26.65.1": [
                f"http://assets1.xboxlive.com/{identity}.msixvc",
                f"http://assets2.xboxlive.com/{identity}.msixvc",
            ],
        }})
        releases = parse_gdk_links(text)
        self.assertEqual(len(releases), 1)
        self.assertEqual(releases[0].name, "1.26.65.1")
        self.assertEqual(releases[0].kind, "gdk")
        self.assertEqual(releases[0].identity, identity)
        self.assertEqual(len(releases[0].urls), 2)

    def test_gdk_entry_with_mismatched_package_version_is_skipped(self):
        identity = "Microsoft.MinecraftUWP_1.26.6501.0_x64__8wekyb3d8bbwe"
        text = json.dumps({"release": {
            "1.26.65.2": [f"http://assets1.xboxlive.com/{identity}.msixvc"],
        }})
        self.assertEqual(parse_gdk_links(text), [])

    def test_uwp_list_only_keeps_release_packages(self):
        text = json.dumps([
            ["1.21.100.6", "release-id", 0, "x64", 0],
            ["1.21.100.7", "beta-id", 1, "x64", 0],
        ])
        releases = parse_community_uwp(text)
        self.assertEqual([release.name for release in releases], ["1.21.100.6"])
        self.assertEqual(releases[0].update_id, "release-id")

    def test_package_version_round_trip_values(self):
        self.assertEqual(game_to_package_version("1.26.65.1"), (1, 26, 6501, 0))


if __name__ == "__main__":
    unittest.main()
