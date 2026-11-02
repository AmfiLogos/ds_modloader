# Roadmap: Mod loader

What we are working on, what comes next, and where you can help.

Updated: 2026-11-02

Dungeon Settlers Modded is made of three repos. Each item below is listed once, in the repo that does the work:

- **Mod loader** (this repo): the app, the Mods window, the Load Campaign list and its filters, which mods each world uses, the parts that every mod window shares (moving, the "-" and "+" size buttons), the installers and their downloads.
- **Multiplayer mod** ([ds_multiplayer](https://github.com/AmfiLogos/ds_multiplayer), [its roadmap](https://github.com/AmfiLogos/ds_multiplayer/blob/main/ROADMAP.md)): playing together, the Multiplayer page, the chat, the Foreign Expedition hub and PvP.
- **Units Laboratory** ([ds_custommodes](https://github.com/AmfiLogos/ds_custommodes), [its roadmap](https://github.com/AmfiLogos/ds_custommodes/blob/main/ROADMAP.md)): My Units, the Unit Builder, the Wiki Build Gallery, Test Fights and the DPS Check.

## Now

These are being built for the next version.

- **Weapon skill slot for your screen only.** The weapon skill slot will change only your own screen: a button that uses your unit's weapon skill. Friends in your game will not need the mod. <!-- id: weapon-slot-local -->

## Next

- Nothing is planned yet.

## Later / ideas

- **Pick your game version.** A list of game versions that the mods work with, so you can choose which version to play.
- **Mods that react to game events.** The loader tells mods when the game opens or closes one of its windows, so mods need less of their own code to find them.
- **A "Local" tag on mods.** The loader finds out by itself if a mod only changes your screen ("Local", never blocks joining a friend) or if every player needs it.
- **Save tags.** "Vanilla+" for mods after which the plain game can still play your save, and "Mod-dependent save" for mods that the save needs. Before you first load a save with a mod of the second kind, the loader makes a backup copy.
- **A tidier Load list.** Your worlds, multiplayer copies, PvP and Expedition maps and plain game saves in clear groups.
- **A check of the installed files after an install**, with a clear message when a file is wrong.

## Done

- **v0.1.30 (2026-11-02).** The "-" and "+" size buttons show only while the mouse is over the window, stay under the mouse when you click them, and have keys (Ctrl and "-" / "="). "Global settings" in the Mods window saves or resets where every mod window is. A right-drag moves only the window on top. Folding a section no longer resizes a window. Load Campaign shows your solo worlds first. Two mods can change the same game function. Hover cards open next to what you point at. Units Laboratory can open your units in the wiki's Character Builder.
- **v0.1.29 (2026-10-26).** Smoother game on the title screen, on the loading screen and in your worlds. VSync stays off when you turn it off. Mods can give their panels "-" and "+" size buttons. Worlds that mods make for themselves stay out of the Load list and the Steam Cloud backup.
- **v0.1.28 (2026-10-25).** Escape and the game's red X close the mod windows, and they follow the game's UI Scale. Load Campaign filters have a Solo box and two groups. New fix: Live building HP. After an update, a "What's new" window shows once.
- **v0.1.27 (2026-10-25).** Load Campaign is one list in the game's own order. Hell difficulty is always loaded, with no restart. New mod: Locked Slots.
- **v0.1.26 (2026-10-25).** A Fixes tab in the Mods window, with fixes that are on in all worlds.
- **v0.1.23 to v0.1.25 (2026-10-24).** Load Campaign filters and "Latest save only". The loader windows use the game's text look. Mac: one "Dungeon Settlers Modded" in the Dock and Spotlight, and updates keep the app where it is. New mods: Sound settings fix, Multi-unit view, Weapon skill slot.
- **v0.1.22 (2026-10-21).** Every mod window was checked in four languages, at five window sizes and at UI Scale 1.5, and now fits inside the game window.
- **v0.1.21 (2026-10-21).** A "Download update" button. The game's Bug Report button opens our report window. Your tutorial progress comes over from the normal game.
- **Earlier.** Mods are saved per world, a hover card lists all the mods of a world, one download per mod, a Mac .dmg and a Windows installer, plainer text. Every version is in the [CHANGELOG](CHANGELOG.md).

## Needs help / testing

Have you tried one of these? Please open an issue and tell us what happened.

- **A real Windows PC.** The Windows install, the Mods window and our mods were only tested in a test setup on a Mac.
- **Steam on Windows.** Nobody has tested the Windows version with a real Steam install yet.
- **Windows: a user name with accents.** Test on a real PC with a user name that has accents (for example José). Under Wine the game fails to load some of its own files from such a folder; we need to know if real Windows does the same.

## Known issues

- **Mac asks you to allow the app the first time.** The app is not signed with an Apple developer ID, so you need to right-click it and click Open, or click "Open Anyway" in System Settings.
- **BepInEx mods and MelonLoader mods cannot be on together.** Both start .NET inside the game. If you try, the Mods window refuses the second kind and tells you why.
- **Extended Hotbar refuses to start on Mac.** That mod runs its own check, and we leave it as it is.
- **Weapon skill slot: every player in a game needs it.** Today it adds a real slot to the skill bar, which changes the game for everyone. The Now list makes it a mod for your own screen only.
