import json

from crit_launcher.core.jsonstore import JsonStore


def test_corrupt_json_creates_backup(tmp_path):
    path = tmp_path / "accounts.json"
    corrupted_content = '{"broken": '
    path.write_text(corrupted_content, encoding="utf-8")
    store = JsonStore(path, [], sensitive=True)
    result = store.read()
    assert result == []
    backup = tmp_path / "accounts.json.corrupt"
    assert backup.exists()
    assert backup.read_text(encoding="utf-8") == corrupted_content
    assert json.loads(path.read_text(encoding="utf-8")) == []

def test_existing_backup_is_not_overwritten(tmp_path):
    path = tmp_path / "accounts.json"
    old_backup = tmp_path / "accounts.json.corrupt"
    path.write_text('{"broken": ', encoding="utf-8")
    old_backup.write_text("important old backup", encoding="utf-8")
    store = JsonStore(path, [], sensitive=True)
    assert store.read() == []
    assert old_backup.read_text(encoding="utf-8") == "important old backup"
    new_backup = tmp_path / "accounts.json.corrupt.1"
    assert new_backup.exists()
    assert new_backup.read_text(encoding="utf-8") == '{"broken": '

def test_backup_failure_preserves_original(tmp_path, monkeypatch):
    import os
    path = tmp_path / "accounts.json"
    corrupted_content = '{"broken": '
    path.write_text(corrupted_content, encoding="utf-8")
    def fail_link(source, destination):
        raise PermissionError("Backup creation blocked")
    monkeypatch.setattr(os, "link", fail_link)
    store = JsonStore(path, [], sensitive=True)
    import pytest
    with pytest.raises(PermissionError):
        store.read()
    assert path.read_text(encoding="utf-8") == corrupted_content
    assert not (tmp_path / "accounts.json.corrupt").exists()


def test_valid_json_does_not_create_backup(tmp_path):
    path = tmp_path / "settings.json"
    path.write_text('{"theme": "dark"}', encoding="utf-8")

    store = JsonStore(path, {"theme": "light"})

    assert store.read() == {"theme": "dark"}
    assert not (tmp_path / "settings.json.corrupt").exists()


def test_missing_file_uses_defaults(tmp_path):
    path = tmp_path / "settings.json"

    store = JsonStore(path, {"theme": "light"})

    assert store.read() == {"theme": "light"}
    assert json.loads(path.read_text(encoding="utf-8")) == {
        "theme": "light"
    }


def test_recovery_only_happens_once(tmp_path):
    path = tmp_path / "accounts.json"
    path.write_text('{"broken": ', encoding="utf-8")

    store = JsonStore(path, [], sensitive=True)

    assert store.read() == []
    assert store.read() == []

    assert (tmp_path / "accounts.json.corrupt").exists()
    assert not (tmp_path / "accounts.json.corrupt.1").exists()


def test_update_after_recovery(tmp_path):
    path = tmp_path / "accounts.json"
    path.write_text('{"broken": ', encoding="utf-8")

    store = JsonStore(path, [], sensitive=True)

    result = store.update(
        lambda accounts: accounts + [{"username": "TestPlayer"}]
    )

    assert result == [{"username": "TestPlayer"}]
    assert json.loads(path.read_text(encoding="utf-8")) == result
    assert (tmp_path / "accounts.json.corrupt").exists()


def test_multiple_existing_backups_are_preserved(tmp_path):
    path = tmp_path / "accounts.json"
    path.write_text('{"broken": ', encoding="utf-8")

    backup_0 = tmp_path / "accounts.json.corrupt"
    backup_1 = tmp_path / "accounts.json.corrupt.1"

    backup_0.write_text("first backup", encoding="utf-8")
    backup_1.write_text("second backup", encoding="utf-8")

    store = JsonStore(path, [], sensitive=True)
    assert store.read() == []

    assert backup_0.read_text(encoding="utf-8") == "first backup"
    assert backup_1.read_text(encoding="utf-8") == "second backup"

    backup_2 = tmp_path / "accounts.json.corrupt.2"
    assert backup_2.read_text(encoding="utf-8") == '{"broken": '


def test_replacement_failure_preserves_original_and_backup(
    tmp_path, monkeypatch
):
    import os
    import pytest

    path = tmp_path / "accounts.json"
    corrupted_content = '{"broken": '
    path.write_text(corrupted_content, encoding="utf-8")

    real_replace = os.replace

    def fail_replace(source, destination):
        if os.fspath(destination) == os.fspath(path):
            raise PermissionError("Replacement blocked")
        return real_replace(source, destination)

    monkeypatch.setattr(os, "replace", fail_replace)

    store = JsonStore(path, [], sensitive=True)

    with pytest.raises(PermissionError):
        store.read()

    assert path.read_text(encoding="utf-8") == corrupted_content
    assert (
        tmp_path / "accounts.json.corrupt"
    ).read_text(encoding="utf-8") == corrupted_content


def test_backup_name_collision_during_link(tmp_path, monkeypatch):
    import os

    path = tmp_path / "accounts.json"
    path.write_text('{"broken": ', encoding="utf-8")

    first_backup = tmp_path / "accounts.json.corrupt"
    real_link = os.link
    attempts = 0

    def simulate_collision(source, destination):
        nonlocal attempts
        attempts += 1

        if attempts == 1:
            first_backup.write_text(
                "another backup", encoding="utf-8"
            )
            raise FileExistsError("Backup name already taken")

        return real_link(source, destination)

    monkeypatch.setattr(os, "link", simulate_collision)

    store = JsonStore(path, [], sensitive=True)
    assert store.read() == []

    assert first_backup.read_text(encoding="utf-8") == "another backup"
    assert (
        tmp_path / "accounts.json.corrupt.1"
    ).read_text(encoding="utf-8") == '{"broken": '


def test_sensitive_recovery_files_are_private(tmp_path):
    import os
    import stat
    import pytest

    if os.name != "posix":
        pytest.skip("POSIX file permissions only")

    path = tmp_path / "accounts.json"
    path.write_text('{"broken": ', encoding="utf-8")

    store = JsonStore(path, [], sensitive=True)
    assert store.read() == []

    backup = tmp_path / "accounts.json.corrupt"

    assert stat.S_IMODE(path.stat().st_mode) == 0o600
    assert stat.S_IMODE(backup.stat().st_mode) == 0o600
