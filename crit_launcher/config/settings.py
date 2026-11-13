"""Launcher settings: allowed values, defaults and the settings file store."""

import re

from ..core.jsonstore import JsonStore
from .paths import SETTINGS_FILE


<<<<<<< HEAD
THEMES = ("green", "blue", "purple", "red", "orange", "pink", "white", "custom")
UI_STYLES = ("default", "minecraft", "modrinth", "fastclient", "sklauncher", "leather")
=======
THEMES = ("green", "blue", "purple", "red", "orange", "pink", "custom")
UI_STYLES = ("default", "minecraft", "modrinth", "fastclient", "sklauncher")
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
ADDON_VIEWS = ("browse", "installed")
ADDON_SOURCES = ("modrinth", "curseforge")
COLOR_MODES = ("dark", "light", "oled")
CATALOG_SOURCES = ("labymod", "critical")
DEFAULT_SETTINGS = {
    "active_account": None,
    "active_instance": None,
    "memory_mb": 2048,
    "min_memory_mb": 0,
    "java_path": None,
    "jvm_args": "",
    "game_width": 0,
    "game_height": 0,
    "fullscreen": False,
    "custom_accent": "#4ade80",
    "recent_accents": [],
    "reduce_motion": False,
    "discord_presence": False,
    "bg_pattern": True,
    "sidebar_collapsed": False,
<<<<<<< HEAD
    "sidebar_top": False,
    "custom_bg": False,
    "launcher_width": 0,
    "launcher_height": 0,
    "window_unlocked": False,
=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
    "theme": "green",
    "ui_style": "default",
    "addon_view": "browse",
    "addon_instance": None,
    "color_mode": "dark",
    "catalog_source": "labymod",
    "addon_source": "modrinth",
    "agreement_accepted": False,
    "agreement_version": 0,
    "server_beta_seen": False,
    "local_skin_warning_seen": [],
    "playtime_seconds": 0,
    "playtime_days": {},
}
<<<<<<< HEAD
DEFAULT_WINDOW = (1080, 700)     # launcher window size when nothing custom is saved
LOCKED_MIN_WINDOW = (900, 580)   # normal minimum size
UNLOCKED_MIN_WINDOW = (240, 160)  # minimum once "unlock size limits" is on
MAX_WINDOW_SIDE = 7680


def window_limits(unlocked) -> tuple:
    return UNLOCKED_MIN_WINDOW if unlocked else LOCKED_MIN_WINDOW


def launcher_window_geometry(settings: dict) -> tuple:
    """(width, height, min_width, min_height) for the launcher window from saved settings."""
    minw, minh = window_limits(bool(settings.get("window_unlocked")))
    try:
        w = int(settings.get("launcher_width") or 0) or DEFAULT_WINDOW[0]
        h = int(settings.get("launcher_height") or 0) or DEFAULT_WINDOW[1]
    except (TypeError, ValueError):
        w, h = DEFAULT_WINDOW
    return max(minw, min(w, MAX_WINDOW_SIDE)), max(minh, min(h, MAX_WINDOW_SIDE)), minw, minh


=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
settings_store = JsonStore(SETTINGS_FILE, DEFAULT_SETTINGS)


def _clean_hex(value, default="#4ade80"):
    if isinstance(value, str) and re.fullmatch(r"#[0-9a-fA-F]{6}", value.strip()):
        return value.strip().lower()
<<<<<<< HEAD
    return default
=======
    return default
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
