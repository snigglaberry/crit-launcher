"""Running the e4mc relay helper and reading the public address it assigns."""

import logging
import os
import subprocess
import threading
import time
from pathlib import Path

from ..config.paths import E4MC_DIR, RUNTIME_DIR
from ..config.settings import settings_store
from ..core.fsutil import file_sha256
from ..core.http import http_download
from ..core.logs import log_exception
from ..core.system import CREATE_NO_WINDOW
from ..instances.store import instance_dir, instance_game_dir
from ..minecraft.java import console_java, find_system_javas, managed_java_path, probe_java
from .constants import (
    E4MC_BIAT_FILENAME,
    E4MC_BIAT_SHA256,
    E4MC_BIAT_URL,
    E4MC_DOMAIN_RE,
    E4MC_MIN_JAVA,
    LAN_PORT_PATTERNS,
)

_DOMAIN_TIMEOUT = 50.0
_LOG_TAIL = 512 * 1024
_HELPER_LOCK = threading.Lock()


def _java_candidates():
    configured = (settings_store.read().get("java_path") or "").strip()
    if configured:
        yield configured
    # Runtimes the launcher installed for Minecraft are usually the right version.
    if RUNTIME_DIR.is_dir():
        try:
            for child in RUNTIME_DIR.iterdir():
                if not child.is_dir() or child.name == "jre-legacy":
                    continue
                path = managed_java_path(child.name)
                if path:
                    yield path
        except OSError:
            pass
    yield from find_system_javas()


def find_java():
    """Return a Java 11+ executable, or None."""
    seen = set()
    best = None
    for candidate in _java_candidates():
        exe = console_java(str(candidate))
        if exe in seen:
            continue
        seen.add(exe)
        major, _ = probe_java(exe)
        if major is None or major < E4MC_MIN_JAVA:
            continue
        if best is None or major > best[0]:
            best = (major, exe)
    return best[1] if best else None


def ensure_helper() -> Path:
    """Download e4mcbiat once and verify the pinned checksum."""
    E4MC_DIR.mkdir(parents=True, exist_ok=True)
    jar = E4MC_DIR / E4MC_BIAT_FILENAME
    if jar.is_file():
        try:
            if file_sha256(jar) == E4MC_BIAT_SHA256:
                return jar
        except OSError:
            pass
        jar.unlink(missing_ok=True)
    try:
        http_download(E4MC_BIAT_URL, jar, expected_sha1=E4MC_BIAT_SHA256)
    except Exception as exc:
        log_exception("e4mc download")
        raise RuntimeError("Couldn't download e4mc. Check your internet connection.") from exc
    return jar


def detect_lan_port(instance_name, since=0) -> int | None:
    """Read the port a singleplayer world announced when it was opened to LAN."""
    # The launcher truncates launch_output.log for each new Java process. The
    # fallback game log can already exist before this run.
    paths = (
        (instance_dir(instance_name) / "launch_output.log", 0),
        (instance_game_dir(instance_name) / "logs" / "latest.log", since),
    )
    for path, minimum_mtime in paths:
        try:
            with open(path, "rb") as f:
                if os.fstat(f.fileno()).st_mtime < minimum_mtime:
                    continue
                f.seek(0, os.SEEK_END)
                f.seek(max(0, f.tell() - _LOG_TAIL))
                text = f.read(_LOG_TAIL).decode("utf-8", "replace")
        except OSError:
            continue
        for line in reversed(text.splitlines()):
            if "Stopping server" in line or "Stopping integrated server" in line:
                return None
            for pattern in LAN_PORT_PATTERNS:
                match = pattern.search(line)
                if match and len(match.group(1)) <= 5:
                    port = int(match.group(1))
                    if 1 <= port <= 65535:
                        return port
    return None


