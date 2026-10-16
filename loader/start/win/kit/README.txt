Dungeon Settlers Modded - Windows
=================================

A mod loader for Dungeon Settlers (Steam build 25846389). It plays a copy of the game with mods.
Steam's game, your normal saves and your settings do not change. The modded game uses a copy of your saves.


INSTALL (one time)
1. Quit Dungeon Settlers.
2. Double-click Dungeon-Settlers-Modded-Setup.exe.
   If Windows shows "Windows protected your PC": click "More info", then click "Run anyway".
   (The installer is not signed. This is why Windows shows this message.)
3. Click Next. The installer finds the game folder of Steam by itself.
   If the game folder box is empty:
   3.1 In Steam, right-click Dungeon Settlers. Click Manage, then click Browse local files.
   3.2 Click the address bar at the top of that folder window. Press Ctrl+C.
   3.3 Click the game folder box in the installer. Press Ctrl+V. Click Next.
4. Click Install. It copies the game (about 700 MB, one or two minutes).
5. Click Finish. You now have "Dungeon Settlers Modded" on your desktop and in the Start menu.

Without the installer (zip kit): extract the zip, open the folder, double-click install.bat.
A black window opens and does the same steps.


PLAY
1. Double-click "Dungeon Settlers Modded". If Steam is not open, it starts Steam first and waits for it.
2. The first start copies your normal saves into the modded game (only once).
3. On the title screen, click Mods to turn mods on or off.

To play the normal game, start it from Steam as usual. Steam starts it without mods.


ADD A MOD
1. Download the mod file. Its name is like gcd-cdr-0.1.18.zip. Do not extract it.
2. Open the mods folder. Do one of these:
   - In the game, on the title screen, click Mods. Click "Open mods folder".
   - Press Windows+R. Paste %LOCALAPPDATA%\Dungeon Settlers Modded\mods and press Enter.
3. Put the zip file into the mods folder.
4. In the Mods window, click Refresh. The mod shows up in the list.
   (If the game is not running: start Dungeon Settlers Modded. It installs the mod. Then click Mods.)
5. Turn the new mod on. Click Apply. The game restarts behind a loading screen, with the mod on.
A new version of a mod replaces the old version. The old version goes to the "retired" folder.


IF SOMETHING GOES WRONG
- "The modded game copy is missing": run the installer (or install.bat) again.
- "Your game version is not the version this kit is for": Steam updated the game. Get the newer kit.
- Antivirus removes baselib.dll: the mod files are not signed. Allow them in your antivirus, then run install.bat again.
- Send these files to the person who helps you: the folder
  %LOCALAPPDATA%\Dungeon Settlers Modded\profiles\modded
  has dsload.log, rig.log, starter.log and player.log.
  (Press Windows+R, paste that folder name, press Enter.)


UNINSTALL
1. Quit Dungeon Settlers.
2. Open Windows Settings, Apps, Installed apps. Find "Dungeon Settlers Modded". Click Uninstall.
   (Zip kit without the installer: double-click uninstall.bat in the kit folder.)
   Your modded saves stay in %LOCALAPPDATA%\Dungeon Settlers Modded\profiles. You can delete that folder.
