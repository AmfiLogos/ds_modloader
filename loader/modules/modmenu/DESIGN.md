# modmenu module: design notes (lane modsmenu, not built yet)

Status: no code yet. `slogel.png` = Mud Slogel idle frame 0 (32x36, from MudSlogel_MS_Idle_sheet.png x 0..31).
Facts below were checked in the mac dump (~/ds_skill_truth/dump_25846389/dump.cs) and by disassembly of the stock
GameAssembly (Steam `GameAssembly.dylib.stock_25846389`), 2026-10-15.

## Hooks: avoid MP's 112 hooks (the engine refuses a second hook on one function)
- Frame tick: `UnityEngine.EventSystems.EventSystem.Update` RVA 0x54B0D68 (MP does not hook it; prologue is plain
  `stp`, hookable). Runs every frame where UI exists. Run once per frame (guard by Time.frameCount).
- MainFlow: no hook; FindObjectOfType(Refactor.Main.MainFlow) (FindObjectOfType 0x50FD97C), scene = +0x18C.
- persistentDataPath 0x507B74C: hook it ONLY when `host->module_loaded("multiplayer")` is 0 (MP hooks it to
  `<DSRIG_DIR>/data`); same answer, so saves stay in one place when MP is switched off.
- Everything else: polling (Input.GetMouseButtonDown 0x51ACDD0, Input.mousePosition 0x51AD4DC, rect_of via
  RectTransform.GetWorldCorners 0x51089D0) or crafted delegates (below).

## Crafted System.Action delegates (no hook needed for stock buttons)
il2cpp invokes an Action inline as `x2=[d+0x18] (invoke_impl); x0=[d+0x40]; x1=[d+0x28]; br x2`
(seen in SubUI_TitleMenuButton.OnPointerClick 0xE6FF20 and CommonNoticeView.OnClick_Confirm 0xF65A18).
So: il2cpp_object_new(System.Action), set +0x18 = C function `void fn(void *ctx, void *mi)`, +0x40 = ctx, +0x28 =
any non-null. Use it for:
- the title "Mods" button: clone of SubUI_TitleMenuButton (TitleMenuView +0x50 Credits as source, as mpmenu does),
  `_onButtonClicked` +0x140 = crafted Action. NEVER leave +0x140 NULL on a clone that MP's hook passes on: the stock
  code throws NullReferenceException (bl 0x6FB9D8) when it is NULL and the button is interactable (+0xD8).
- the world dialog: CommonNoticeView.Show_Select(text, onConfirm, onCancel, yesNo) 0xF652A8 with two crafted
  Actions; raw text via tmp_set on +0x30 (_noticeText), labels +0x40 / +0x50 (as mpmenu's leave_box_show).
  MP's ui lane hooks OnClick_Confirm/Cancle; they pass boxes that are not theirs to the original, which invokes ours.

## Mods per world
- Keep dsl_world.c's mechanism: DSML_SESSION = `<data>/Mods/worlds.json` {"modset": <running key>, "worlds":
  {guid: key}}; key = sorted gameplay mod ids joined by "+" ("vanilla" if none). modmenu writes "modset" at
  plugin_init from host->gameplay_mods() (modmenu itself: scope "client").
- Register a world: in scene 2, campaign loaded, guid (campaign data +0x50, +0x10, as mpmenu campaign_guid) not in
  worlds -> worlds[guid] = running key.
- Mismatch: small dsl_world.c change: after the refusal (state -2 + CancelIronModeLoad, which closes the load and
  shows a stock notice), call an optional module export `dsl_world_mismatch(save_name, guid, world_key)`; modmenu
  then (1-2 frames later) re-uses the shown CommonNoticeView with Show_Select "This world uses: X. Load it with
  them?" [Load] [Cancel]. Load = soft restart into that save.
- Load screen: SaveLoadView (Refactor.Main) get_CurrentSelectedSaveName, header via
  CampaignLoadingEventHandler.TryGetSaveHeader 0xD6CA44 (self = MainFlow +0x50 -> +0x28); line under _lowLoadPanel
  +0x98: "Mods: <names> [Change]". New Campaign: SubUI_CampaignSetting, same line for the running set.
- Load a save by name (fallback after a plain restart): obj_new(md 0x6651118) LoadCampaignRequested, ctor
  0xDCFD78(ev, name), publish 0xCCFAD4(MainFlow +0x48, ev) (rig_cmd.c c_load_save).

## Soft restart: one function
`host->soft_restart(profile, env_lines)` is coming from lane softrestart (appended to DSLHost; check `size`).
Asked them (2026-10-15 ~18:30) for env line `DSL_SOFT_LOAD=<save name>` = restart into that save. Call it with a
pending profile `<data>/Mods/pending.json`; B copies it over active.json once it runs (commit only on success).
Fallback until it lands: write handoff.json {"load": save}, `open -n $DSML_APP`, quit.

## Files (stub contract, app/BRIEF_stub.md, commit 2614928)
DSL_PROFILE = `<data>/Mods/active.json` ({"enabled","order"}, modmenu always enabled), groups.json, worlds.json,
DSML_DATA, DSML_APP, DSL_MODS_DIR = ROOT/loader/mods. Ship modules: multiplayer (on by default), gcd-cdr, modmenu.

## Tab frame for other lanes (proposed export)
`int modmenu_add_tab(const char *title, void (*build)(void *content_tr, void *tpl_img, void *tpl_txt),
void (*frame)(void *content_tr, int shown))`; tabs: Mods, My Units, Gallery, Test Fights, Unit Builder.
