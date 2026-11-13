"""JS-bridge API: artwork."""

<<<<<<< HEAD
import base64

from ..config.paths import DATA_DIR
from ..config.settings import settings_store
from ..core.logs import log_exception
from .mc_textures import ensure_mc_font, ensure_mc_panorama, ensure_mc_textures
from .page import ensure_home_assets


CUSTOM_BG_FILE = DATA_DIR / "custom_bg.jpg"
_CUSTOM_BG_MAX = 12 * 1024 * 1024


=======
from ..core.logs import log_exception
from .mc_textures import ensure_mc_panorama, ensure_mc_textures
from .page import ensure_home_assets


>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
class UiAssetsApi:
    """Fetching the page's artwork."""

    def ensure_assets(self):
        """index.html calls this when assets/<bg>.png isn't readable yet (first run, packaged exe)."""
        try:
            return ensure_home_assets()
        except Exception:
            log_exception("ensure_assets")
            return {"success": False, "files": {}}

<<<<<<< HEAD
    def ensure_mc_font(self):
        try:
            return ensure_mc_font()
        except Exception:
            log_exception("ensure_mc_font")
            return {"success": False}

=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
    def ensure_mc_textures(self):
        try:
            return ensure_mc_textures()
        except Exception:
            log_exception("ensure_mc_textures")
            return {"success": False, "files": {}}

    def ensure_mc_panorama(self):
        try:
            return ensure_mc_panorama()
        except Exception:
            log_exception("ensure_mc_panorama")
            return {"success": False}
<<<<<<< HEAD

    def save_custom_bg(self, data):
        """Store the user's own background (the page sends a downscaled JPEG data URI)."""
        try:
            prefix = "data:image/jpeg;base64,"
            if not isinstance(data, str) or not data.startswith(prefix):
                return {"success": False, "error": "That image format isn't supported."}
            raw = base64.b64decode(data[len(prefix):])
            if len(raw) > _CUSTOM_BG_MAX or not raw.startswith(b"\xff\xd8\xff"):
                return {"success": False, "error": "That image couldn't be used."}
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            tmp = CUSTOM_BG_FILE.with_suffix(".part")
            tmp.write_bytes(raw)
            tmp.replace(CUSTOM_BG_FILE)
            settings_store.update(lambda s: s.__setitem__("custom_bg", True))
            return {"success": True}
        except Exception:
            log_exception("save_custom_bg")
            return {"success": False, "error": "Couldn't save that image."}

    def clear_custom_bg(self):
        try:
            try:
                CUSTOM_BG_FILE.unlink()
            except FileNotFoundError:
                pass
            settings_store.update(lambda s: s.__setitem__("custom_bg", False))
            return {"success": True}
        except Exception:
            log_exception("clear_custom_bg")
            return {"success": False}

    def get_custom_bg(self):
        try:
            if not CUSTOM_BG_FILE.is_file():
                return {"success": False}
            return {"success": True, "data": "data:image/jpeg;base64," + base64.b64encode(CUSTOM_BG_FILE.read_bytes()).decode("ascii")}
        except Exception:
            log_exception("get_custom_bg")
            return {"success": False}
=======
>>>>>>> c8a4504af20569eff010b5589660ae1511cab67c
