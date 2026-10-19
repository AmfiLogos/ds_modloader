# ZET-U Mod Suite for BepInEx

The ZET-U Mod Suite of Dungeon Settlers Modded, as a plugin for plain BepInEx 6 (IL2CPP). It does not need the mod loader.

- **Skills without weapon requirement**: a unit can use the skills of a skill tree without holding that tree's weapon.
  Prerequisite skills, level requirements and skill point costs stay the same.
- **Max units in the dungeon**: how many units can go on one expedition (4 to 8; 4 is the game's normal value).
  The expedition window, the supplies step, the Inventory / Status / Skill windows and the result screen show every unit.

Game version: Dungeon Settlers build 25846389 (DS_B.0.4.29). After a game update, a part whose game code changed
switches itself off and says so in `BepInEx/LogOutput.log`.

## Install (Windows)

1. Install BepInEx 6 IL2CPP **be.788** for Windows x64:
   `BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.788+5b766a3.zip` from https://builds.bepinex.dev/projects/bepinex_be
   Unzip it into the game folder (the folder with `DungeonSettlers.exe`).
2. Start the game once and wait for the title. The first start takes a few minutes (BepInEx prepares the game's code).
   Quit the game.
3. Unzip this zip into the game folder. You get `BepInEx/plugins/ZetuSuite/ZetuSuite.dll`.
4. Start the game. The plugin is on: installing it is choosing it.

## Install (Mac)

The Mac needs our BepInEx build for Apple chips: `bepinex-mac-arm64-0.1.3.zip` from
https://github.com/AmfiLogos/ds_multiplayer/releases/tag/bepinex-mac-0.1.3 (plain BepInEx be.788 does not run on Apple
chips). Put this plugin in its `BepInEx/plugins/ZetuSuite/` folder, and start the game with BepInEx (Doorstop).
Dungeon Settlers Modded does all of this for you, and it has the ZET-U Mod Suite built in: with it you do not need
this plugin.

## Settings

File: `BepInEx/config/amfi.zetu-suite.cfg` (written at the first start). Change it while the game is closed; the new
values apply at the next start.

```
[Gameplay]
skills_without_weapon = true
max_units = 4
```

## Your saves

This mod changes what a world saves (for example 8 units in a dungeon). Such a world may break later in the game
without this mod. So before a save loads with the mod on, the game asks you once per world: **Load** or **Cancel**.
Load first copies that world's save files to `BepInEx/ZetuSuite/save_backups/<date>_<clan name>/`.
To go back: quit the game, then copy the files from that folder into the game's `Saves` folder
(Windows: `%USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers\Saves`).
The plugin never deletes or moves a save file.

## With Dungeon Settlers Modded

If the mod loader's own ZET-U Mod Suite runs in the same game, this plugin does nothing and writes one line about it in
`BepInEx/LogOutput.log`. The mod loader's settings apply then.

## Remove

Delete the folder `BepInEx/plugins/ZetuSuite`.
