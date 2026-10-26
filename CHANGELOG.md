# Changelog

What changed in each version, newest first. Downloads: the GitHub releases of this repo.

## Dungeon Settlers Modded (kit)

### Unreleased (0.1.29)

- Load Campaign: worlds that a mod hides as its own (old multiplayer hub and PvP worlds, Units Laboratory test worlds) no longer add the line "worlds are hidden by a filter under the list". There is no box that shows them.
- Unit cards you tick by a click (Units Laboratory: My Units, Test Fights world units and teams) no longer show a white or grey block next to the card when you point at it or click it.
- Steam Cloud backup: worlds that a mod makes for itself (Units Laboratory Test Fights and DPS Check worlds, old multiplayer hub and PvP worlds) are no longer uploaded, and copies of them already in the cloud are not downloaded to your computer. Those copies stay in the cloud; nothing is deleted.

### 0.1.28 (2026-10-25)

- Escape closes the windows of the mods: the Mods window, the bug report window and the mod message boxes. On a mod's Settings page, Escape goes back first. The game does not see that press, so its own Escape menu does not open.
- The Mods window and the bug report window close with the game's red X, and they follow the game's UI Scale.
- The title corner (Auto-update and the version) hides while a game window is open.
- Load Campaign filters: a new Solo box. The boxes are in two groups: Mods (Vanilla & Vanilla+, Modded) and Play (Solo, Multiplayer). A world shows when its box in each group is on. A group with all boxes off shows every world. All four boxes are on by default.
- Load Campaign: "Remove tag" now sits on the world's title line, right after the multiplayer tag. It no longer covers the world's details.
- New Game: the Custom difficulty panel stays in place when you scroll the settings. The world's mods are in their own panel under it, "Mods of this world".
- Get the host's mods: a mod that is on but did not start on this computer gets its own line with the reason. The game no longer restarts only to be refused again. The Mods window also says when a mod stays off because it stopped the game at its last start.
- Mods window: the settings for your computer only are now called "This computer". A world's Mods window no longer lists client mods such as Multi-unit view (their switch there was never kept). A mod that needs another mod names it as the list shows it, and says "Turn it on." when that mod is off.
- New fix, Live building HP 0.1.0: when a building is selected, its durability number and bar in the bottom panel and in the Inspect window follow its real durability, also when it takes damage or gets repaired. On by default, on the Fixes tab.
- Locked Slots 0.1.2: the lock is a smaller white padlock. The item card of a locked slot says "Kept On Return".
- ZET-U Mod Suite 0.1.3: with 5 to 8 units, the supplies step, the result window and the unit windows stay readable.
- Units Laboratory 0.1.43: a DPS Check in Test Fights (DPS meter, hit log in the chat, Restart Test), units from your saves in Test Fights, Rename Team, and one window look where Escape and the game's red X close the window.
- Multiplayer 0.1.27: its windows close with the game's red X or Escape and follow the game's UI Scale. You can move, resize and read the chat without typing. Without Steam, your player name is never your computer account's name.
- Windows: after an auto-update, new texts of the Mods window and the other mod loader windows show in your language. Before, they stayed in English until you ran Setup again.
- After an update, the game shows this "What's new" window once.

### 0.1.27 (2026-10-25)

- Load Campaign: one list in the game's own order, with no group headings. The filter panel has "Show tags" (on: the Modded and Multiplayer tags; off: names only) under "Latest save only". With all three kind boxes off, every world shows. The hidden-worlds line no longer covers a save row, and the list no longer shows unfiltered for a moment when it opens.
- Hell difficulty is always loaded: the New Game switch only shows or hides the Hell level, with no restart. A world uses Hell only when its combat or management level is 5.
- New mod, Locked Slots 0.1.0: right-click a slot in a unit's inventory and choose "Keep on return", and what is in that slot stays with the unit when it comes back to the settlement. Off by default.
- Weapon skill slot 0.1.2: saves keep the game's own form, so a world opens in the game without the mod. Its worlds count as "Vanilla & Vanilla+" in the Load Campaign filter.
- Units Laboratory 0.1.16: a Delete team box (Team and units / Team only), World units (the units of your worlds as read-only cards) and Save snapshot.
- Multiplayer 0.1.26: the Foreign Expedition hub leaves no save; a world new to multiplayer offers a single-player copy; a guest with Hell joins a Hell world with no restart; clicking units no longer forces a full resync.
- Hover cards no longer cover their own window.
- An older copy of a mod that you added yourself no longer replaces a newer version from the app or from auto-update.
- Everything from v0.1.26.

