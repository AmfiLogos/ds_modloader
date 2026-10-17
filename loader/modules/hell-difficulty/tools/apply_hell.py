#!/usr/local/bin/python3
"""Add Hell as a sixth difficulty AFTER Devastation (enum value 5).

Mac Apple Silicon, Steam build 25846389 (DS_B.0.4.29) ONLY. The script
refuses any other GameAssembly (LC_UUID of the arm64 slice + stock bytes at
every site). The loader's Hell module uses only the table part (edit_tables,
through hell_delta.py); its code changes are in hell.c, not this dylib patch.

Does not retune Devastation. Game must be closed for an in-place install.

Tables: new affecter / text / starting rows, 6th slot on cost arrays.
GameAssembly: raise the +/- max from 4 to 5, Ironman on Hell, mood/env
fallback uses Devastation numbers. Hell uses the same Very-Hard-clear
unlock as Devastation. On Hell only, a recruit still rolls Mage / Druid /
Prodigy / Cursed Child, but staff keys are dropped from DefaultEquipments
(empty weapon slot). Other difficulties keep the staff.

The dylib patch only touches its own sites and its own cave range
0x604BC00-0x604BEFF, so it runs on top of notch LC_LOAD + weapon slot
(0x604AD70-0x604AFEF) + multi-bar stubs (0x604B780-0x604B977, marker
0x604BFF0). GCD cave 0x604AC40-0x604AD6F is left alone (GCD stays off).

Staged use (no live files touched):
  apply_hell.py --install --dylib-only --no-sign --ga IN.dylib --out OUT.dylib
  apply_hell.py --install --tables-only --aa AA_DIR --out-dir STAGE_DIR
In place (reapply_all.sh style): omit --out / --out-dir.
"""
from __future__ import annotations

import argparse
import copy
import os
import shutil
import struct
import sys
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = "25846389"
ARM64_UUID = bytes.fromhex("3C5DD8BDE3713C0EA674A81042B0AC9F")
TEXT_VMSIZE = 0x604C000
FACTOR_166 = 1.659999966621399
NOP = bytes.fromhex("1f2003d5")
FAT_MAGIC = 0xCAFEBABE
CPU_ARM64 = 0x0100000C

MONSTER_HELL = [
    "MovementSpeedPerModifier_15",
    "AttackSpeedPerModifier_20",
    "CriticalChance_10",
    "DodgeChance_15",
    "CriticalDamageBonus_50",
    "MaxHealthTotalPerModifier_100",
    "PhysicalAttackPowerPerModifier_100",
    "MagicAttackPowerPerModifier_100",
]

MGMT_KEYS = (
    "GAMESYSTEM_Management_CraftingIngredientAmountFactorByDifficulty",
    "GAMESYSTEM_Management_ConstructionMaterialAmountFactorByDifficulty",
    "GAMESYSTEM_Management_ResearchRequiredWorkloadFactorByDifficulty",
)

# The Hell rows' texts, one per game language: ../lang/hell_texts.json (key -> {code -> text}; "" = that language keeps
# the copied Downfall cell, with Devastation's colour tag made Hell's, as before). Game language code -> TextKeyTable
# column (Refactor.Setting.LanguageType; pt-BR: an empty PortugueseBrazil cell shows the Portuguese one in the game).
HELL_TEXTS = HERE.parent / "lang" / "hell_texts.json"
LANG_COLUMN = {"en": "English", "ko": "Korean", "fr": "French", "de": "German", "ru": "Russian",
               "zh-Hans": "ChineseSimplified", "zh-Hant": "ChineseTraditional", "ja": "Japanese",
               "es": "Spanish", "pt-BR": "PortugueseBrazil"}


def hell_texts() -> dict:
    import json
    t = json.loads(HELL_TEXTS.read_text(encoding="utf-8"))
    for key in ("TEXTKEY_CreateCampaign_5Difficulty", "TEXTKEY_CreateCampaign_5CombatDifficulty_DESC",
                "TEXTKEY_CreateCampaign_5ManagementDifficulty_DESC"):
        if not (t.get(key) or {}).get("en"):
            sys.exit("%s: no English text for %s" % (HELL_TEXTS, key))
    return t


GAME_APP = Path.home() / (
    "Library/Application Support/Steam/steamapps/common/"
    "Dungeon Settlers/DungeonSettlers.app"
)
DEFAULT_MAC_AA = GAME_APP / "Contents/Resources/Data/StreamingAssets/aa"
DEFAULT_MAC_GA = GAME_APP / "Contents/Frameworks/GameAssembly.dylib"

