# "App Running in Background" notice (owner item 7, v13)

Notice seen on macOS 27.2 (26B5101f): *App Running in Background: "DungeonSettlers" is running in the background.
You can manage background apps in the Dock and in System Settings.*

## When it fired (owner's unified log, read only, 2026-10-15)

It did not fire at the start. It fired 33 s after the first soft restart (Mods window, "Apply").

| time | event |
|---|---|
| 23:55:20 | stub `DS Modded` started by LaunchServices (ASN 0x4756752), Steam wait |
| 23:55:42 | stub `execv` -> game A (pid 46262), same ASN, RunningBoard job `application.com.amfilogos.ds-modded...` |
| 23:55:43 | A starts its AutoFill XPC helper `com.apple.SafariPlatformSupport.Helper` (pid 46557) |
| 23:57:00 | Apply -> A `posix_spawn`s B (pid 46953): RunningBoard `anon<Dungeon Settlers>`, same coalition 165226 as A |
| 23:57:08.689 | B starts its own AutoFill helper (pid 47007) |
| 23:57:08.734 | A exits (WindowServer "Process death 0x0-0x4756752") |
| 23:57:08.763 | Dock: `Tile started running in background` |
| 23:57:41.840 | backgroundtaskmanagementd: `checkTasksAllowedInBackgroundForAppURL shouldRegister:true ASN 0x4756752` (A's ASN), appURL `.../play-modded/DungeonSettlers.app` |
| 23:57:41.863 | `should post background tasks=true` -> usernoted: BTMNotificationAgent, category `com.apple.BackgroundTaskManagement.backgroundtasks` = the notice |
| 00:01:10 | B exits -> Dock: `Tile stopped running in background` |

`sfltool dumpbtm` now has "DungeonSettlers - background tasks" (8192.com.CanOpener.Dungeon-Settlers.Modded, notified).
"DungeonSettlers" in the text = the play copy's folder name `DungeonSettlers.app`.

## Cause

B is started with `posix_spawn`, so it stays inside A's launchd job. The XPC helpers that macOS starts for B (AutoFill,
`com.apple.SafariPlatformSupport.Helper`) are booked on A's app. When A quits after the swap, A's Dock tile still has a
live helper, so the Dock marks the tile "running in background" for as long as B runs. After about 30 s macOS 27 asks
Background Task Management and posts the notice (once per bundle id per Mac).

Not the cause: the stub (it becomes A by `execv`; nothing of it is left at the swap), dsml-helper (exits before the
game), B's UIElement / Accessory switch (tested alone: no notice), a child app alive in A's job without an XPC helper
(tested: no notice).

## Reproduction without the game (toy apps, scratch only, nothing of the game)

`BGStub.app` (started by LaunchServices, `execv`s another bundle like the stub) -> `BGProbe` parent -> child. The parent
exits 6 s later; the child lives 48 s.

| child | AutoFill helper in child | Dock "running in background" | BTM shouldRegister:true / notice |
|---|---|---|---|
| posix_spawn, Regular | no | no | no |
| posix_spawn, UIElement -> Accessory -> Regular (like B) | no | no | no |
| posix_spawn | yes | **yes** (01:04:00) | **yes** (01:04:33, banner posted) |
| posix_spawn + responsibility disclaim | yes | **yes** | **yes** |
| NSWorkspace openApplicationAtURL, new instance | yes | no | no |

The NSWorkspace child got its environment (`DYLD_INSERT_LIBRARIES` marker dylib loaded, custom variables set) and the
completion handler came on a background queue (a main-thread semaphore wait took 0.084 s). It starts in "/".

## Fix

1. `loader/start/mac/dsl_soft_mac.c` (owned by another helper): B through LaunchServices
   (`NSWorkspace openApplicationAtURL:configuration:` with a new instance, no activation, the full environment, the
   same arguments). B then has its own launchd job, and its helpers are booked on B. A falls back to `posix_spawn`
   when LaunchServices does not answer. B is no longer A's child: `dsl_sos_child_exited` uses `kill(pid, 0)`.
   Exact change: `app/stub/dsl_soft_mac_bg.patch` (`git apply app/stub/dsl_soft_mac_bg.patch`).
2. `loader/start/mac/dsl_start_mac.c` (this branch): LaunchServices starts B in "/", so A passes `DSL_SOFT_CWD`
   (its start folder, the profile folder with `steam_appid.txt`) and the constructor goes there before the soft snapshot.
   No-op without the patch.

## Game test (patched loader, test copy S, hidden, muted, stage off screen, lease CD)

`loader/test/soft/soft_test.py` with `DYLD_INSERT_LIBRARIES` = loader built from this branch + the patch: round 1
(title) shown 7.87 s after the request, round 2 (world, speed Fast) shown 15.77 s after, B2 in the world, A exited both
times, 0 guard violations. dsload.log: `B started by LaunchServices`, `DSL_SOFT_CWD ...: chdir ok` in both rounds.

A test copy has LSUIElement + LSBackgroundOnly (lead rule 2026-10-15): it has no Dock tile and starts no AutoFill
helper, so the notice itself cannot show in a game test copy. The before / after proof of the notice is the toy test.
Check on a player copy after the merge: at a soft restart, `log show --predicate 'process == "Dock" AND category ==
"tile"'` must show no "Tile started running in background".

## Side effects of this work on the owner's Mac

- One "BGProbe is running in the background" banner at 01:04:33 (toy test, owner away) and a "BGProbe - background
  tasks" row in System Settings > General > Login Items & Extensions. `sfltool` has no single-item removal; harmless.
- The DS row is already "notified" on this Mac, so he would not see the DS notice again for this bundle id anyway;
  every new player would see it once, at the first soft restart, without the fix.
