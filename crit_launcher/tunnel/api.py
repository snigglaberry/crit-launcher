"""JS-bridge API: share a local Minecraft port through e4mc."""

from ..core.logs import log_exception
from ..instances.store import read_instance_meta, sanitize_instance_name
from .client import E4mcSession, detect_lan_port


class E4mcApi:
    """Getting a public e4mc address for a LAN world or a running server."""

    def _e4mc_init(self):
        self._e4mc = E4mcSession(self._e4mc_changed)

    def _e4mc_changed(self, state):
        self.emit("e4mcChanged", state)

    def get_e4mc_status(self):
        try:
            data = self._e4mc.status()
            data["success"] = True
            return data
        except Exception:
            log_exception("get_e4mc_status")
            return {"success": False, "error": "Couldn't read the e4mc status."}

    def start_e4mc(self, kind, name):
        try:
            name = sanitize_instance_name(name)
            if kind == "server":
                rt = self._srv_online_rt(name)
                if not rt:
                    return {"success": False, "error": "Start the server before sharing it."}
                port = rt.port

                def is_running():
                    return not self._closing and rt.status == "online" and rt.proc.poll() is None
            elif kind == "instance":
                proc, port = self._e4mc_lan_target(name)
                if not port:
                    return {"success": False, "error": "Open your Java world to LAN, then click Share again."}

                def is_running():
                    return not self._closing and proc.poll() is None
            else:
                return {"success": False, "error": "Choose a Java server or LAN world to share."}

            ok, error = self._e4mc.start(port, (kind, name), is_running)
            if not ok:
                return {"success": False, "error": error}
            return self.get_e4mc_status()
        except ValueError as exc:
            return {"success": False, "error": str(exc)}
        except Exception:
            log_exception("start_e4mc")
            return {"success": False, "error": "Couldn't start e4mc."}

    def _e4mc_stop_target(self, kind, name):
        self._e4mc.stop((kind, name))

    def stop_e4mc(self, kind=None, name=None):
        try:
            if kind is None and name is None:
                self._e4mc.stop()
            else:
                self._e4mc_stop_target(kind, sanitize_instance_name(name))
            return self.get_e4mc_status()
        except Exception:
            log_exception("stop_e4mc")
            return {"success": False, "error": "Couldn't stop e4mc."}

    def _e4mc_lan_target(self, name):
        with self._proc_lock:
            proc, started = self._processes.get(name), self._sessions.get(name)
        if not proc or not started or proc.poll() is not None:
            return None, None
        if (read_instance_meta(name) or {}).get("loader") == "bedrock":
            return None, None
        return proc, detect_lan_port(name, since=started)

    def get_e4mc_port(self, instance_name):
        """The current Java instance's announced LAN port, if any."""
        try:
            _, port = self._e4mc_lan_target(sanitize_instance_name(instance_name))
            return {"success": True, "port": port}
        except ValueError:
            return {"success": False, "error": "That instance name isn't valid."}
        except Exception:
            log_exception("get_e4mc_port")
            return {"success": False, "error": "Couldn't read that instance's log."}