# Steam 25846389 arm64 RVAs, derived by method symbol from dump_25846389
# (every method below is byte-identical to 25284551 apart from metadata
# slot immediates, so in-method offsets are unchanged).
#   ChangeCampaignCombatDifficulty     0xE62DAC
#   IsIronModeForced                   0xE628A4
#   ChangeCampaignManagementDifficulty 0xE62FF0
#   RefreshDifficultyIncreaseButtonState 0xE6278C
#   Tooltip_Difficulty.GetCombatEnvironmentCountValues 0xF3CCFC
#   Management.GetTreatmentExpectationMoodValues 0x842EA0
#   Management.GetDungeonStayMoodPenaltyValues   0x842F88
#   Tooltip_Difficulty.FormatManagementDifficultyGrade 0xF3D598
#   Tooltip_Difficulty.GetManagementDifficultyValueColor 0xF3D6C0
#   Tooltip_Difficulty.GetManagementMoodRows 0xF3BE6C
#   RecruitHelper.DetermineInitialEquipments 0xD5C58C
#   RecruitHelper.ParseItems 0xD60228
# (rva, old_bytes, new_bytes, note)
DYLIB_PATCHES = [
    # Combat + : allow increment from Devastation (4) to Hell (5)
    (0xE62E70, bytes.fromhex("9f0e0071"), bytes.fromhex("9f120071"), "combat max cmp 3->4"),
    # Combat Ironman: treat >=4 as forced tier
    (0xE62E88, bytes.fromhex("00010054"), bytes.fromhex("02010054"), "iron enter old b.eq->b.hs"),
    (0xE62E90, bytes.fromhex("c1000054"), bytes.fromhex("c3000054"), "iron enter new b.ne->b.lo"),
    (0xE62EAC, bytes.fromhex("81000054"), bytes.fromhex("83000054"), "iron stay b.ne->b.lo"),
    (0xE62EC0, bytes.fromhex("60000054"), bytes.fromhex("62000054"), "iron leave b.eq->b.hs"),
    (0xE628A8, bytes.fromhex("e0179f1a"), bytes.fromhex("e0379f1a"), "IsIronModeForced cset hs"),
    # Management + : allow increment from 4 to 5
    (0xE630B0, bytes.fromhex("3f0c0071"), bytes.fromhex("3f100071"), "mgmt max cmp 3->4"),
    # + button locked at Hell (5), not Devastation (4)
    (0xE627DC, bytes.fromhex("5f110071"), bytes.fromhex("5f150071"), "mgmt + lock cmp 4->5"),
    (0xE62804, bytes.fromhex("7f110071"), bytes.fromhex("7f150071"), "combat + lock cmp 4->5"),
    # Env count >4 uses Devastation, not Hard. Literal slots in this build:
    # 0x66CC5C0 CountDownfall, 0x66CC5D0 CountHard.
    (0xF3CD94, bytes.fromhex("08411791"), bytes.fromhex("08011791"), "env fallback CountDownfall"),
    # Mood/stay value 5 uses Downfall case
    (0x842ED4, bytes.fromhex("01020054"), NOP, "treatment mood 5->Downfall"),
    (0x842FBC, bytes.fromhex("01020054"), NOP, "dungeon stay 5->Downfall"),
    # Tooltip grade: stock maps unknown >4 to Hard/Medium. Hell must use SuperHigh
    # (English VeryHigh), same word as Devastation. Current Downfall numbers are
    # already harsher than demo 0.3.26, so the values stay on Downfall.
    (0xF3D634, bytes.fromhex("81010054"), bytes.fromhex("83010054"), "mood grade b.ne->b.lo"),
    (0xF3D6D8, bytes.fromhex("00008052c0035fd6"),
     bytes.fromhex("80008052fbffff17"),
     "mood color >4 uses Downfall"),
    (0xF3BF5C, bytes.fromhex("17008052"), bytes.fromhex("f7008052"),
     "mood row1 color 0->7"),
    (0xF3C054, bytes.fromhex("14008052"), bytes.fromhex("f4008052"),
     "mood row2 color 0->7"),
    # Same unlock gate for Hell as Devastation (VeryHard-clear flag)
    (0xE62E3C, bytes.fromhex("81010054"), bytes.fromhex("8b010054"), "combat lock b.ne->b.lt"),
    (0xE63080, bytes.fromhex("81010054"), bytes.fromhex("8b010054"), "mgmt lock b.ne->b.lt"),
    (0xE627EC, bytes.fromhex("c1000054"), bytes.fromhex("cb000054"), "combat tooltip b.ne->b.lt"),
    (0xE627D4, bytes.fromhex("e8079f1a"), bytes.fromhex("e8a79f1a"), "mgmt tooltip cset lt"),
]

