"""Small thread-safe JSON file store used for settings, accounts and indexes."""

import json
import os
import tempfile
import threading
from pathlib import Path


class JsonStore:
    def __init__(self, path: Path, default, sensitive: bool = False):
        self.path = path
        self._lock = threading.RLock()
        self._default = default
        self._sensitive = sensitive
        self._prepared = False

    def _prepare(self):
        """First use: tighten permissions on an existing sensitive file, or create the file.

        Done lazily (not in __init__) so importing the launcher never touches the data folder.
        """
        if self._prepared:
            return
        self._prepared = True
        if self.path.exists() and self._sensitive and os.name == "posix":
            os.chmod(self.path, 0o600)
        if not self.path.exists():
            self._write(self._default)

    def _write(self, data):
        fd, tmp_name = tempfile.mkstemp(
            prefix=f".{self.path.name}.",
            suffix=".tmp",
            dir=self.path.parent,
        )
        tmp = Path(tmp_name)

        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)

            os.replace(tmp, self.path)

            if self._sensitive and os.name == "posix":
                os.chmod(self.path, 0o600)
        finally:
            tmp.unlink(missing_ok=True)

    def _read(self):
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                return json.load(f)

        except json.JSONDecodeError:
            # Find backup name that does not already exist
            backup = self.path.with_name(self.path.name + ".corrupt")
            number = 1

            while backup.exists():
                backup = self.path.with_name(
                    f"{self.path.name}.corrupt.{number}"
                )
                number += 1


            while True:
                try:
                    os.link(self.path, backup)
                    break
                except FileExistsError:
                    backup = self.path.with_name(
                        f"{self.path.name}.corrupt.{number}"
                    )
                    number += 1

            # Keep macOS/Linux account backups private
            if self._sensitive and os.name == "posix":
                os.chmod(backup, 0o600)

            # Create fresh JSON file using default data
            default = json.loads(json.dumps(self._default))

            self._write(default)

            return default

        except FileNotFoundError:
            return json.loads(json.dumps(self._default))

    def read(self):
        with self._lock:
            self._prepare()
            return self._read()

    def write(self, data):
        with self._lock:
            self._prepare()
            self._read()
            self._write(data)

    def update(self, mutator):
        with self._lock:
            self._prepare()
            data = self._read()
            result = mutator(data)
            if result is not None:
                data = result
            self._write(data)
            return data
