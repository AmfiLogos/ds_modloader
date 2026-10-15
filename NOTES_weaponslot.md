# Weapon-skill slot: client or gameplay? (launcher lane, 2026-10-15)

Test: tests/weaponslot_pair.py (host WITH the mod, 5 quick slots; guest stock). Results in test_env/pair/.
- Run 1 (05:01, full-state check on): 24 host casts of the weapon skill (UseSkillTargetRequested, the real input
  path), guest replayed them with the same cooldowns (e.g. SummonBubbling host 11.58 / guest 11.49), all step
  fingerprints equal. At step 7200 the full-state (save) check differed: PlayerUnitsSaveData/QuickSlots (host saves 5
  slots, stock guest 4) -> desync -> everyone reloads. Report: test_env/pair/old/desync_reports_CD_0503.
- Run 2 (06:19, --minutes 10 --no-full-check): 10.2 min, 134 casts sent, 34 732 steps with equal fingerprints,
  0 desyncs, units equal at the paused step, 190 orders applied on both.
Verdict: the SIMULATION is client-safe, but the mod changes the save's QuickSlots layout, which the lan_v10 full-state
check compares. So it stays GAMEPLAY for now. It becomes CLIENT if the full-state check skips
PlayerUnitsSaveData/QuickSlots (per-player UI layout; not simulation). That change is in src/mp_game.c (full_sections),
not in this lane. Logged in MORNING_QUESTIONS.md.
