#!/usr/bin/env sh
# Crit Launcher Native - cross build with Zig (x86_64-windows-gnu).
# Requires: zig 0.13+  (https://ziglang.org/download/)
set -e
cd "$(dirname "$0")"

ZIG=${ZIG:-zig}
command -v "$ZIG" >/dev/null 2>&1 || { echo "error: zig not found; set ZIG=/path/to/zig"; exit 1; }

SOURCES="src/main.cpp src/app/app.cpp src/app/shell.cpp src/app/home.cpp src/app/minecraft.cpp \
src/app/overlays.cpp src/app/page_instances.cpp src/app/page_accounts.cpp \
src/app/page_settings.cpp src/app/page_misc.cpp src/ui/framework.cpp \
src/render/canvas.cpp src/render/font.cpp src/render/svg.cpp \
src/core/util.cpp src/core/zip.cpp src/platform/json.cpp src/platform/win_platform.cpp"

mkdir -p build
# shellcheck disable=SC2086
"$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 -DNDEBUG \
  -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE -DCRIT_USE_STB \
  -Isrc -Ithird_party/stb \
  $SOURCES -o build/CritLauncher.exe \
  -static-libgcc -static-libstdc++ -Wl,--subsystem,windows \
  -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -lshlwapi \
  -ld3d11 -ldxgi -lwinhttp -lversion -ldwmapi -lbcrypt -lcrypt32 -lpsapi -luuid
echo "built build/CritLauncher.exe"
