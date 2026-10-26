# ZET-U Mod Suite for BepInEx

The ZET-U Mod Suite of Dungeon Settlers Modded, as a plugin for plain BepInEx 6 (IL2CPP). It does not need the mod loader.

- **Skills without weapon requirement**: a unit can use the skills of a skill tree without holding that tree's weapon.
  Prerequisite skills, level requirements and skill point costs stay the same.
- **Max units in the dungeon**: how many units can go on one expedition (4 to 8; 4 is the game's normal value).
  The expedition window, the supplies step, the Inventory / Status / Skill windows and the result screen show every unit.

Game version: Dungeon Settlers build 25846389 (DS_B.0.4.29). After a game update, a part whose game code changed
switches itself off and says so in `BepInEx/LogOutput.log`.

Version 0.1.2. Fix: a world did not load on game builds other than 25846389. The save warning and backup are removed:
tests showed saves made with the mod load and play in the normal game.

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

## How to change the settings

1. Start the game once with the plugin, then quit. This makes the settings file.
2. Close the game. Open this file with a text editor (for example Notepad):
   `BepInEx/config/amfi.zetu-suite.cfg` (in the game folder, next to `DungeonSettlers.exe`).
3. Change the value after the `=` sign. Save the file.
4. Start the game. The new values work from this start. (A change while the game runs does nothing until the next start.)

The file looks like this:

```
[Gameplay]
skills_without_weapon = true
max_units = 4
```

| Setting | Values | What it does |
|---|---|---|
| `skills_without_weapon` | `true` (normal) or `false` | `true`: a unit can use the skills of a skill tree without holding that tree's weapon. `false`: the game's normal rule. |
| `max_units` | a number from `4` to `8` (normal: `4`) | How many units can go into the dungeon on one expedition. `4` is the game's normal value. Example: `max_units = 8` for 8 units. |

A value outside these limits is changed to the nearest good value. If you delete the file, the next start makes it
again with the normal values.

## BepInEx console

The plugin switches BepInEx's console window off: it sets `Enabled = false` under `[Logging.Console]` in
`BepInEx/config/BepInEx.cfg` (the next starts open no console) and closes the console of the current start.
BepInEx's log file `BepInEx/LogOutput.log` stays on.

## Your saves

Saves made with this mod load and play in the normal game (without this plugin). There the game's own limits apply
again: its windows show 4 units, and a new expedition takes at most 4 units; the other units stay at the base.
The plugin never deletes or moves a save file.

## With Dungeon Settlers Modded

If the mod loader's own ZET-U Mod Suite runs in the same game, this plugin does nothing and writes one line about it in
`BepInEx/LogOutput.log`. The mod loader's settings apply then.

## Remove

Delete the folder `BepInEx/plugins/ZetuSuite`.