class E4mcSession:
    """One relay helper process at a time."""

    def __init__(self, on_change):
        self._on_change = on_change
        self._lock = threading.Lock()
        self._proc = None
        self._domain = None
        self._port = None
        self._target = None
        self._starting = False
        self._running = False
        self._message = None
        self._error = None
        self._last_line = ""
        self._stop = threading.Event()
        self._generation = 0
        self._revision = 0

    # -- state -----------------------------------------------------------
    def status(self) -> dict:
        with self._lock:
            return {
                "running": self._running,
                "starting": self._starting,
                "domain": self._domain if self._running else None,
                "port": self._port,
                "target": {"kind": self._target[0], "name": self._target[1]} if self._target else None,
                "message": self._message,
                "error": self._error,
                "revision": self._revision,
            }

    def _publish(self):
        try:
            self._on_change(self.status())
        except Exception:
            log_exception("e4mc change listener")

    def _current(self, generation) -> bool:
        with self._lock:
            return self._generation == generation

    def _set_message(self, message, generation):
        with self._lock:
            if self._generation != generation:
                return
            self._message = message
            self._revision += 1
        self._publish()

    def _fail(self, message, generation):
        with self._lock:
            if self._generation != generation:
                return
            self._starting = False
            self._running = False
            self._domain = None
            self._port = None
            self._target = None
            self._message = None
            self._error = message
            self._proc = None
            self._revision += 1
        self._publish()

    # -- lifecycle -------------------------------------------------------
    def start(self, port, target=None, is_running=None) -> tuple[bool, str | None]:
        try:
            port = int(port)
        except (TypeError, ValueError):
            return False, "Choose a port between 1 and 65535."
        if not 1 <= port <= 65535:
            return False, "Choose a port between 1 and 65535."
        with self._lock:
            if is_running is not None and not is_running():
                return False, "The game is no longer running."
            if self._starting or self._running:
                if self._target == target and self._port == port:
                    return True, None
                other = self._target[1] if self._target else f"port {self._port}"
                return False, f"Stop forwarding {other} before sharing another game."
            self._generation += 1
            generation = self._generation
            stop_event = threading.Event()
            self._stop = stop_event
            self._starting = True
            self._domain = None
            self._port = port
            self._target = target
            self._message = "Preparing e4mc..."
            self._error = None
            self._last_line = ""
            self._revision += 1
        self._publish()
        threading.Thread(target=self._run, args=(port, generation, stop_event), daemon=True).start()
        return True, None

    def stop(self, target=None) -> bool:
        with self._lock:
            if target is not None and target != self._target:
                return False
            proc = self._proc
            active = self._starting or self._running
            self._stop.set()
            self._generation += 1
            self._starting = False
            self._running = False
            self._domain = None
            self._port = None
            self._target = None
            self._message = None
            self._error = None
            self._proc = None
            self._revision += 1
        if proc is not None:
            self._terminate(proc)
        self._publish()
        return active or proc is not None

    def _run(self, port, generation, stop_event):
        proc = None

        def stopped():
            return stop_event.is_set()

        try:
            java = find_java()
            if not java:
                raise RuntimeError(
                    "e4mc needs Java 11 or newer. Install Java or set its path in Settings.")
            if stopped() or not self._current(generation):
                return

            self._set_message("Downloading e4mc...", generation)
            with _HELPER_LOCK:
                if stopped():
                    return
                jar = ensure_helper()
            if stopped() or not self._current(generation):
                return

            self._set_message("Connecting to e4mc...", generation)
            with self._lock:
                if stopped() or self._generation != generation:
                    return
                proc = subprocess.Popen(
                    [java, "-jar", str(jar), "nogui", f"port={port}"],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    creationflags=CREATE_NO_WINDOW,
                    shell=False,
                )
                self._proc = proc

            ready = threading.Event()
            threading.Thread(
                target=self._read_output, args=(proc, ready, generation), daemon=True).start()

            deadline = time.monotonic() + _DOMAIN_TIMEOUT
            while time.monotonic() < deadline:
                if ready.wait(0.4):
                    break
                if proc.poll() is not None or stopped():
                    break

            with self._lock:
                if stopped() or self._generation != generation:
                    return
                code = proc.poll()
                if not self._domain or code is not None:
                    reason = f" (exit code {code})" if code is not None else ""
                    if self._last_line:
                        reason += f": {self._last_line}"
                    raise RuntimeError("e4mc couldn't connect to the relay" + reason + ".")
                self._starting = False
                self._running = True
                self._message = None
                self._error = None
                self._revision += 1
            self._publish()

            code = proc.wait()
            if not stopped():
                raise RuntimeError(f"e4mc stopped unexpectedly (exit code {code}).")
        except Exception as exc:
            if not stopped():
                log_exception("e4mc session")
                if proc is not None and proc.poll() is None:
                    self._terminate(proc)
                self._fail(str(exc) or "e4mc couldn't start.", generation)

    def _read_output(self, proc, ready, generation):
        try:
            for raw in iter(proc.stdout.readline, b""):
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                if not line:
                    continue
                logging.info("e4mc: %s", line)
                match = E4MC_DOMAIN_RE.search(line)
                with self._lock:
                    if self._generation != generation:
                        return
                    self._last_line = line[-300:]
                    if match and self._starting:
                        self._domain = match.group(1)
                        ready.set()
        except Exception:
            log_exception("e4mc output reader")

    @staticmethod
    def _terminate(proc):
        try:
            if proc.stdin is not None:
                try:
                    proc.stdin.write(b"stop\n")
                    proc.stdin.flush()
                except (OSError, ValueError):
                    pass
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        except OSError:
            pass
