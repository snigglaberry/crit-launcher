"""Program entry: builds the window and wires events."""

import logging
import os
import sys
import threading
import time

from ..bootstrap import init as init_environment
from ..config.paths import IS_LINUX
from ..config.settings import launcher_window_geometry, settings_store
from ..core.logs import log_exception
from ..ui.page import prepare_page, start_asset_download
from ..ui.webview_lifecycle import cancel_edge_scripts_on_close
from .facade import API


def main():
    init_environment()
    if len(sys.argv) >= 3 and sys.argv[1] == "--launch-instance":
        instance_name = " ".join(sys.argv[2:])
        api = API()
        result = api.launch(instance_name)
        if not result.get("success"):
            logging.error("Shortcut launch failed for %s: %s", instance_name, result.get("error"))
            return 1
        while True:
            with api._proc_lock:
                installing = instance_name in api._installing
            if not installing:
                break
            time.sleep(0.25)
        return 0

    if IS_LINUX:
        # Use the Qt (QtWebEngine) backend. It ships inside the AppImage; GTK/WebKit would not.
        os.environ.setdefault("QT_API", "pyqt6")
    import webview

    start_asset_download()
    api = API()
    index_path = prepare_page()

    try:
        win_w, win_h, min_w, min_h = launcher_window_geometry(settings_store.read())
    except Exception:
        log_exception("read window size")
        win_w, win_h, min_w, min_h = 1080, 700, 900, 580

    window = webview.create_window(
        "Crit Launcher",
        str(index_path),
        js_api=api,
        width=win_w,
        height=win_h,
        min_size=(min_w, min_h),
        resizable=True,
        background_color="#0b0c0e",
    )
    api.set_window(window)
    # pywebview runs `closing` on the GUI thread. Don't wait for servers or
    # evaluate JavaScript from there; let the native window close first.
    window.events.closing += api._begin_close
    if sys.platform == "win32":
        window.events.before_show += cancel_edge_scripts_on_close

    def on_closed():
        api.cancel_minecraft_web_login()

    window.events.closed += on_closed
    api._presence_sync()

    def on_shown():
        api.apply_saved_titlebar_theme()

    try:
        window.events.shown += on_shown
    except Exception:
        log_exception("hook shown event")

    try:
        webview.start(gui="qt" if IS_LINUX else None, debug=False)
    finally:
        api._begin_close()
        # Discord IPC can be blocked on a read, so don't hold up exit on it.
        threading.Thread(target=api._presence_sync_off, daemon=True).start()
        for name, cleanup in (("e4mc", api.stop_e4mc), ("servers", api.stop_all_servers)):
            try:
                cleanup()
            except Exception:
                log_exception(f"stop {name} on close")
