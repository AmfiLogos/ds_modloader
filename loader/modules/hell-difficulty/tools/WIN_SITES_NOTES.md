# Hell difficulty: Windows x64 sites, Steam build 25846389

Files: `../rva/25846389_win_patches.tsv` (23 patches), `../rva/25846389_win.tsv` (staff strip functions), check `check_win_patches.py` (passes: stock bytes, no overlap, whole instructions).

## How the sites were found
- Mac patch -> containing method by `~/ds_skill_truth/dump_25846389/script.json`; same name in `~/ds_multiplayer/win/25846389/dump/script.json`; disassembled Mac, old Windows 25284551 (`backups/win/stock_gameassembly.dll`, names from `dump_win_25284551/dump.cs`) and new Windows side by side.
- `CreateCampaignPresenter` methods (ChangeCampaignCombatDifficulty, ChangeCampaignManagementDifficulty, RefreshDifficultyIncreaseButtonState, IsIronModeForced) are instruction-for-instruction the same as 25284551; every old site moved by +0x35C10.
- String literal slots confirmed with `stringliteral.json`: env switch case 4 = `GAMESYSTEM_Environment_CountDownfall`, default = `CountHard`; grade case 4 = `TEXTKEY_Shared_SuperHigh`. Mood case 4 reads static field +0x40 (`TreatmentExpectationMoodDownfallDifficulty`) / +0x68 (`DungeonStayMoodPenaltyDownfallDifficulty`).

## Mac -> Windows mapping
| Mac | Windows | Why the count differs |
|---|---|---|
| 0xe62e3c combat lock b.lt | 0xb5e842 jl | |
| 0xe62e70 combat max | 0xb5e860 cmp 5 | |
| 0xe62e88 / 0xe62e90 / 0xe62eac / 0xe62ec0 iron branches | 0xb5e872, 0xb5e878, 0xb5e889, 0xb5e88e, 0xb5e8cf | MSVC computes the enter test with two setcc and the button interactable flag separately (setne dil), so 5 sites |
| 0xe628a8 IsIronModeForced | 0xb5fcf3 | dead on both (no direct caller) |
| 0xe63080 / 0xe630b0 mgmt lock, max | 0xb5ea5c, 0xb5ea5e | |
| 0xe627d4 / 0xe627dc / 0xe627ec / 0xe62804 Refresh | 0xb61ac7, 0xb61b15, 0xb61b32, 0xb61b3d, 0xb61b41 | Windows has two copies of the combat `== 4` max test (cleared / gate path) |
| 0xf3cd94 env fallback | 0xc43c3a + 0xc43da5 | Windows INLINES GetCombatEnvironmentCountValues into GetCombatRegionalEnvironmentRows (no direct caller left); Mac calls it |
| 0x842ed4 / 0x842fbc mood, stay | 0x5c1e12, 0x5c1ce2 | Windows also calls both from CampaignBalanceRuntime.ResolveSnapshot (Mac: via CreateSnapshot); both reach the patched body |
| 0xf3d634 grade | 0xc43081 | |
| 0xf3d6d8 colour | 0xc44176 | |
| 0xf3bf5c / 0xf3c054 mood rows colour | 0xc4427e, 0xc443a4 | value-colour switch inlined twice, same as Mac |

## Not patched
- `IsDownfallDifficultyLocked` (0xB5FB50, both overloads folded at one address, nothing else there). The 25284551 Windows table patched it (sete->setae at body+0xC = 0xB5FB5C, bytes `0f94c0`->`0f93c0`). Mac 25846389 leaves it stock; no direct caller on Mac or Windows (inlined into the ChangeCampaign*/Refresh copies, which are patched). Left stock to match Mac; adding it would be harmless.

## Inlining / folding audit
- Byte scan of the whole Windows .text for the 5-way difficulty switch shape: only the 8 switch sites above in difficulty code (standalone + 3 inlined copies).
- Every patched body has a single name in script.json (no identical-code folding with unrelated methods).
- 25284551 Windows table missed the 3 inlined sites (its GetCombatRegionalEnvironmentRows and GetManagementMoodRows had the same inlined copies at 0xC25875, 0xC25D4E, 0xC25E74): old Windows Hell showed Hard env counts and plain mood-row colour in the tooltip.

## Small differences with Mac (unreachable)
For a negative difficulty value, Windows mood/stay/grade fallbacks go to Downfall/SuperHigh, Mac to the Medium case. Difficulty is never negative.

## Staff strip (25846389_win.tsv)
- DetermineInitialEquipments 0xA7E480 (prologue `mov [rsp+8],rbx`, 5 bytes), ParseItems 0xA80CE0 (prologue `push rbp; push rsi; push r15`, 5 bytes). No rip-relative operand in either first 5 bytes.
- One direct `call` (E8) to ParseItems at 0xA7E525 (no tail jump), so ParseItems_return = 0xA7E52A. Same offset +0xA5 as 25284551 (WIN_EQUIP_CALL). On Windows `this` (rcx) IS passed to ParseItems (`mov rcx, rdi`).
- Other ParseItems callers: CreateRecruitCandidate x2 (explicit item lists), DetermineInitialInventoryItems; same as Mac. DetermineInitialEquipments callers: 3 on both (not inlined).
- Offsets same on Windows dump.cs / il2cpp.h: RecruitHelper._campaignSetting 0x30; CampaignSettingContainer.CampaignStartSetting 0x20; CampaignStartSetting._combatDifficulty 0x1C; List<ValueTuple<string,int>> _items 0x10, _size 0x18, _version 0x1C; array m_Items at 0x20, entry 16 bytes (string* + int32 + pad).
- `hell.c` now reads `rva/<build>_mac_patches.tsv` only; a Windows build needs it to pick `_win_patches.tsv`.
