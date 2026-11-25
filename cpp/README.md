# Building Crit Launcher (native C++ / DirectX 11)

No Python, no HTML, no WebView. The launcher is a single self-contained
`CritLauncher.exe` built from `cpp/src` with the DirectX 11 renderer in
`cpp/src/platform/win_platform.cpp`.

## Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| MSVC (Visual Studio 2022 Build Tools) | v143 toolset | provides `cl.exe`, the Windows SDK and `d3d11.lib` |
| Zig (optional) | 0.13+ | cross-compiles the .exe from Linux/macOS using the MinGW headers |
| Windows 10/11 | — | runtime target |

No external libraries are required at build or run time: fonts (`core/fontdata.h`),
icons (`ui/icons_data.h`) and the app icon (`core/win_icons.h`) are embedded in
the binary, and `stb_truetype.h` is vendored in `third_party/stb`.

## Windows (MSVC)

```bat
cd cpp
build.bat
```

`build.bat` calls `vcvars64.bat` (or uses the Developer Command Prompt you are
already in), compiles every `src\**\*.cpp` except the headless preview tool, and
links `CritLauncher.exe` next to the sources. Copy `..\assets\dark_Bg.png` beside
the exe if you want the home artwork to load without a download.

## Windows (MinGW / Zig from Linux)

```sh
cd cpp
./build.sh            # produces build/CritLauncher.exe (x86_64-windows-gnu)
```

`build.sh` uses Zig's bundled MinGW toolchain, statically links the C++ runtime
(`-static-libgcc -static-libstdc++`) and embeds an application manifest so the
exe picks up per-monitor DPI awareness and the common controls v6 styles.

## What gets produced

* `CritLauncher.exe` — window, DirectX 11 renderer, all pages, install/launch engine.
* On first run it creates `%APPDATA%\CritLauncher` and reuses the exact JSON
  schemas of the Python launcher, so existing instances and accounts keep working.

## Headless preview (no GPU)

The UI is drawn through one display list (`render/canvas.cpp`), consumed either by
the D3D11 backend or by a software rasterizer. That makes it possible to render
every screen to a PNG on any machine:

```sh
cd cpp
g++ -std=c++17 -O2 -Isrc -Ithird_party/stb -DCRIT_USE_STB \
    src/tools/preview_main.cpp src/app/*.cpp src/ui/framework.cpp \
    src/render/canvas.cpp src/render/font.cpp src/render/svg.cpp src/render/preview.cpp \
    src/core/util.cpp src/core/zip.cpp src/platform/json.cpp src/platform/posix_platform.cpp \
    -o /tmp/crit_preview -lpthread
/tmp/crit_preview out/ 1080 700 home instances accounts settings modal
```

Add `CRIT_DUMP_BATCHES=1` to print the draw batches and texture stats, which is
handy when a widget does not show up.

## Layout

```
cpp/src/
  main.cpp              wWinMain / main entry
  app/                  App state, shell (sidebar/topbar/boot), pages, install+launch engine
  ui/                   immediate-mode widgets, theme, embedded icon atlas
  render/               display list, TrueType atlas, SVG icons, software preview
  platform/             DirectX 11 device + window, WinHTTP, Win32 file/process helpers
  core/                 strings/math/util, DEFLATE + ZIP, embedded fonts and .ico
```
