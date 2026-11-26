"""JS-bridge helpers: downloading and starting a Bedrock instance."""

import threading

from ..core.logs import log_exception
from ..core.progress import ProgressTracker
from .installer import ensure_latest, launch_release


class BedrockApi:
    """Background work for instances whose loader is Bedrock."""

    def _bedrock_worker(self, instance_name: str, launch: bool = False):
        try:
            self.emit("installStarted", {
                "instance": instance_name,
                "title": (f"Downloading the latest Bedrock release for {instance_name}..."
                          if not launch else f"Preparing Bedrock for {instance_name}..."),
            })
            tracker = ProgressTracker(self.emit)
            tracker.reset(1_500_000_000)
            tracker.set_task("Finding the latest Bedrock release", force=True)
            release, _package = ensure_latest(instance_name, tracker)
            if launch:
                tracker.set_task(f"Installing Bedrock {release.name}", force=True)
                launch_release(release, instance_name)
            self.emit("installFinished", {"instance": instance_name})
        except Exception as exc:
            log_exception("bedrock worker")
            self.emit("installError", {
                "message": str(exc) or "Couldn't prepare Minecraft Bedrock."})
        finally:
            with self._proc_lock:
                self._installing.discard(instance_name)
            self.push_state()

    def _start_bedrock(self, instance_name: str, launch: bool = False):
        with self._proc_lock:
            if instance_name in self._installing:
                return False
            self._installing.add(instance_name)
        threading.Thread(target=self._bedrock_worker, args=(instance_name, launch),
                         daemon=True).start()
        self.push_state()
        return True