DETERMINE_EQUIP = 0xD5C58C
PARSE_ITEMS = 0xD60228
CAVE_SAVE = 0x604BC00
CAVE_STRIP = 0x604BC40
CAVE_END = 0x604BF00
# Other mods' regions in the same __TEXT zero tail (0x604AC38-0x604BFFF).
FOREIGN_RANGES = [
    (0x604AC40, 0x604AD70, "GCD cave (reserved, off)"),
    (0x604AD70, 0x604AFF0, "weapon slot"),
    (0x604B780, 0x604B978, "multi-bar stubs"),
    (0x604BFF0, 0x604C000, "multi-bar marker"),
]


def stamp() -> str:
    return datetime.now().strftime("%Y-%m-%d_%H%M%S")


def find_aa(explicit: str | None) -> Path:
    if explicit:
        p = Path(explicit).expanduser().resolve()
        if p.name != "aa":
            cand = list(p.rglob("StreamingAssets/aa"))
            if len(cand) == 1:
                p = cand[0]
        if not p.is_dir():
            sys.exit("aa folder not found: %s" % p)
        return p
    if DEFAULT_MAC_AA.is_dir():
        return DEFAULT_MAC_AA
    sys.exit("Pass --aa to the game StreamingAssets/aa folder.")


def find_ga(explicit: str | None) -> Path:
    if explicit:
        p = Path(explicit).expanduser().resolve()
        if not p.is_file():
            sys.exit("GameAssembly not found: %s" % p)
        return p
    if DEFAULT_MAC_GA.is_file():
        return DEFAULT_MAC_GA
    sys.exit("Pass --ga to GameAssembly.dylib")


def find_table_bundle(aa: Path) -> Path:
    hits = sorted(aa.glob("StandaloneOSX/table_assets_all_*.bundle"))
    if len(hits) != 1:
        sys.exit("Need exactly one table_assets_all_*.bundle under %s (got %d)" % (aa, len(hits)))
    return hits[0]


def backup(src: Path, kind: str) -> Path:
    """Copy into backups/ before an in-place write. Never deletes old copies."""
    dest_dir = HERE / "backups"
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / ("%s_%s%s" % (kind, stamp(), src.suffix or ".bin"))
    shutil.copy2(src, dest)
    return dest


def patch_catalog(src: Path, dest: Path, bundle_name: str, old_size: int, new_size: int) -> None:
    data = bytearray(src.read_bytes())
    name_b = bundle_name.encode("ascii")
    if data.count(name_b) != 1:
        sys.exit("catalog.bin names %s %d times (want 1)" % (bundle_name, data.count(name_b)))
    idx = data.find(name_b)
    window = bytes(data[idx : idx + 400])
    packed_old = struct.pack("<I", old_size)
    packed_new = struct.pack("<I", new_size)
    rel = window.find(packed_old)
    if rel < 0:
        rel = window.find(packed_new)
        if rel < 0:
            sys.exit("catalog.bin size %d/%d not near %s" % (old_size, new_size, bundle_name))
        print("catalog size already %d" % new_size)
        size_off = idx + rel
    else:
        size_off = idx + rel
        data[size_off : size_off + 4] = packed_new
        print("catalog bundle size %d -> %d" % (old_size, new_size))
    crc_off = size_off - 4
    old_crc = struct.unpack_from("<I", data, crc_off)[0]
    data[crc_off : crc_off + 4] = struct.pack("<I", 0)
    print("catalog CRC 0x%08x -> 0" % old_crc)
    tmp = dest.with_name(dest.name + ".new")
    tmp.write_bytes(data)
    tmp.replace(dest)


def load_unitypy():
    try:
        import UnityPy
    except ImportError:
        sys.exit("UnityPy missing. /usr/local/bin/python3 -m pip install --user UnityPy==1.25.0")
    UnityPy.config.FALLBACK_UNITY_VERSION = "6000.0.58f2"
    return UnityPy


def get_table(env, name: str):
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        d = obj.read_typetree()
        if d.get("m_Name") == name:
            return obj, d
    raise KeyError(name)


def row_by_key(rows, key: str):
    for r in rows:
        if r.get("Key") == key:
            return r
    return None


def extend_array(row, extra: float) -> None:
    fv = list(row["FloatValues"])
    if len(fv) == 5:
        fv.append(extra)
    elif len(fv) == 6:
        fv[5] = extra
    else:
        sys.exit("%s has %d floats, expected 5 or 6" % (row["Key"], len(fv)))
    row["FloatValues"] = fv


def clone_text(src, new_key: str, texts: dict) -> dict:
    """a copy of the Downfall row src as new_key; texts = {game language code: text} (hell_texts.json)"""
    row = copy.deepcopy(src)
    row["Key"] = new_key
    given = {"Key"}
    for code, col in LANG_COLUMN.items():
        if texts.get(code):
            if col not in row:
                sys.exit("TextKeyTable has no %s column (language %s)" % (col, code))
            row[col] = texts[code]
            given.add(col)
    for lang, val in list(row.items()):
        if lang in given:
            continue
        if val not in ("", "NULL", None) and "<color=#f16071>Devastation</color>" in str(val):
            row[lang] = "<color=#f16071>Hell</color>"
    return row


