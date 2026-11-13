<<<<<<< HEAD

=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
import json
import os
import socket
import struct
import threading
import time

<<<<<<< HEAD

=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
OP_HANDSHAKE = 0
OP_FRAME = 1


class _Pipe:
<<<<<<< HEAD
    """Windows Discord IPC named-pipe connection."""
=======
    """Named pipe dressed up as a socket. Windows only; Discord uses
    \\\\?\\pipe\\discord-ipc-N there instead of a Unix domain socket."""
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c

    def __init__(self, path):
        self._fh = open(path, "r+b", buffering=0)

<<<<<<< HEAD
    def settimeout(self, seconds):
        # Named pipes use blocking file I/O.
=======
    def settimeout(self, _seconds):
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
        pass

    def sendall(self, data):
        self._fh.write(data)

    def recv(self, size):
        return self._fh.read(size)

    def close(self):
        self._fh.close()


def _candidates():
<<<<<<< HEAD
    """Discover Discord IPC endpoints for the current platform."""

    if os.name == "nt":
        for n in range(10):
            yield "pipe", "\\\\?\\pipe\\discord-ipc-%d" % n
        return

    seen = set()

    directories = [
        os.environ.get("XDG_RUNTIME_DIR"),
        os.environ.get("TMPDIR"),
        os.environ.get("TEMP"),
        os.environ.get("TMP"),
        "/tmp",
    ]

    for directory in directories:
        if not directory:
            continue

        for n in range(10):
            path = os.path.join(directory, "discord-ipc-%d" % n)

            if path not in seen:
                seen.add(path)
                yield "unix", path


class Presence:
    """Cross-platform Discord Rich Presence client."""

    def __init__(self, client_id, log=None):
        self.client_id = str(client_id)
        self.log = log or (lambda *a: None)

        self.asset = "crit"
        self.large_text = ""

        self._sock = None
        self._lock = threading.Lock()
        self._stop = threading.Event()

=======
    for n in range(10):
        name = "discord-ipc-%d" % n
        for var in ("XDG_RUNTIME_DIR", "TMPDIR", "TEMP", "TMP"):
            base = os.environ.get(var)
            if base:
                yield "unix", os.path.join(base, name)
        if os.name == "nt":
            yield "pipe", "\\\\?\\pipe\\" + name


class Presence:
    def __init__(self, client_id, log=None):
        self.client_id = str(client_id)
        self.log = log or (lambda *a: None)
        self.asset = "crit"
        self.large_text = ""
        self._sock = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
        self._started = time.time()
        self._details = ""
        self._state = ""

    def _connect(self):
        for kind, path in _candidates():
            try:
                if kind == "pipe":
                    return _Pipe(path)
<<<<<<< HEAD

                if not hasattr(socket, "AF_UNIX"):
                    continue

                sock = socket.socket(
                    socket.AF_UNIX,
                    socket.SOCK_STREAM
                )

                sock.settimeout(2.0)
                sock.connect(path)

                return sock

            except (OSError, AttributeError):
                continue

=======
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.settimeout(2.0)
                sock.connect(path)
                return sock
            except OSError:
                continue
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
        return None

    def _send(self, opcode, payload):
        body = json.dumps(payload).encode("utf-8")
        frame = struct.pack("<II", opcode, len(body)) + body
<<<<<<< HEAD

        with self._lock:
            if self._sock is None:
                return False

            try:
                self._sock.sendall(frame)
                return True

=======
        with self._lock:
            if self._sock is None:
                return False
            try:
                self._sock.sendall(frame)
                return True
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
            except OSError:
                self._sock = None
                return False

    def _recv_exact(self, count):
        chunks = []
        remaining = count
<<<<<<< HEAD

        while remaining > 0:
            chunk = self._sock.recv(remaining)

            if not chunk:
                return None

            chunks.append(chunk)
            remaining -= len(chunk)

=======
        while remaining > 0:
            chunk = self._sock.recv(remaining)
            if not chunk:
                return None
            chunks.append(chunk)
            remaining -= len(chunk)
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
        return b"".join(chunks)

    def _read(self, timeout=1.0):
        with self._lock:
            if self._sock is None:
                return None
<<<<<<< HEAD

            try:
                self._sock.settimeout(timeout)

                head = self._recv_exact(8)

                if head is None:
                    self._sock = None
                    return None

                _opcode, length = struct.unpack("<II", head)

                body = self._recv_exact(length)

                if body is None:
                    self._sock = None
                    return None

                return json.loads(body.decode("utf-8"))

            except socket.timeout:
                return None

