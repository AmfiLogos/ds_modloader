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

## Weaponslot lane, 2026-10-15 evening: verdict GAMEPLAY (owner had said client is fine; the test says no)
- Offline (Mac arm64 25846389): no player can turn auto-use on for the WEAPON slot. ToggleAutoSkillRequested is built only
  in InputEventFactory.ConvertUIEventToEventData (0xD37008) from the right-click menu options SetAutoSkill1..4 /
  UnsetAutoSkill1..4; the mod's decoders (0xD37A00 `sub #0xca`, 0xD37A40 `sub #0xce`) give Index 1..4, never 0, and its cave
  at 0xF22250 (QuickSlotUI <Init>b__2) opens no menu on the weapon slot. The weapon skill's own AutoUseByDefault is sim, same on both.
- Pair AB, dylib with the QuickSlots skip (ds_multiplayer branch weaponslot 0e8b819):
  - 7.7 min, full-state check ON, 100 weapon-skill casts: 2 full-state checks equal, 0 desyncs, 26 037 equal fingerprints.
  - 7.2 min `--auto last`: the slot-4 skill (EmergencyEscape) was auto ON by default on both: 0 desyncs (no toggle sent).
  - 5 min `--auto last --flip`: host toggles auto at quick-slot index 4 (bar slot 5, the 4th free slot). Host: EmergencyEscape
    auto OFF; stock guest (4 slots = the host's first 4) keeps it ON. Full-state check at step 10800: EntityComponentSaveData
    differs -> desync + reload (report A/data/desync_reports/20261015-212438-player1-step10800). Fingerprints still equal
    in 5 min only because that skill had not fired yet; AutoUse drives DecideSkill, so the fights would split later.
- Why: index orders (ToggleAutoSkill, Set/Unset/MoveQuickSlot) are replayed by index and resolved against each copy's own
  quick slots. Same for Set/Move into slot 4 (not tested). Client-safe would need the multiplayer mod to send these by skill key.
- So: the mod stays GAMEPLAY; the QuickSlots skip was reverted (7aa7032); no "scope": "client" anywhere.
- Test: tests/weaponslot_pair.py `--auto weapon|last`, `--flip`, `--dylib`, env WEAPONSLOT_ROOT (pair_guard.py honours it too).