def edit_tables(env) -> None:
    obj, aff = get_table(env, "AffecterTable")
    down = row_by_key(aff["tableDatas"], "AFFECTER_MonsterDifficultyDownfall")
    if down is None:
        sys.exit("missing AFFECTER_MonsterDifficultyDownfall")
    hell = row_by_key(aff["tableDatas"], "AFFECTER_MonsterDifficulty5")
    if hell is None:
        hell = copy.deepcopy(down)
        hell["Key"] = "AFFECTER_MonsterDifficulty5"
        aff["tableDatas"].append(hell)
        hell = row_by_key(aff["tableDatas"], "AFFECTER_MonsterDifficulty5")
    hell["AdjustStatsData"] = list(MONSTER_HELL)
    obj.save_typetree(aff)

    gobj, gs = get_table(env, "GameSystemTable")
    extras = {
        MGMT_KEYS[0]: FACTOR_166,
        MGMT_KEYS[1]: FACTOR_166,
        MGMT_KEYS[2]: FACTOR_166,
        "GAMESYSTEM_Management_DropRateFactorByDifficulty": 1.0,
        "GAMESYSTEM_Dungeon_UnitRespawnTermByDifficulty": 600.0,
    }
    for key, extra in extras.items():
        row = row_by_key(gs["tableDatas"], key)
        if row is None:
            sys.exit("missing " + key)
        extend_array(row, extra)
    gobj.save_typetree(gs)

    tobj, tk = get_table(env, "TextKeyTable")
    src_name = row_by_key(tk["tableDatas"], "TEXTKEY_CreateCampaign_DownfallDifficulty")
    src_c = row_by_key(tk["tableDatas"], "TEXTKEY_CreateCampaign_DownfallCombatDifficulty_DESC")
    src_m = row_by_key(tk["tableDatas"], "TEXTKEY_CreateCampaign_DownfallManagementDifficulty_DESC")
    if not (src_name and src_c and src_m):
        sys.exit("missing Downfall text keys")
    existing = {r["Key"] for r in tk["tableDatas"]}
    ht = hell_texts()
    new_rows = [
        clone_text(src_name, "TEXTKEY_CreateCampaign_5Difficulty", ht["TEXTKEY_CreateCampaign_5Difficulty"]),
        clone_text(src_c, "TEXTKEY_CreateCampaign_5CombatDifficulty_DESC", ht["TEXTKEY_CreateCampaign_5CombatDifficulty_DESC"]),
        clone_text(src_m, "TEXTKEY_CreateCampaign_5ManagementDifficulty_DESC", ht["TEXTKEY_CreateCampaign_5ManagementDifficulty_DESC"]),
    ]
    for r in new_rows:
        if r["Key"] in existing:
            old = row_by_key(tk["tableDatas"], r["Key"])
            old.clear()
            old.update(r)
        else:
            tk["tableDatas"].append(r)
    tobj.save_typetree(tk)

    sobj, st = get_table(env, "CampaignStartingTable")
    clones = []
    have = {r["Key"] for r in st["tableDatas"]}
    for r in st["tableDatas"]:
        if r.get("ManagementDifficultyType") != 4:
            continue
        n = copy.deepcopy(r)
        n["Key"] = r["Key"].replace("Downfall", "Hell")
        if n["Key"] == r["Key"]:
            n["Key"] = r["Key"] + "Hell"
        n["ManagementDifficultyType"] = 5
        if "ManagementDifficultyTypeName" in n:
            n["ManagementDifficultyTypeName"] = "5"
        if n["Key"] not in have:
            clones.append(n)
    st["tableDatas"].extend(clones)
    sobj.save_typetree(st)
    print("starting clones", len(clones))


def write_patched_bundle(src: Path, dest: Path) -> None:
    UnityPy = load_unitypy()
    env = UnityPy.load(str(src))
    edit_tables(env)
    files = list(env.files.values())
    if len(files) != 1:
        sys.exit("table bundle has %d top-level files, expected 1" % len(files))
    try:
        written = files[0].save(packer="original")
    except Exception as e:  # keep the stock LZ4HC flags unless UnityPy cannot
        print("original packer failed (%s); using lz4" % e)
        written = files[0].save(packer="lz4")
    if not written:
        sys.exit("UnityPy produced no bundle bytes")
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".new")
    tmp.write_bytes(written)
    tmp.replace(dest)
    print("wrote %s (%d bytes)" % (dest, len(written)))


