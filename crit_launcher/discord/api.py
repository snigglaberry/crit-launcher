<<<<<<< HEAD

=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
"""JS-bridge API: Discord Rich Presence."""

import atexit
import logging
import threading

from ..config.settings import settings_store
from ..core.http import http_get_json
from ..core.logs import log_exception
from ..version import APP_VERSION


DISCORD_CLIENT_ID = "1566575742307934349"
<<<<<<< HEAD

DISCORD_ASSETS_URL = (
    "https://discord.com/api/v9/oauth2/applications/%s/assets"
    % DISCORD_CLIENT_ID
)


def _discord_asset():
    """Find the uploaded Discord application asset."""

    try:
        assets = http_get_json(
            DISCORD_ASSETS_URL,
            timeout=3,
            retries=0
        ) or []

        for item in assets:
            if item.get("name") == "crit":
                return "crit"

        return "crit-1024"

=======
DISCORD_ASSETS_URL = "https://discord.com/api/v9/oauth2/applications/%s/assets" % DISCORD_CLIENT_ID


def _discord_asset():
    """Discord keeps the size suffix on upload, so ask which name exists."""
    try:
        for item in http_get_json(DISCORD_ASSETS_URL, timeout=3, retries=0) or []:
            if item.get("name") == "crit":
                return "crit"
        return "crit-1024"
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
    except Exception:
        return "crit"


class DiscordPresenceApi:
<<<<<<< HEAD
    """Cross-platform Discord Rich Presence API."""

    def __init__(self):
        self._presence = None
        self._presence_starting = False

        self._presence_lock = threading.RLock()

        atexit.register(self._presence_sync_off)

    def _presence_sync_off(self):
        with self._presence_lock:
            presence = self._presence
            self._presence = None

=======
    """Discord Rich Presence."""

    def _presence_sync_off(self):
        with self._presence_lock:
            presence, self._presence = self._presence, None
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
        if presence is not None:
            try:
                presence.stop()
            except Exception:
<<<<<<< HEAD
                log_exception("presence stop")

    def _presence_sync(self):
        wanted = bool(
            settings_store.read().get("discord_presence", False)
        )

        with self._presence_lock:
            if wanted:
                if self._presence is not None or self._presence_starting:
                    return

                self._presence_starting = True

            else:
                if self._presence is None:
                    return

                presence = self._presence
                self._presence = None

                try:
                    presence.stop()
                except Exception:
                    log_exception("presence stop")

                return

        def boot():
            try:
                from . import rpc as discord_rpc

                presence = discord_rpc.Presence(
                    DISCORD_CLIENT_ID,
                    log=lambda m: logging.info(
                        "presence: %s", m
                    )
                )

                presence.start(
                    "Crit Launcher",
                    "v" + APP_VERSION,
                    asset=_discord_asset()
                )

                with self._presence_lock:
                    if (
                        settings_store.read().get("discord_presence", False)
                        and self._presence is None
                    ):
                        self._presence = presence
                    else:
                        presence.stop()

            except Exception:
                log_exception("presence start")

            finally:
                with self._presence_lock:
                    self._presence_starting = False

        threading.Thread(
            target=boot,
            daemon=True,
            name="DiscordPresenceBootstrap"
        ).start()
=======
                pass

    def _presence_sync(self):
        wanted = bool(settings_store.read().get("discord_presence", False))
        if wanted and self._presence is None and not self._presence_starting:
            self._presence_starting = True

            def boot():
                try:
                    from . import rpc as discord_rpc
                    presence = discord_rpc.Presence(
                        DISCORD_CLIENT_ID, log=lambda m: logging.info("presence: %s", m))
                    presence.start("Crit Launcher", "v" + APP_VERSION, asset=_discord_asset())
                    with self._presence_lock:
                        if settings_store.read().get("discord_presence", False) and self._presence is None:
                            self._presence = presence
                            atexit.register(self._presence_sync_off)
                        else:
                            presence.stop()
                except Exception:
                    log_exception("presence start")
                finally:
                    self._presence_starting = False

            threading.Thread(target=boot, daemon=True).start()
        elif not wanted and self._presence is not None:
            self._presence_sync_off()
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
