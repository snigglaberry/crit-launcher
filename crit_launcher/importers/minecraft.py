"""Minecraft Launcher / .minecraft folder scanner."""

from pathlib import Path

from ..minecraft.loaders import MOD_LOADERS
from .common import _count_dir, _count_images
from .universal import (
    _details_from_game,
    _guess_loader_from_mods,
    _looks_like_game_dir,
    _norm_loader,
    game_dir_for,
)

MINECRAFT_SOURCE_ID = "__minecraft__"


def _scan_minecraft(root: Path) -> list:
    """Describe the game folder at ``root`` (or its .minecraft child)."""
    root = Path(root)
    game_dir = root
    if not _looks_like_game_dir(game_dir):
        for name in (".minecraft", "minecraft"):
            candidate = root / name
            if _looks_like_game_dir(candidate):
                game_dir = candidate
                break
        else:
            return []
    game_dir = game_dir_for(game_dir)

    mc_version, loader, loader_version = _details_from_game(game_dir)
    loader = _norm_loader(loader) if loader else "unknown"
    if loader in ("unknown", "vanilla") and (game_dir / "mods").is_dir():
        guessed = _guess_loader_from_mods(game_dir)
        if guessed:
            loader = guessed
    if loader == "unknown" and not (game_dir / "mods").is_dir():
        loader = "vanilla"

    return [{
        "source_id": MINECRAFT_SOURCE_ID,
        "name": "Minecraft",
        "version": mc_version,
        "loader": loader,
        "loader_version": loader_version,
        "has_options": (game_dir / "options.txt").is_file()
        or (game_dir / "optionsLC.txt").is_file(),
        "mods_count": _count_dir(game_dir / "mods", ".jar") if loader in MOD_LOADERS else 0,
        "resourcepacks_count": _count_dir(game_dir / "resourcepacks"),
        "saves_count": _count_dir(game_dir / "saves"),
        "shaderpacks_count": _count_dir(game_dir / "shaderpacks"),
        "schematics_count": _count_dir(game_dir / "schematics"),
        "screenshots_count": _count_images(game_dir / "screenshots"),
        "config_count": _count_dir(game_dir / "config") if loader in MOD_LOADERS else 0,
    }]