def verify_bundle(path: Path) -> None:
    UnityPy = load_unitypy()
    env = UnityPy.load(str(path))
    _, aff = get_table(env, "AffecterTable")
    _, gs = get_table(env, "GameSystemTable")
    _, tk = get_table(env, "TextKeyTable")
    _, st = get_table(env, "CampaignStartingTable")
    down = row_by_key(aff["tableDatas"], "AFFECTER_MonsterDifficultyDownfall")
    if "MaxHealthTotalPerModifier_50" not in down["AdjustStatsData"]:
        sys.exit("verify failed: Devastation monster HP is not stock +50")
    hell = row_by_key(aff["tableDatas"], "AFFECTER_MonsterDifficulty5")
    if hell is None or hell["AdjustStatsData"] != MONSTER_HELL:
        sys.exit("verify failed: Hell monster stats")
    if row_by_key(aff["tableDatas"], "AFFECTER_PlayerDifficulty5") is not None:
        sys.exit("verify failed: AFFECTER_PlayerDifficulty5 must not exist")
    if row_by_key(aff["tableDatas"], "AFFECTER_PlayerDifficultyDownfall")["AdjustStatsData"] != [
        "CriticalChance_10", "DodgeChance_10", "CriticalDamageBonus_50"
    ]:
        sys.exit("verify failed: Devastation player bonuses were changed")
    for key in MGMT_KEYS:
        arr = row_by_key(gs["tableDatas"], key)["FloatValues"]
        if len(arr) != 6 or abs(arr[5] - FACTOR_166) > 1e-5:
            sys.exit("verify failed: %s %s" % (key, arr))
    craft = row_by_key(gs["tableDatas"], MGMT_KEYS[0])["FloatValues"]
    if abs(craft[4] - 1.0) > 1e-5:
        sys.exit("verify failed: craft array %s" % craft)
    drop = row_by_key(gs["tableDatas"], "GAMESYSTEM_Management_DropRateFactorByDifficulty")["FloatValues"]
    resp = row_by_key(gs["tableDatas"], "GAMESYSTEM_Dungeon_UnitRespawnTermByDifficulty")["FloatValues"]
    if len(drop) != 6 or drop[5] != 1.0 or len(resp) != 6 or resp[5] != 600.0:
        sys.exit("verify failed: drop %s respawn %s" % (drop, resp))
    lab = row_by_key(tk["tableDatas"], "TEXTKEY_CreateCampaign_DownfallDifficulty")["English"]
    if lab != "<color=#f16071>Devastation</color>":
        sys.exit("verify failed: Devastation label is %r" % lab)
    if row_by_key(tk["tableDatas"], "TEXTKEY_CreateCampaign_5Difficulty")["English"] != "<color=#f16071>Hell</color>":
        sys.exit("verify failed: Hell label")
    for k in ("TEXTKEY_CreateCampaign_5CombatDifficulty_DESC",
              "TEXTKEY_CreateCampaign_5ManagementDifficulty_DESC"):
        if row_by_key(tk["tableDatas"], k) is None:
            sys.exit("verify failed: missing " + k)
    n5 = sum(1 for r in st["tableDatas"] if r.get("ManagementDifficultyType") == 5)
    n4 = sum(1 for r in st["tableDatas"] if r.get("ManagementDifficultyType") == 4)
    if n5 != n4 or n5 == 0:
        sys.exit("verify failed: %d Hell starting rows vs %d Devastation" % (n5, n4))
    print("verify OK: Devastation stock, Hell is a sixth step (%d starting rows)" % n5)


def fat_arm64(data: bytes):
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic != FAT_MAGIC:
        raise SystemExit("GameAssembly.dylib is not a fat Mach-O")
    nfat = struct.unpack_from(">I", data, 4)[0]
    off = 8
    for _ in range(nfat):
        cput, _cpsub, o, sz, _al = struct.unpack_from(">iiIII", data, off)
        off += 20
        if cput == CPU_ARM64:
            return o, sz
    raise SystemExit("no arm64 slice")


def check_build(data: bytes, base: int) -> None:
    """Refuse anything that is not the 25846389 arm64 slice."""
    if struct.unpack_from("<I", data, base)[0] != 0xFEEDFACF:
        sys.exit("arm64 slice is not a 64-bit Mach-O")
    ncmds = struct.unpack_from("<I", data, base + 16)[0]
    off = base + 32
    uuid = None
    text_vmsize = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x1B:
            uuid = bytes(data[off + 8:off + 24])
        elif cmd == 0x19 and data[off + 8:off + 24].rstrip(b"\0") == b"__TEXT":
            text_vmsize = struct.unpack_from("<Q", data, off + 32)[0]
        off += size
    if uuid != ARM64_UUID or text_vmsize != TEXT_VMSIZE:
        sys.exit("Unknown GameAssembly build (UUID %s, __TEXT %s). This script is for "
                 "Steam %s only; re-derive the RVAs first."
                 % (uuid.hex() if uuid else None, hex(text_vmsize or 0), BUILD))


