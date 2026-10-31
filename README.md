# Crit Launcher

Crit Launcher is a open source minecraft launcher 

<details>
<summary>feature list</summary>
fabric forge neoforge and vanilla support
  
addon managment(mods resource packs modpacks shaders)

skin managment(with a catalog)

built in local server hosting

importing data from other launchers

official Microsoft authentication and offline support

themes: default minecraft modrinth fastclient sklauncher

add shortcuts to desktop for instance

customizable viewing methods for instances

play time tracking
</details>
<details>
<summary>notes</summary>
we are not affiliated with microsoft or mojang

we are not affiliated with modrinth or fastclient

we are not affiliated with fabric forge or neoforge

we are not affiliated with mcdonalds or burger king

the themes which aim to replicate other launchers are completely custom built, no original or decompiled code was used for their creation therefore are completly unrelated, if you are fastclient or modrinth kindly create a support ticket in the discord server for any requests
</details>

## compilation 
first install the source code and add a icon to the folder
then run
```
pyinstaller --onefile --windowed --name crit --icon=icon.ico --add-data "index.html;." launcher.py
```
setup:
[innosetup](https://jrsoftware.org/isinfo.php)
then run critsetup.iss


<sub>this readme is valid as of v1.7</sub>
