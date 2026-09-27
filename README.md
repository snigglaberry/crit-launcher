# Crit Launcher

Crit Launcher is a Minecraft launcher with many built-in features.

## Feature List

* Built-in mod, modpack, and resource pack management
* Built-in skin switcher
* Built-in cape switcher
* Forge and Fabric support
* Offline mode without a Minecraft account
* Microsoft authentication

## Warning

If you read the source code, you may notice a session token commands.

**This does not steal your tokens.**

Crit Launcher does not currently have Microsoft approval for its authentication system because we have not applied for approval yet.

As a workaround, Crit Launcher opens Minecraft.net for authentication. After signing in, the launcher uses the session token provided by Minecraft.net to authenticate you.

This authentication generally lasts for around one day.

## No Ads

Crit Launcher contains no advertisements.

to compile 
first install the source code and add a icon to the folder
then run
```
pyinstaller --onefile --windowed --name crit --icon=icon.ico --add-data "background.png;." --add-data "index.html;." launcher.py
```
after that to compile use [innosetup](https://jrsoftware.org/isinfo.php)
i wont tell you how to use inno setup cuz i dont care