def u32(x: int) -> bytes:
    return struct.pack("<I", x & 0xFFFFFFFF)


def b_imm(pc: int, tgt: int) -> bytes:
    imm = (tgt - pc) >> 2
    if not (-2 ** 25 <= imm < 2 ** 25):
        raise SystemExit("B out of range %s -> %s" % (hex(pc), hex(tgt)))
    return u32(0x14000000 | (imm & 0x3FFFFFF))


def bl_imm(pc: int, tgt: int) -> bytes:
    imm = (tgt - pc) >> 2
    if not (-2 ** 25 <= imm < 2 ** 25):
        raise SystemExit("BL out of range %s -> %s" % (hex(pc), hex(tgt)))
    return u32(0x94000000 | (imm & 0x3FFFFFF))


def b_cond(pc: int, tgt: int, cond: int) -> bytes:
    imm19 = (tgt - pc) >> 2
    if not (-2 ** 18 <= imm19 < 2 ** 18):
        raise SystemExit("B.cond out of range")
    return u32(0x54000000 | ((imm19 & 0x7FFFF) << 5) | (cond & 0xF))


def cbz_reg(rt: int, pc: int, tgt: int, is64: bool) -> bytes:
    imm19 = (tgt - pc) >> 2
    op = 0xB4000000 if is64 else 0x34000000
    return u32(op | ((imm19 & 0x7FFFF) << 5) | (rt & 0x1F))


class _Asm:
    def __init__(self, base: int):
        self.base = base
        self.buf = bytearray()
        self.labels: dict[str, int] = {}
        self.fixups: list[tuple] = []

    def pc(self) -> int:
        return self.base + len(self.buf)

    def lab(self, name: str) -> None:
        self.labels[name] = self.pc()

    def w(self, word: int) -> None:
        self.buf.extend(u32(word))

    def b(self, lab: str) -> None:
        self.fixups.append((len(self.buf), lab, "b", 0))
        self.buf.extend(b"\0\0\0\0")

    def bc(self, lab: str, cond: int) -> None:
        self.fixups.append((len(self.buf), lab, "bc", cond))
        self.buf.extend(b"\0\0\0\0")

    def cbzx(self, rt: int, lab: str) -> None:
        self.fixups.append((len(self.buf), lab, "cbzx", rt))
        self.buf.extend(b"\0\0\0\0")

    def cbzw(self, rt: int, lab: str) -> None:
        self.fixups.append((len(self.buf), lab, "cbzw", rt))
        self.buf.extend(b"\0\0\0\0")

    def bl_abs(self, tgt: int) -> None:
        self.buf.extend(bl_imm(self.pc(), tgt))

    def b_abs(self, tgt: int) -> None:
        self.buf.extend(b_imm(self.pc(), tgt))

    def fix(self) -> bytes:
        for off, lab, kind, extra in self.fixups:
            tgt = self.labels[lab]
            pc = self.base + off
            if kind == "b":
                self.buf[off:off + 4] = b_imm(pc, tgt)
            elif kind == "bc":
                self.buf[off:off + 4] = b_cond(pc, tgt, extra)
            elif kind == "cbzx":
                self.buf[off:off + 4] = cbz_reg(extra, pc, tgt, True)
            elif kind == "cbzw":
                self.buf[off:off + 4] = cbz_reg(extra, pc, tgt, False)
        return bytes(self.buf)


