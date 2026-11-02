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
- Pair AB, dylib with the QuickSlots skip (ds_multiplayer branch weaponslot f24ed79):
  - 7.7 min, full-state check ON, 100 weapon-skill casts: 2 full-state checks equal, 0 desyncs, 26 037 equal fingerprints.
  - 7.2 min `--auto last`: the slot-4 skill (EmergencyEscape) was auto ON by default on both: 0 desyncs (no toggle sent).
  - 5 min `--auto last --flip`: host toggles auto at quick-slot index 4 (bar slot 5, the 4th free slot). Host: EmergencyEscape
    auto OFF; stock guest (4 slots = the host's first 4) keeps it ON. Full-state check at step 10800: EntityComponentSaveData
    differs -> desync + reload (report A/data/desync_reports/20261015-212438-player1-step10800). Fingerprints still equal
    in 5 min only because that skill had not fired yet; AutoUse drives DecideSkill, so the fights would split later.
- Why: index orders (ToggleAutoSkill, Set/Unset/MoveQuickSlot) are replayed by index and resolved against each copy's own
  quick slots. Same for Set/Move into slot 4 (not tested). Client-safe would need the multiplayer mod to send these by skill key.
- So: the mod stays GAMEPLAY; the QuickSlots skip was reverted (93ce3f8); no "scope": "client" anywhere.
- Test: tests/weaponslot_pair.py `--auto weapon|last`, `--flip`, `--dylib`, env WEAPONSLOT_ROOT (pair_guard.py honours it too).

## 2026-11-01 (lane nvwslot): weapon-slot 0.2.0 is CLIENT ONLY (owner: "it's client only")
- The slot is now a view: the game's data keeps 4 quick slots, the saves are the game's own, the AI order is stock.
  The 6th bar button shows the equipped weapon's skill (when the unit has it) and a click goes through the stock click
  path: UseQuickSlotSkillRequested Index 5 -> PlayerControlEventHandler -> GetSkillAt(unit, 4), which the module answers.
  The cast is the game's own order UseSkillTargetRequested (by skill key), so a stock player applies it the same way.
- What client-only cannot do (owner questions in ~/ds_multiplayer/ext/nvwslot/PLAN.md): weapon skill first in auto-use,
  one more free slot, a weapon skill for a unit that equipped with a full bar.
- Tests: loader/test/weapon_slot_test.py (single player), loader/test/weapon_slot_pair.py --mod host|guest (pair, mixed).
  The 0.1 tests above are in tests/old/.

## 2026-11-02 (lane nvwslot3): greyed button, "hide the copy", the 0.2.0 hold
- Stock facts (hidden copy, build 25846389): a unit HAS its weapon skill only while the skill sits in one of its 4 free
  slots. Equip with a full bar: no skill (ProcessEquip adds it only when TryRegisterQuickSlot found a slot). The stock
  "Remove from Quick Slot" (right-click menu; UnsetQuickSlotRequested, ord_events_gen.h order 34) also removes the skill
  (InputEventHandler 0xD9789C: SkillComponent.RemoveSkill when the key is in no other slot): auto-use ends, the skill window
  keeps it (extra skills). So the 0.2.0 button is always a copy of a free slot, and sending the stock Remove for the
  "duplicate" would also empty the button: not built.
- The unit does not have its weapon's skill: the button shows the skill greyed (SkillSlotUI.SetActivate(false)), no
  auto-use mark, and the game's simple hover card (TooltipFeedback.SetSimpleText, the locked-slots pattern): "Free a quick
  slot, then equip the weapon again" (10 languages). A click does nothing (GetSkillAt answers null).
- Setting "hide_copy" (mod.json settings, scope local, default off: owner 2026-11-02): the free slot that holds the
  weapon skill is drawn empty on this screen (the view array gets null there; its per-frame refresh goes to the weapon
  button). Game data unchanged: that slot's key still casts it, auto-use is the same, a dropped skill replaces it (stock).
  Live: dsl_on_setting_change. Test request {"cmd":"setting",...} (loader/src/dsl_reg.c, DSL_SOFT_TEST only) sets it as
  the Mods page does.
- The bar now re-reads the unit at each bar rebuild (InitSkillUI / RefreshSkillUI), so the button no longer keeps a
  removed skill's icon for up to 30 frames.
- The 0.2.0 hold of v0.1.30 lived only in the release folder (rebuild script: the published 0.1.2 zip copied over
  build/mods_all/{mac,win}/weapon-slot; publish script: weapon-slot in its "same as before" list). The repo always had
  0.2.0. A release lane must not copy that overlay: the next kit ships the 0.2.0 built from this tree.
- Tests: weapon_slot_test.py (22 checks: greyed + hover text, hide_copy on/off live, slot key still casts, save is the
  game's own); weapon_slot_pair.py with WS_HIDE_COPY=1 (the player with the mod hides the copy; the pair stays in sync).
