
from pathlib import Path
from tempfile import TemporaryDirectory

from crit_launcher.core.jsonstore import JsonStore


def test_corrupted_json():
    with TemporaryDirectory() as folder:
        path = Path(folder) / "accounts.json"

        # Create a deliberately corrupted JSON file
        path.write_text("{broken json", encoding="utf-8")

        store = JsonStore(path, [], sensitive=True)

        # Try updating the corrupted file
        try:
            store.update(
                lambda accounts: accounts.append({"name": "Test"})
            )
        except ValueError:
            print("PASS: Corrupted JSON was detected")
        else:
            raise AssertionError(
                "FAIL: Corrupted JSON was not detected"
            )

        # Check that the original file wasn't overwritten
        assert path.read_text(encoding="utf-8") == "{broken json"
        print("PASS: Original file was preserved")


def test_valid_json():
    with TemporaryDirectory() as folder:
        path = Path(folder) / "accounts.json"

        store = JsonStore(path, [])

        store.update(
            lambda accounts: accounts.append({"name": "Test"})
        )

        assert store.read() == [{"name": "Test"}]
        print("PASS: Valid JSON still works")


if __name__ == "__main__":
    test_corrupted_json()
    test_valid_json()

    print("\nAll 3 checks passed!")
