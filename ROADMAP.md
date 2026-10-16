# Roadmap: Mod loader

What we are working on, what comes next, and where you can help.

Dungeon Settlers Modded is made of three repos. Each item below is listed once, in the repo that does the work:

- **Mod loader** (this repo): the app, the Mods window, which mods each world uses, the installers and their downloads.
- **Multiplayer mod** ([ds_multiplayer](https://github.com/AmfiLogos/ds_multiplayer), [its roadmap](https://github.com/AmfiLogos/ds_multiplayer/blob/main/ROADMAP.md)): playing together, the Multiplayer page.
- **Custom Modes** ([ds_custommodes](https://github.com/AmfiLogos/ds_custommodes), [its roadmap](https://github.com/AmfiLogos/ds_custommodes/blob/main/ROADMAP.md)): My Units, Unit Builder, Gallery, Test Fights, the Foreign Expedition hub, PvP.

## Now

- **Mods are saved per world.** The mods you pick will belong to the whole world, so every save and autosave of that world uses them. Today they stick to a single save file. The Mods window then shows that file's long name, which can run past the edge of the window. We will show the world's name instead.
- **The hover card on a world lists all its mods.** One mod per line, Multiplayer and Custom Modes included. Today the card can show just one mod. When the mods do not fit on the world's row, the row will say how many are hidden, for example "Global cooldown +2".
- **One download per mod.** Each mod will be a single download, and the loader reads it as it is.
- **Mac .dmg and Windows installer.** These replace the zip files, so installing takes fewer steps.
- **Plainer text.** We are rewriting the Mods window help lines and the mod descriptions in plain words. Some of the current text reads like a programmer's notes.

## Next

- Nothing else is planned for the loader yet. The Now list comes first.

## Later / ideas

- **Mod settings.** Change a mod's options in the game, not just turn it on or off. It's an idea for later. We haven't started it.

## Needs help / testing

Have you tried one of these? Please open an issue and tell us what happened.

- **A real Windows PC.** The Windows install, the Mods window and our mods were only tested in a test setup on a Mac.
- **Steam on Windows.** Nobody has tested the Windows version with a real Steam install yet.
- **Windows: a user name with accents.** Test on a real PC with a user name that has accents (for example José). Under Wine the game fails to load some of its own files from such a folder; we need to know if real Windows does the same.

## Known issues

- **Mac asks you to allow the app the first time.** The app is not signed with an Apple developer ID, so you need to right-click it and click Open, or click "Open Anyway" in System Settings.
- **BepInEx mods and MelonLoader mods cannot be on together.** Both start .NET inside the game. If you try, the Mods window refuses the second kind and tells you why.
- **Extended Hotbar refuses to start on Mac.** That mod runs its own check, and we leave it as it is.
- **The weapon-skill slot mod is left out of the download.** We tried to make it work when only the host has it, but other players' games then go out of sync. So every player in a game would need it.