=======
            try:
                self._sock.settimeout(timeout)
                head = self._recv_exact(8)
                if head is None:
                    self._sock = None
                    return None
                _opcode, length = struct.unpack("<II", head)
                body = self._recv_exact(length)
                if body is None:
                    self._sock = None
                    return None
                return json.loads(body.decode("utf-8"))
            except socket.timeout:
                return None
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
            except (OSError, ValueError):
                self._sock = None
                return None

    def _activity(self):
        return {
            "application_id": self.client_id,
            "name": self.large_text or self._details,
            "type": 0,
            "details": self._details,
            "state": self._state,
            "assets": {
                "large_image": self.asset,
                "large_text": self.large_text or self._details,
            },
<<<<<<< HEAD
            "timestamps": {
                "start": int(self._started)
            },
=======
            "timestamps": {"start": int(self._started)},
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
            "instance": True,
        }

    def _push(self):
        return self._send(OP_FRAME, {
            "cmd": "SET_ACTIVITY",
<<<<<<< HEAD
            "args": {
                "pid": os.getpid(),
                "activity": self._activity(),
            },
=======
            "args": {"pid": os.getpid(), "activity": self._activity()},
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
            "nonce": "%d" % time.time_ns(),
        })

    def _handshake(self):
<<<<<<< HEAD
        if not self._send(OP_HANDSHAKE, {
            "v": 1,
            "client_id": self.client_id,
        }):
            return False

        reply = self._read(timeout=3.0)

        if not reply:
            return False

        if reply.get("evt") == "ERROR":
            detail = reply.get("data") or {}
            self.log(
                "handshake rejected: %s" %
                detail.get("message")
            )
            return False

=======
        if not self._send(OP_HANDSHAKE, {"v": 1, "client_id": self.client_id}):
            return False
        reply = self._read(timeout=3.0)
        if not reply:
            return False
        if reply.get("evt") == "ERROR":
            detail = reply.get("data") or {}
            self.log("handshake rejected: %s" % detail.get("message"))
            return False
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
        return True

    def start(self, details, state, asset="crit", large_text=None):
        self._details = details
        self._state = state
        self.asset = asset
        self.large_text = large_text or details
<<<<<<< HEAD

        self._started = time.time()

        self._stop.clear()

        threading.Thread(
            target=self._run,
            daemon=True,
            name="DiscordPresence"
        ).start()

    def _run(self):
        backoff = 2.0

        while not self._stop.is_set():
            sock = self._connect()

            if self._stop.is_set():
                if sock is not None:
                    try:
                        sock.close()
                    except OSError:
                        pass
                return

            with self._lock:
                self._sock = sock

=======
        self._started = time.time()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        backoff = 2.0
        while not self._stop.is_set():
            sock = self._connect()
            if self._stop.is_set():
                try:
                    sock.close()
                except (OSError, AttributeError):
                    pass
                return
            with self._lock:
                self._sock = sock
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
            if sock is None or not self._handshake():
                self._drop()
                self._stop.wait(backoff)
                backoff = min(backoff * 2, 30.0)
                continue
<<<<<<< HEAD

=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
            if self._stop.is_set():
                self._clear_activity()
                self._drop()
                return
<<<<<<< HEAD

            backoff = 2.0

=======
            backoff = 2.0
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
            self._push()
            self.log("connected")

            last_push = time.time()
<<<<<<< HEAD

            while not self._stop.is_set():
                if self._read(timeout=1.0) is None:
                    with self._lock:
                        if self._sock is None:
                            break

                if time.time() - last_push > 60:
                    self._push()
                    last_push = time.time()

=======
            while not self._stop.is_set():
                if self._read(timeout=1.0) is None and self._sock is None:
                    break
                if time.time() - last_push > 60:
                    self._push()
                    last_push = time.time()
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
                if self._stop.wait(0.2):
                    break

            self._drop()
<<<<<<< HEAD

=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
            if not self._stop.is_set():
                self._stop.wait(3.0)

    def _drop(self):
        with self._lock:
            sock = self._sock
            self._sock = None
<<<<<<< HEAD

=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass

    def _clear_activity(self):
        body = json.dumps({
            "cmd": "SET_ACTIVITY",
<<<<<<< HEAD
            "args": {
                "pid": os.getpid(),
                "activity": None,
            },
            "nonce": "%d" % time.time_ns(),
        }).encode("utf-8")

        with self._lock:
            if self._sock is not None:
                try:
                    self._sock.sendall(
                        struct.pack("<II", OP_FRAME, len(body)) + body
                    )
=======
            "args": {"pid": os.getpid(), "activity": None},
            "nonce": "%d" % time.time_ns(),
        }).encode("utf-8")
        with self._lock:
            if self._sock is not None:
                try:
                    self._sock.sendall(struct.pack("<II", OP_FRAME, len(body)) + body)
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
                except OSError:
                    pass

    def stop(self):
        self._stop.set()
        self._clear_activity()
        self._drop()