### 0.1.26 (2026-10-25)

- Mods window: a new Fixes tab (Game mods | Fixes | Launcher mods) for mods that fix bugs in the game: Global cooldown fix and Sound settings fix. "On in all worlds" (on by default) makes every world use a fix, old saves and new ones; a world's Change window can still turn it off for that world.
- Load Campaign filters: a world that has only fixes now counts as "Vanilla & Vanilla+", so "Modded" off no longer hides it.
- A world's Change window: Fixes and Mods sections, and "Show only active mods" (on by default) lists only the mods that are on for that world.
- Multiplayer 0.1.25: in the Bring Units text window, skill trees show in the game's own words and in your language.
- Everything from v0.1.25 (the game's text look in the mod loader and multiplayer windows, the Load Campaign "Mods:" line on one line).

### 0.1.25 (2026-10-24)

- The mod loader's windows and texts (Mods window, Load Campaign filters, boxes, bug report) now have the game's own text look.
- Multiplayer 0.1.23: the multiplayer windows have the game's text look too.
- Load Campaign: the "Mods:" line of a world no longer runs over a multiplayer world's "with <names>", and it always stays on one line. Long mod names are cut with "…"; hover the line to see every mod.
- Everything from v0.1.24 (one Mac "Dungeon Settlers Modded" in the Dock and Spotlight, Weapon skill slot, Units Laboratory 0.1.14).

### 0.1.24 (2026-10-24)

- Mac: one "Dungeon Settlers Modded" in the Dock and in Spotlight, always with mods. Starting the game copy from the Dock or Spotlight now opens the modded game through the app, so the Dock icon you pinned keeps working. Old update copies kept as backups no longer show up as apps.
- New mod in the bundle: Weapon skill slot 0.1.1 (off until you turn it on in the Mods window). The skill bar gets a slot for the equipped weapon's skill.
- Units Laboratory 0.1.14: its texts and title buttons look like the game's own menus.
- Everything from v0.1.23 (Load Campaign filters, "Latest save only", Sound settings fix, Multi-unit view).

### 0.1.23 (2026-10-24)

- Load Campaign has filters, in a small panel on the right of the window: Vanilla & Vanilla+ (worlds with no mods, or only mods that do not change the gameplay), Modded (at least one mod that changes the gameplay), Multiplayer (worlds you played with friends). Tick the kinds of worlds you want to see. The game keeps your choice.
- "Latest save only" in the same panel: each world shows only its newest save, so a long list stays short.
- A save you selected and then hid with a filter is unselected, so Load and Remove File never act on a save you cannot see.
- Multiplayer 0.1.22: the "Show multiplayer worlds" box under the Load list is gone. Use the Multiplayer filter instead.
- Two new mods in the bundle: Sound settings fix (on: the volume sliders and Mute buttons change the sound at once) and Multi-unit view (off until you turn it on in the Mods window).
- Mac: updates now keep the app where it is, so the Dock icon and Finder shortcuts keep working. This starts with the next update. The step to v0.1.23 is still done by the version you have now: if your Dock icon opens an old copy or does nothing after this update, drag the app from Applications to the Dock again.

### 0.1.22 (2026-10-21)

- This release is a review of every window the mods add. We checked each window in English, German, Russian and Japanese, at five game window sizes (960x540 to 2560x1440) and at UI scale 1.5.
- Windows fit inside the game window at every size and UI scale. Nothing is cut at the edge any more. The Teams window was wider than the game window at some sizes: now it scales down to fit.
- Teams: the "take a free unit" hint is in the empty area, not on top of the unit slots. Unit names under the portraits are no longer hidden. The help line stays inside the window.
- Chat: one plain background under the messages and the input line. Long lines wrap inside the box.
- Lobby, Multiplayer page, Host page and rules: texts no longer run edge to edge in buttons or touch the arrows. Long German and Russian texts wrap or get shorter words.
- Recent worlds: each row grows with its players line, and the players line no longer runs under your units.
- PvP: the score panel labels sit inside the panel. The vote box and the Bring Units check boxes line up. A clearer message when a team has no units yet.
- PvP: a player who joins goes to the smaller team (before, a friend could land on Blue too, and every Start was refused).
- Units Laboratory: the Gallery, Test Fights and the Unit Builder follow the game's UI scale. Cancel in the Unit Builder after a change asks "Discard your changes?". Gallery card buttons are no longer cut.
- Mods window, bug report window and loader boxes fit small game windows. Buttons and fields keep their side lines at 960x540.
- Chinese, Japanese and Korean text is never smaller than 14 pt (design size), other languages never smaller than 12 pt.
- Better wording in German, Russian and Japanese in many places.

### 0.1.21 (2026-10-21)

Multiplayer:
- If your connection to the host drops for a short time, you stay in the world and the game joins again by itself. You get your units back. Click Cancel to go to the title.
- Chat: long messages wrap inside the chat box and no longer overlap. Accents, Cyrillic, Chinese, Japanese and Korean letters work. The mouse wheel over the chat scrolls the chat, not the map. You can chat in the lobby before the game starts. The chat shows above the game's windows.
- You see the units your friends select: they glow in the colour of the friend's name.
- When a unit goes to another player in Teams, it leaves the selection of the player who lost it.
- A player who joins with no unit sees the Teams window with a short hint.
- When one player pauses, the others can still give orders to their own units. The orders run when the game continues.
- Return scroll: every player votes first (60 seconds, no answer = yes). The game pauses while any vote is open, also in "No pause" mode.
- Clicking a friend's unit to look at it no longer blocks his orders.
- The lobby has Quit for guests, and Quit or "Turn off multiplayer" for the host.
- Steam lobby: the address block is hidden behind a "Show address for friends without Steam" button. It shows one internet address and one home network address.
- Bring Units window: about 8 units fit on a page, and a click anywhere on a card selects it.

PvP:
- Bring World lists your normal game saves too.
- You can bring units in the PvP lobby and during the preparation (Bring Units button).
- With Units Laboratory installed, you can bring units from My Units or build a new one. Other players do not need Units Laboratory.
- Nobody starts the fight alone: a player proposes, everyone agrees, then a short countdown starts. The game pauses while a proposal is open.
- Each team sees only its own units and what they see. Enemy health bars, portraits, status and windows are hidden. Clicking an enemy only shows its info, like a monster.
- PvP rules: the host can show or hide enemy vision, windows, portraits, health bars, status and view range (all hidden by default). Rules are kept between games and can be saved as presets.
- Maps: random maps with random start points, and dungeon maps (dungeon 1 or 2, a floor, or the boss room).
- Your orders win: hold position (G) stops a unit at once and it holds into the fight (a unit outside its team's half first walks into it), and an enemy attack no longer replaces your attack order.
- When everyone agrees to fight, the game waits until both teams are in place ("Taking places...", at most 20 seconds), then counts down. A timer shows the preparation and the countdown. A round end screen shows who won the round and when the next round starts.

Launcher:
- When an update is found, a "Download update" button shows in the title corner, also with Auto-update off. When it is downloaded, "Restart and update now" installs it at once.
- Bug report: the game's Bug Report button opens our report window (text, contact, a screenshot and the logs, each optional). Reports go to the mod maker.
- The tutorial progress of your normal game comes into the modded game, so you do not see tutorials you already finished.
- Check boxes are square again. The "very fast" speed button is hidden (it did nothing).

### 0.1.20 (2026-10-20)

New:
- Windows: Steam multiplayer. Host and join through Steam, invite your Steam friends from the game, and accept their invites. Mac and Windows players can play together through Steam invites.
- PvP and Foreign Expedition are now part of Multiplayer. You do not need Units Laboratory to host or join them. You bring units from your own saves.
- In the lobby and the Players list, each player's line shows how they are connected: Steam or Direct. A guest sees a line that says how they are connected to the host.
- When you host by address, the game asks your router to open the port by itself (UPnP). If the router does not do it, the lobby tells you to open port 47777 (or your Host port) on your router to this computer. This is also called port forwarding.
- A friend can join while the host has a game window open, for example a Yes/No box. The friend waits and joins when the host closes the window.
- Multiplayer: the Bring Units window shows your units as cards, like Units Laboratory.

Fixes:
- Units Laboratory never stops you from joining a friend, also when your copy is different from the host's or turned off.
- Foreign Expedition: the loot you find comes home with you.
- A host who is alone in the lobby can bring units before friends join.
- Windows: the lobby line about the Windows network question is no longer cut off.

### 0.1.19 (2026-10-20)

New:
- Host port setting for Multiplayer, in the Mods window settings. Type the port your router sends to this computer. Default: 47777.
- The lobby shows your internet address for friends outside your home, and a "Same home" line for players on your own network. Copy copies the internet address.
- Mac: Invite opens a list of your Steam friends in the game. Your friend must open Dungeon Settlers Modded first.
- The Mods window has two tabs: Game mods and Launcher mods.

Fixes:
- Remove File asks once more before it deletes a save that the game without mods also uses.
- The menu no longer takes clicks during the restart after Apply in the Mods window.
- Cancel on the multiplayer new-campaign screen really cancels. A single-player game you load after it is no longer hosted.
- The tutorial cannot be hosted in multiplayer. A message says that it is single-player only.
- Units Laboratory, Windows: units you save to My Units from the Gallery now show in My Units.
- No stutter on clicks in the Unit Builder and the Gallery.
- Tick boxes look like the game's own again.
- The Mods button keeps its place in the title menu when Multiplayer or Units Laboratory are off.

### 0.1.18 (2026-10-20)

- The title screen now shows the installed version in the top corner, under the Auto-update switch: "Dungeon Settlers Modded v0.1.18". Friends can check that they have the same version before they play together.
- The tick box under the world list in "Host a world" and Load Campaign, which shows the hidden worlds, was missing when Units Laboratory was on. The list said to tick the box, but only an empty dark bar showed. The box shows again.
- The mods are the same as in v0.1.17.

### 0.1.17.1 (2026-10-20)

- After loading a save, the game no longer runs slowly for the first seconds. The mod loader searched for the title menu on every frame for a while after each scene change, also inside a world, where there is no title menu. On the Windows test PC the first 10 to 15 seconds after a load ran at about 22 fps instead of 60. Loads are also slightly shorter.
- The mods are the same as in v0.1.17.

### 0.1.17 (2026-10-20)

- Mods window: Multiplayer and Units Laboratory are now listed as "Launcher features", in their own group below the game mods. They are no longer part of a world's mod set: a world saved with or without them loads without the "This world uses other mods" message, and a world's Mods tab shows only game mods.
- Hell difficulty 0.1.7: a new game with combat difficulty Hell no longer stays on the loading screen (Mac and Windows).
- Mac: when Steam runs but does not answer games, the start no longer says "Steam is not running" and no longer tries to start Steam again. It waits up to 30 seconds, then tells you what to do (quit Steam fully and start it again, or sign in).

### 0.1.16.1 (2026-10-19)

- BepInEx mods: the BepInEx console window no longer opens. BepInEx still writes its log file (BepInEx/LogOutput.log).
- The mods are the same as in v0.1.16.

### 0.1.16 (2026-10-19)

- Updates are signed: the mod loader installs only updates and mods that we signed.
- Windows: the mod loader needs Steam, as on Mac. If Steam asks you to sign in, the game waits for it.
- Windows: after applying mods or starting a new game, the game keeps its taskbar button (it no longer turns into a background process).
- Mods can keep their own data in a ModData folder. In multiplayer, a mod's data is synced when you join.
- Multiplayer: a host whose game asks a question when the world loads (Lock Consecration) can now let players join.
- For mod makers: DSMP Bridge 1.1.0 lets BepInEx mods use the multiplayer session, a random that is the same for every player, and chat lines. It is installed with the mods that need it.
- Windows: BepInEx mods that patch the same game code as our mods now work together.
- Each release now lists every game hook the mods use (hooks.json, hooks.tsv).
- Windows: when install.bat cannot find the game, it lists the folders it checked.
- Multiplayer: stricter test-only tools.

### 0.1.15 (2026-10-18)

- New mod: ZET-U Mod Suite. Use skills without their weapon, and take up to 8 units into the dungeon. The Inventory, Status and Skill windows show all of them. Off until you turn it on.
- Mod settings: mods with settings get a Settings button in the Mods window.
- Multiplayer: if your mods differ from the host's, "Get the host's mods" downloads the host's official mods and settings for you, then you join again.
- Crash reports: the game asks first. If you say yes, after a crash, a freeze, a desync or a failed join, the next start sends the logs, without names or IP addresses. You can turn it off in the Mods window.
- Global cooldown is now called "Global cooldown fix".
- Load list: when some worlds are not shown, a line says how many and why. They are not lost.
- Multiplayer: messages about lent units name the unit, even when it is on another map.
- Multiplayer: temporary files are no longer backed up to Steam Cloud.
- Long notices fit in their box.
- Windows: the Foreign Expedition hub now hides the Guild Contact button when recruiting is off, as on Mac.
- Windows: fixes checks of the Load list and the loading screen that could read the game wrong.
- Multiplayer: test tools now only active in test builds.
- After a Steam update of the game, the mod loader still checks for its own update, so a fix can reach you even while the mods are off.

### 0.1.14 (2026-10-18)

- Fixed: a crash in multiplayer after entering the dungeon more than once.

### 0.1.13 (2026-10-18)

- Custom Modes is now called Units Laboratory. Its title button shows a random recruit, new each time.
- Title screen: "ModLoader" under the game's logo. The title menu always fits the window and stays centred.
- Title menu: it no longer jumps after Multiplayer > Back.
- Start: the two logos now stay up while the game loads, timed to the load. Then the game's own loading screen and a progress line, until the title.
- Windows: fast start is on again.
- Windows: fixes the freeze at start on some PCs (black window).
- Your normal game saves now show in the Load list of the modded game. You play them in place, and the normal game sees your progress. Saves made only in the modded game stay apart. "Import a save" is gone.
- With Hell difficulty on, the game asks before it loads a normal save. Deleting a normal save in the modded game asks once more.
- Unit Builder: the look of the character sheet, search and filters (several choices at once), Level-ups and Presets tabs. You can start from a Wiki Build Gallery build. It shows the unit's portrait and its character in the game.
- Wiki Build Gallery: Compact Plus and List views, a Details preview, each unit's portrait and sprite. Race, background and weapon names are the game's own words.
- Test Fights: pick squad units one by one. Boss card and DPS check card. Battle kit names are translated.
- Multiplayer: the Players panel opens above the Teams window.
- Multiplayer: the "Watching" box has a Hide button.
- Multiplayer: hosts see their address in the Players panel, with a Copy button.
- Multiplayer: players watching an expedition now see it.
- Fixed: Windows PCs with ARM processors went out of sync with other players in the same game.
- Multiplayer on Mac: joining a host on the local network works again. If macOS blocks it, the game says so and opens the right setting.
- PvP: the Rules panel fits its text.
- Windows installer and install.bat in the game's 10 languages. The uninstaller removes everything it installed and keeps your saves, your own mods and your BepInEx / MelonLoader settings.
- Applying mods no longer says the game restarts.

### 0.1.12 (2026-10-17)

- Windows: fast start turned off for now (a start freeze on some PCs). Windows starts the way the normal game does.
- Mac: fixes a black screen at start on Macs with a notch. The game could stop while it was opening its window and stay black. It now starts normally.

### 0.1.11 (2026-10-17)

- Repairs mods that auto-update replaced with old builds.
- Fixes auto-update installing old mod builds. Versions v0.1.7 to v0.1.9 could take old mod builds from our early bundle releases and install them as "updates". If that happened to you, this version puts the right mod versions back: with Auto-update on it happens by itself, with it off the button in the top corner offers it.
- Reworked texts in the 9 other languages for auto-update, the update window, mod zip messages and the Windows folder warning.

### 0.1.10 (2026-10-17)

- The mods now speak the game's language. The Mods window, the title screen buttons, the auto-update corner, the multiplayer pages, the lobby, the Teams window, Custom Modes and the messages all follow the language you pick in the game's options: English, French, German, Spanish, Portuguese (Brazil), Russian, Japanese, Korean, Chinese (Simplified) and Chinese (Traditional). Chinese, Japanese and Korean use the game's own fonts. If you change the language in the options, the mod texts change at once, with no restart.
- Mod names and descriptions in the Mods window are translated too, and so are Hell difficulty's texts.
- On the multiplayer page, all the buttons have the same size.
- Fixes auto-update installing old mod builds: it now takes each mod only from that mod's own releases.

### 0.1.9 (2026-10-17)

- The game's own start logo is back. It shows the way it does in the normal game and fades out when the title screen is ready, and the start stays fast (about 3 seconds to the title on our test Mac).
- The "Auto-update" switch on the title screen: no tooltip covers it any more, so one click turns it on or off. It only shows on the title screen when nothing is loading.
- Windows: if the install folder has letters the game cannot use on your PC (for example Chinese, Japanese, Korean or Cyrillic letters on a Western Windows), Setup and install.bat tell you and ask for another folder, and the game says why instead of not starting. Accented letters (é, ü, ç) work.
- Still true from v0.1.8: with Hell difficulty on, the start is not sped up.

### 0.1.8 (2026-10-17)

- The game starts much faster. On our test Mac it reaches the title screen in about 3 seconds instead of about 13. The Unity splash screen is skipped. The very first start after you install takes a little longer, because the game checks its files once and remembers the result.
- On Windows (tested under Wine) the start to the title went from about 16-22 seconds to about 5.5 seconds.
- Applying mods is faster too: the restart takes about 3 seconds instead of about 6.
- Multiplayer: joining a game that is already running and reloading after a resync are a little faster.
- If a game file changes (for example after a Steam update), the game checks that file again the normal way. On a game build we do not know, the fast start turns itself off. With Hell difficulty on, the start is not sped up yet (Hell swaps the game's table file at the same place).

### 0.1.7 (2026-10-17)

- The game can update itself. Turn on "Auto-update" in the top corner of the title screen and a new release downloads in the background, gets checked, and installs the next time you start the game. With it off, a button tells you when an update is out.
- Your modded saves are backed up to Steam Cloud. The new "Import a save" button on the title screen brings a world over from your normal game, and it asks before it replaces anything.
- Saves are handled more carefully: a save you deleted on another computer is moved aside instead of coming back.
- Hell difficulty now warns you before it changes a save, and its extra tables load on every install.
- When you apply mods and the game restarts, a progress bar shows how far along it is.
- On a Mac with a notch, the game uses the whole screen, including the strip beside the notch, with no black band at the top.
- On Windows, user names and folders with accents or other non-English letters work now. If something the game needs is missing, it still starts, just with mods turned off, instead of failing.
- Fast join is on by default in multiplayer, and more network problems are fixed. If two players have different versions, the message now says which ones.

### 0.1.6 (2026-10-16)

- Multiplayer is safer to host. A player who joins your game can no longer crash it by sending an oversized command, and a connection that stops responding no longer stalls the game.
- If the host's save can't be sent while you join, you go back to the menu instead of waiting forever.
- Hosts of big worlds no longer drop out of their own game at the regular sync check. A long mod list doesn't crash the join anymore either.
- The Windows installer always uses its own "Dungeon Settlers Modded" folder and won't install into a folder that already holds other files. Uninstalling removes the game copy and the mods it put there, nothing else, so your saves and any files of your own stay put.
- Version numbers changed with this release. The installer bundle is now numbered after the loader (v0.1.6), and every mod has its own number. Older releases are named bundle-v0.1.10 to bundle-v0.1.16.

## Global cooldown fix (gcd-cdr)

### 0.1.7 (2026-10-25)

- It is now a fix: with the v0.1.26 mod loader it shows on the new Fixes tab of the Mods window, with "On in all worlds" (on by default).
- A world that has only fixes counts as "Vanilla & Vanilla+" in the Load Campaign filters.
- The game code of the fix is the same.

### 0.1.6 (2026-10-19)

- Built for the v0.1.16 mod loader. Nothing changes in the game.

### 0.1.5 (2026-10-18)

- New name: "Global cooldown fix" (was "Global cooldown"), in all 10 languages.

### 0.1.4 (2026-10-18)

- Built for the v0.1.13 mod loader. Nothing changes in the game.

### 0.1.3 (2026-10-17)

- Its name and description in the Mods window follow the game's language.

### 0.1.2 (2026-10-17)

- It plays the same as before; only the version number changed.

## Hell difficulty (hell-difficulty)

### 0.1.7 (2026-10-20)

- A new game with combat difficulty Hell no longer stays on the loading screen.

### 0.1.6 (2026-10-19)

- Built for the v0.1.16 mod loader. Nothing changes in the game.

### 0.1.5 (2026-10-18)

- Built for the v0.1.15 mod loader. Nothing changes in the game.

### 0.1.4 (2026-10-18)

- Built for the v0.1.13 mod loader. Nothing changes in the game.

### 0.1.3 (2026-10-17)

- Its name, description and difficulty texts follow the game's language.

### 0.1.2 (2026-10-17)

- It warns you before it changes a save.
- Its extra tables load on every install.

## ZET-U Mod Suite (zetu-suite)

### 0.1.3 (2026-10-25)

- Supplies step with 6 to 8 units: the units show in pages of 4 at the game's own size. Two buttons in the title bar, "1-4" and "5-8", change the page. With 5 units, one row.
- Result window with 5 to 8 units: the portraits show in two lines at the game's own size, and the window grows by one line. A party of 4 or fewer keeps the game's own line (before, its portraits were also made half size).
- Inventory, Status and Skill windows with 5 to 8 units: they never cover the pause and speed row or the alert rows. They use one row while it stays readable, else two rows.

### 0.1.2 (2026-10-19)

- Built for the v0.1.16 mod loader. Nothing changes in the game.

### 0.1.1 (2026-10-18)

- First release: use skills without their weapon, and take up to 8 units into the dungeon. It stays off until you turn it on in the Mods window; its options are on the mod's Settings page.
- Skills without their weapon (on by default once the mod is on).
- Units in the dungeon: 4 to 8 (4 by default). The Inventory, Status and Skill windows and the expedition screens show all of them.
- Before ZET-U plays one of your game saves, the loader backs it up.

## Sound settings fix (sound-fix)

### 0.1.1 (2026-10-25)

- It is now a fix: with the v0.1.26 mod loader it shows on the new Fixes tab of the Mods window. It is the same in all worlds. The game code of the fix is the same.

### 0.1.0 (2026-10-24)

- First release: the Volume sliders and Mute buttons of the Sound settings change the sound at once, at the title screen and in a world.

## Multi-unit view (multi-bar)

### 0.1.0 (2026-10-24)

- First release: select 2 to 4 of your units and each one gets its own card at the bottom of the screen, with its portrait, name, mood and status bars, behaviour buttons (F/G/H), status effects, item slot and its full skill bar.

## Weapon skill slot (weapon-slot)

### 0.1.2 (2026-10-25)

- Saves now keep the game's own form. Each unit is saved with the 4 free slots of the game: the weapon skill goes into the first empty free slot, or stays in the unit's skills with its auto-use choice. The weapon slot comes back when the world loads with the mod. A world saved with this mod opens in the game without it, so the mod no longer shows a save warning when a world loads. In the Load Campaign filter, its worlds count as "Vanilla & Vanilla+".

### 0.1.1 (2026-10-24)

- When a world finishes loading, a unit whose weapon slot is empty now gets its equipped weapon's skill there, the same way equipping the weapon gives it. This covers worlds played without the mod (where the slot was cut) and units that equipped a weapon while the 4 free slots were full. The unit also gets the skill if it did not have it.

### 0.1.0 (2026-10-24)

- First release: the skill bar gets a slot for the equipped weapon's skill, between the basic attack and the 4 free slots.

## Locked Slots (locked-slots)

### 0.1.2 (2026-10-25)

- The lock is now a small white padlock with a thin dark edge, about half the old size, in the slot's top-left corner. It grows and shrinks with the slot and the UI Scale.
- The item card of a locked slot has one more row: "Kept On Return". An empty locked slot shows a "Kept On Return" card too.
- English menu rows in Title Case: "Keep On Return" and "Do Not Keep On Return".

### 0.1.0 (2026-10-25)

- First release: right-click a slot in a unit's inventory and choose "Keep on return". A small lock shows on the slot, and what is in it stays with the unit when it comes back to the settlement.

## Live building HP (live-hp)

### 0.1.0 (2026-10-25)

- First release, a fix: when a building is selected, its durability number and bar in the bottom panel and its rows in the Inspect window follow its real durability when it takes damage or gets repaired, not only the value it had when you selected it.
- On by default, on the Fixes tab of the Mods window.

## ZET-U Mod Suite for BepInEx (zetu-suite-bepinex)

### 0.1.1 (2026-10-20)

- Fixes: no save could load on Windows (the backup copied a file twice). With 0.1.0, a world with a manual save (for example MyClan.json and MyClan_1.json) did not load.
- Now each save file is copied once, into a new backup folder, and the world loads. If a copy really fails, the world still does not load (your save stays safe).

### 0.1.0 (2026-10-19)

- First release: ZET-U Mod Suite as a plugin for plain BepInEx 6 (IL2CPP). It does not need the mod loader.
- Skills without weapon requirement (on by default): a unit can use the skills of a skill tree without holding that tree's weapon.
- Max units in the dungeon (4 to 8, default 4): the expedition window, the supplies step, the Inventory / Status / Skill windows and the result screen show every unit.
- Before a save loads with the mod on, the game asks once per world (Load / Cancel). Load first copies the world's save files to a backup folder.
- BepInEx's console window is switched off. The log file BepInEx/LogOutput.log stays on.
- If the mod loader's own ZET-U Mod Suite runs in the same game, this plugin stays off.

## Old bundles (bundle)

### 0.1.16 (2026-10-16)

- Two mods for Dungeon Settlers Modded bundle-v0.1.16, Mac and Windows. They are the same as in bundle-v0.1.15.
- Global cooldown (gcd-cdr 2.0.0) makes cooldown reduction shorten the global cooldown too.
- Hell difficulty (hell-difficulty 2.0.0) adds a harder difficulty. It stays off until you turn it on.

### 0.1.15 (2026-10-16)

- Two mods for Dungeon Settlers Modded bundle-v0.1.15, Mac and Windows.
- Global cooldown (gcd-cdr 2.0.0) makes cooldown reduction shorten the global cooldown too.
- Hell difficulty (hell-difficulty 2.0.0) adds a harder difficulty. It stays off until you turn it on.