def build_strip_cave() -> bytes:
    """ParseItems wrapper: on CombatDifficulty 5 drop entries whose key holds "Staff".

    x22 = RecruitHelper this (saved by CAVE_SAVE). this+0x30 _campaignSetting
    (CampaignSettingContainer), +0x20 CampaignStartSetting, +0x1C _combatDifficulty.
    """
    a = _Asm(CAVE_STRIP)
    ne, ge, gt, lt = 1, 0xA, 0xC, 0xB
    a.w(0xF9403D01)                 # ldr x1, [x8, #0x78]   (stock instr)
    a.w(0xF9401AC9)                 # ldr x9, [x22, #0x30]
    a.cbzx(9, "tail")
    a.w(0xF9401129)                 # ldr x9, [x9, #0x20]
    a.cbzx(9, "tail")
    a.w(0xB9401D29)                 # ldr w9, [x9, #0x1c]
    a.w(0x7100153F)                 # cmp w9, #5
    a.bc("tail", ne)
    a.w(0xA9BA7BFD)
    a.w(0xA90153F3)
    a.w(0xA9025BF5)
    a.w(0xA90363F7)
    a.w(0xA9046BF9)
    a.bl_abs(PARSE_ITEMS)
    a.cbzx(0, "rest")
    a.w(0xAA0003F3)
    a.w(0xB9401A74)
    a.cbzw(20, "rest")
    a.w(0xF9400A75)
    a.cbzx(21, "mov0")
    a.w(0x910082B5)
    a.w(0x2A1F03F6)
    a.w(0x2A1F03F7)
    a.lab("loop")
    a.w(0x6B1402DF)
    a.bc("fin", ge)
    a.w(0x8B3652B8)
    a.w(0xF9400319)
    a.cbzx(25, "keep")
    a.w(0xB940133A)
    a.w(0x7100175F)
    a.bc("keep", lt)
    a.w(0x91005320)
    a.w(0x5100175A)
    a.w(0x2A1F03E1)
    a.lab("sloop")
    a.w(0x6B1A003F)
    a.bc("keep", gt)
    a.w(0x8B214402)
    a.w(0x79400043)
    a.w(0x71014C7F)
    a.bc("np", ne)
    a.w(0x79400443)
    a.w(0x7101D07F)
    a.bc("np", ne)
    a.w(0x79400843)
    a.w(0x7101847F)
    a.bc("np", ne)
    a.w(0x79400C43)
    a.w(0x7101987F)
    a.bc("np", ne)
    a.w(0x79401043)
    a.w(0x7101987F)
    a.bc("np", ne)
    a.b("next")
    a.lab("np")
    a.w(0x11000421)
    a.b("sloop")
    a.lab("keep")
    a.w(0x8B3752A3)
    a.w(0x3DC00300)
    a.w(0x3D800060)
    a.w(0x110006F7)
    a.lab("next")
    a.w(0x110006D6)
    a.b("loop")
    a.lab("fin")
    a.w(0xB9001A77)
    a.w(0xB9401E68)
    a.w(0x11000508)
    a.w(0xB9001E68)
    a.lab("mov0")
    a.w(0xAA1303E0)
    a.lab("rest")
    a.w(0xA9446BF9)
    a.w(0xA94363F7)
    a.w(0xA9425BF5)
    a.w(0xA94153F3)
    a.w(0xA8C67BFD)
    a.w(0xD65F03C0)
    a.lab("tail")
    a.b_abs(PARSE_ITEMS)
    blob = a.fix()
    if len(blob) > (CAVE_END - CAVE_STRIP):
        raise SystemExit("strip cave too large: %d" % len(blob))
    return blob


def build_caves() -> dict[int, bytes]:
    save = (
        u32(0x9100C3FD)             # add x29, sp, #0x30   (stock instr)
        + u32(0xAA0103F3)           # mov x19, x1          (stock instr)
        + u32(0xAA0003F6)           # mov x22, x0          (keep this)
        + u32(0xD65F03C0)
    )
    return {
        CAVE_SAVE: save,
        CAVE_STRIP: build_strip_cave(),
    }


def cave_region() -> bytes:
    region = bytearray(CAVE_END - CAVE_SAVE)
    for va, blob in build_caves().items():
        region[va - CAVE_SAVE:va - CAVE_SAVE + len(blob)] = blob
    return bytes(region)


def staff_strip_sites() -> list[tuple[int, bytes, bytes, str]]:
    return [
        (
            DETERMINE_EQUIP + 0x10,
            bytes.fromhex("fdc30091f30301aa"),
            bl_imm(DETERMINE_EQUIP + 0x10, CAVE_SAVE) + NOP,
            "equip save this",
        ),
        (
            DETERMINE_EQUIP + 0x9C,
            bytes.fromhex("013d40f9") + bl_imm(DETERMINE_EQUIP + 0xA0, PARSE_ITEMS),
            bl_imm(DETERMINE_EQUIP + 0x9C, CAVE_STRIP) + NOP,
            "equip strip staff on Hell",
        ),
    ]


def all_sites() -> list[tuple[int, bytes, bytes, str]]:
    return list(DYLIB_PATCHES) + staff_strip_sites()


def _check_layout() -> None:
    for lo, hi, what in FOREIGN_RANGES:
        if lo < CAVE_END and CAVE_SAVE < hi:
            raise SystemExit("Hell cave overlaps %s" % what)


def _apply_one(data: bytearray, base: int, rva: int, old: bytes, new: bytes, note: str) -> bool:
    off = base + rva
    n = max(len(old), len(new))
    have = bytes(data[off:off + n])
    if have[:len(new)] == new:
        print("already: %s" % note)
        return False
    if have[:len(old)] != old:
        sys.exit("dylib mismatch at %s (%s): have %s want %s" % (
            hex(rva), note, have[:len(old)].hex(), old.hex()))
    data[off:off + len(new)] = new
    print("patched %s" % note)
    return True


