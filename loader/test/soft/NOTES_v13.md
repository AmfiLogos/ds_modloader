# Soft restart v13: Apply only at the title, no settle wait, prewarm (lane loaderui, 2026-10-16)

Owner: "No mod restart inside a world. To change mods, go back to the title screen. Apply only at the title.
Faster restart at the title if possible." (PLAN_V13 R5)

## What changed (src/dsl_soft.c, start/mac/dsl_soft_mac.c, src/dsl_soft_os.h)
- `dsl_soft_apply` refuses unless the game is at the title (scene 1), not loading: log line
  `soft: apply refused: mods change only at the title (scene N)`, returns -1. The world path is gone (pause, the
  game's autosave, newest-save search, camera in the handoff). Flow K stays: `DSL_SOFT_LOAD=<save>` from the title,
  B opens that save before it is shown.
- No settle wait at the title: B puts its loading screen up as soon as the title is ready (was 0.3 s settle + 0.4 s
  minimum fade wait). "Loading screen up" = the fade done flag seen low after the call, then high (no fixed wait).
  Flow K keeps its 0.3 s settle after the world load.
- Prewarm (new, signatures as asked):
  `int dsl_soft_prewarm(const char *profile, const char *env_lines)`, `void dsl_soft_prewarm_stop(void)`,
  `int dsl_soft_prewarm_state(void)` (0 none, 1 booting, 2 ready and waiting).
  - Refused (-1) when not at the title, title loading, a restart runs, this process is a B, or env_lines has
    `DSL_SOFT_LOAD=`. Same profile content + same env_lines as the waiting B: kept (no restart).
  - B loads a snapshot of the profile: `<profile folder>/prewarm-<name>` (pending.json -> prewarm-pending.json, so
    commit_pending still turns it into active.json, but only once B is on screen: dsl_soft_wanted() stays 1).
  - Apply with the same profile FILE CONTENT and the same env_lines string uses the waiting (or still booting) B.
    Anything else: the prewarm B is killed and a cold B starts.
  - A waiting B: hidden as before, muted (AudioListener pause + volume 0, the game's values back at the swap; this
    now applies to every hidden B), 10 fps (DSL_SOFT_IDLE_FPS), loading screen up. The game fades its own title in
    2-3 s after the title is ready; the waiting B puts its loading screen back up, and at "go" shows only with it up.
  - Never alone: A gone -> B quits (0.31 s measured). A enters a world -> A stops it. Cap DSL_SOFT_PREWARM_MAX s
    (default 1800): A stops it; B quits by itself 60 s later. dsl_soft_busy() stays 0 for A while a B waits.
  - B's "ready" file holds B's pid; A accepts only the pid of its current child. Killed children are reaped
    without blocking (no zombie).
- Test only: `prewarm_request.json` ({"profile","env"} or {"stop":1}) in DSL_SOFT_TEST mode; `DSL_SOFT_BOOTLOG=1`
  logs every step of the game's boot coroutines (MainFlow.Start, AddressableInitializer.*) with frame count.
  In the test stage without activation the shown window goes to the back (orderBack) instead of the front: on top,
  the system made a staged test copy the front app for a moment while focus moved between other apps (4 guard kills).

## Proof (copy V = APFS clone of copy S, Steam gate off, hidden + muted, stage -20000,-20000, no activation; leases loaderui)
Tool: `test/soft/v13_test.py cold|prewarm|world|life|flowk`. Out: `test_env/soft_out/v13_final_*`.
Load average 6-9 (other lanes running). "In game" = A's dsl_soft_apply -> B's window shown (B's own log line).

| case | run 1 | run 2 | run 3 |
|---|---|---|---|
| title, cold Apply, before v13 (NOTES.md) | 7.23 s | 7.42 s | 7.56 s |
| title, cold Apply, v13 (v13_cold4, load 8) | 6.12 s | 6.10 s | 6.25 s |
| title, cold Apply, v13 final binary (load 9) | 6.56 s | 7.60 s | 7.35 s |
| title, prewarmed Apply, final binary | 0.756 s | 0.758 s | 0.761 s |
| flow K at the title (into the world), final | 14.16 s | 13.64 s | 13.76 s |

- Prewarm: B ready 6.2-7.3 s after the prewarm call. The swap = A's own fade to its loading screen (0.75 s); B is on
  screen 10-20 ms after that. Request file -> shown 0.89-1.25 s (includes the 0.5 s test file poll).
- Checks green: cold 6/6, prewarm 21/21 (B muted, idle 10 fps, window server alpha 0, A answers at the title while
  B waits, the prewarmed B used, right module count, A exited), world 5/5 (prewarm stopped when A entered the world;
  Apply REFUSED; prewarm REFUSED; no new process; A still in its world), life 4/4 (same set kept; cap stop at 20.2 s;
  set mismatch -> cold start; A killed -> waiting B quit in 0.31 s), flow K 6/6 (world check refused, B in the
  world with gcd-cdr + multiplayer, pending -> active.json, A exited). Guard: 0 violations on the final binary.
- Captures (the game's own frames, v13_cap3): A title -> A loading screen -> B loading screen -> B fades in to the title.
- Waiting B cost: 9.9 % of one core, 2.35 GB memory (A at the title: 43.5 % in the same test copy).
- Host tests `test/run_tests.sh`: 31 passed, 0 failed. Windows `start/win/build_win.sh`: baselib.dll and the
  starter build (one old warning in dsl_ui.c).

## Step 3: B's boot by step (DSL_SOFT_BOOTLOG=1, v13_steps2; frames uncapped ~1500/s)
| step | time | what |
|---|---|---|
| process start -> MainFlow.Awake | 0.46 s | Unity start, splash already cancelled |
| LoadSheets | 0.22 s | 38 tables |
| LoadFont | 3.83 s | ONE Addressables load: fonts_shared bundle, 344 MB (all languages); ~2 cores busy |
| LoadCursor + LoadTiles | 0.13 s | |
| LoadUIPrefabs | 0.50 s | |
| title scene up (11 frames) | 0.91 s | |
| title ready -> loading screen up -> "ready" | 0.02-0.05 s | was 1.06 s |

Nothing safe to cut: every step loads what the shown game needs. LoadFont is the big one; loading only the current
language's font would change what the game has in memory (a language switch later would break), so it is not "the
same as stock". The cut that is safe was the settle wait (1.0 s). The rest is hidden by prewarm.

## Open risks
- Steam: a prewarm B is a second process with the same app id for up to 30 min. Not tested (copies run with the
  Steam gate off).
- The multiplayer module binds `<DSRIG_DIR>/ctl.sock` with unlink + bind: a prewarm B takes A's control socket; if
  that B is stopped, A has none until its next start. Tests use another DSRIG_DIR for B.
- Windows: not run; a prewarm runs bie_prepare (BepInEx plugins of the pending set into the game copy) at spawn.
- Not on screen in tests: real activation at the swap (NOACTIVATE), Dock, full-screen.
- Old tests now hit the refusal by design: soft_test.py round 2 (world), l1_test.py flow J in a world.
