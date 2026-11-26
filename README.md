# Crit Launcher

Crit Launcher is a open source minecraft launcher written in python and html

<details>
<summary>notes</summary>
we are not affiliated with microsoft or mojang

we are not affiliated with modrinth or fastclient

we are not affiliated with fabric forge or neoforge

we are not affiliated with mcdonalds or burger king

the themes which aim to replicate other launchers are completely custom built, no original or decompiled code was used for their creation therefore are completly unrelated, if you are fastclient or modrinth kindly create a support ticket in the discord server for any requests.
</details>

## project layout
the backend is the `crit_launcher/` package, one folder per feature. see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for where everything lives and how to debug it.

run from source: `python crit.py` (needs `pywebview`)
tests: `python -m unittest discover -s tests`

## Minecraft Bedrock (Windows)

Bedrock is built into Crit Launcher; no separate launcher is needed. When running from source,
Crit Launcher keeps Bedrock instance metadata and downloads in `launcher_data/bedrock/instances/`
and the release cache in `launcher_data/bedrock/cache/`, separate from Java instances. Windows
still manages the registered Minecraft package and its in-game save location.

## compilation
first install the source code (the `icon.ico` is already in the folder)
then run
```
pyinstaller --onefile --windowed --name crit --icon=icon.ico --add-data "index.html;." --add-data "assets;assets" crit.py
```
setup:
[innosetup](https://jrsoftware.org/isinfo.php)
then run `packaging/windows/critsetup.iss`

linux: run `bash packaging/linux/build-appimage.sh` or use the manual **Build Linux AppImage** GitHub Actions workflow.

## release signatures

Place the finished binaries together in a release directory. Sign them there with the Ed25519
private key kept outside the checkout. Signing needs the Python `cryptography` package; verification
uses the public key in `crit_launcher/updates/signature.py` and needs only Python.

```powershell
$key = "$env:LOCALAPPDATA\CritLauncher\release-keys\release-ed25519.pem"
python packaging/release.py sign --tag v1.9 --key $key output/crit.exe output/setup.exe output/Crit_Launcher-1.9-x86_64.AppImage
python packaging/release.py verify --tag v1.9 output/crit.exe output/setup.exe output/Crit_Launcher-1.9-x86_64.AppImage
```

Upload the binaries, `SHA256SUMS` and `SHA256SUMS.sig` to the matching GitHub Release.
The updater checks the signature against that exact tag and the downloaded installer's SHA-256 before running it.