def patch_dylib(src: Path, dest: Path) -> None:
    _check_layout()
    data = bytearray(src.read_bytes())
    base, _sz = fat_arm64(bytes(data))
    check_build(bytes(data), base)
    # Verify every site and the cave range BEFORE writing anything.
    for rva, old, new, note in all_sites():
        have = bytes(data[base + rva:base + rva + len(old)])
        if have != old and bytes(data[base + rva:base + rva + len(new)]) != new:
            sys.exit("dylib mismatch at %s (%s): have %s want %s. Nothing written." % (
                hex(rva), note, have.hex(), old.hex()))
    region = cave_region()
    have_cave = bytes(data[base + CAVE_SAVE:base + CAVE_END])
    if have_cave != region and any(have_cave):
        sys.exit("Hell cave range %s-%s is not empty (another mod?). Nothing written."
                 % (hex(CAVE_SAVE), hex(CAVE_END - 1)))
    for rva, old, new, note in DYLIB_PATCHES:
        _apply_one(data, base, rva, old, new, note)
    if have_cave != region:
        data[base + CAVE_SAVE:base + CAVE_END] = region
        print("wrote recruit caves %s-%s" % (hex(CAVE_SAVE), hex(CAVE_END - 1)))
    else:
        print("already: recruit caves")
    for rva, old, new, note in staff_strip_sites():
        _apply_one(data, base, rva, old, new, note)
    tmp = dest.with_name(dest.name + ".hellnew")
    tmp.write_bytes(data)
    os.chmod(tmp, 0o755)
    tmp.replace(dest)
    print("wrote dylib %s" % dest)


def verify_dylib(path: Path) -> None:
    data = path.read_bytes()
    base, _sz = fat_arm64(data)
    check_build(data, base)
    for rva, old, new, note in all_sites():
        have = data[base + rva:base + rva + len(new)]
        if have != new:
            sys.exit("dylib verify fail %s at %s have %s" % (note, hex(rva), have.hex()))
    if data[base + CAVE_SAVE:base + CAVE_END] != cave_region():
        sys.exit("dylib verify fail cave %s" % hex(CAVE_SAVE))
    print("verify OK: GameAssembly Hell clamps, lock, staff strip (Steam %s)" % BUILD)


def _game_running() -> bool:
    import subprocess
    r = subprocess.run(["pgrep", "-f", "DungeonSettlers.app/Contents/MacOS"],
                       capture_output=True, text=True)
    return bool((r.stdout or "").strip())


def _is_live(p: Path) -> bool:
    try:
        return str(p.resolve()).startswith(str(GAME_APP.resolve()))
    except OSError:
        return False


def install_tables(aa: Path, out_dir: Path | None) -> None:
    bundle = find_table_bundle(aa)
    catalog = aa / "catalog.bin"
    old_size = bundle.stat().st_size
    if out_dir is None:
        if _is_live(bundle) and _game_running():
            sys.exit("Close Dungeon Settlers first.")
        backup(bundle, "table_bundle")
        backup(catalog, "catalog")
        bdest, cdest = bundle, catalog
    else:
        out_dir.mkdir(parents=True, exist_ok=True)
        bdest, cdest = out_dir / bundle.name, out_dir / "catalog.bin"
    write_patched_bundle(bundle, bdest)
    verify_bundle(bdest)
    patch_catalog(catalog, cdest, bundle.name, old_size, bdest.stat().st_size)


def install_dylib(ga: Path, out: Path | None, sign: bool) -> None:
    import subprocess
    dest = out or ga
    if out is None:
        if _is_live(ga) and _game_running():
            sys.exit("Close Dungeon Settlers first.")
        backup(ga, "gameassembly")
    patch_dylib(ga, dest)
    if sign:
        subprocess.check_call(["codesign", "-f", "-s", "-", str(dest)])
    verify_dylib(dest)


def main() -> None:
    ap = argparse.ArgumentParser(description="Add Hell after Devastation (Steam %s)." % BUILD)
    ap.add_argument("--aa", help="StreamingAssets/aa folder to read tables from")
    ap.add_argument("--ga", help="input GameAssembly.dylib")
    ap.add_argument("--out", help="write the patched dylib here instead of in place")
    ap.add_argument("--out-dir", help="write patched table bundle + catalog.bin here")
    ap.add_argument("--tables-only", action="store_true")
    ap.add_argument("--dylib-only", action="store_true")
    ap.add_argument("--no-sign", action="store_true")
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--verify", action="store_true", help="check a dylib only")
    args = ap.parse_args()
    if args.verify:
        verify_dylib(find_ga(args.ga))
        return
    if not args.install:
        ap.error("Choose --install or --verify")
    if args.tables_only and args.dylib_only:
        ap.error("Pick one of --tables-only / --dylib-only")
    if not args.dylib_only:
        install_tables(find_aa(args.aa), Path(args.out_dir).expanduser() if args.out_dir else None)
    if not args.tables_only:
        install_dylib(find_ga(args.ga), Path(args.out).expanduser() if args.out else None,
                      sign=not args.no_sign)


if __name__ == "__main__":
    main()
