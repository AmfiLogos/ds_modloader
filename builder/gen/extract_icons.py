#!/usr/bin/env python3
"""Extract the Unit Builder icons from YOUR installed Dungeon Settlers into ui/bicons/.

The icons are game art. They are not in this repo. This script reads them from the game's own Unity files
(read only) and writes the same set the builder screen shows:
  - every icons/... path in builder/data/dsb.json
  - the portraits of the builder's portrait pools (dsb.json "portraits")
  - the 8 equipment slot glyphs (icons/ui/EquipmentSlot*.png)
Each icons/<group>/<name>.png lands at ui/bicons/<group>/<name>.png.

The selection and processing follow the Dungeon Settlers wiki's extractors (extract_db_icons.py,
extract_ui_icons.py, abyss_shader.py), for exactly these groups:
  - per sprite name the largest sprite wins; items take their inventory icon (the one in an icon_*
    Addressables bundle), not the worn-on-body sprites with the same name
  - items are scaled up to 64 px with nearest neighbour (integer factor)
  - units are the art of the unit prefab's "Body" SpriteRenderer (frame name walk as fallback);
    UNIT_Abyss* units are the base unit tinted with the game's Abyss shader
  - skill tree icons are the sprite named after the tree (SKILLTREE_Sword <- "Sword")
  - three stat icons have another name in the game (see ALIAS)

Usage:
  python3 builder/gen/extract_icons.py [--game DIR] [--out DIR]
DIR for --game (or env DS_GAME_DIR) may be the Steam game folder ("Dungeon Settlers"), the .app bundle,
or the Data folder (Mac: DungeonSettlers.app/Contents/Resources/Data, Windows: DungeonSettlers_Data).
Without it, the script looks in the Steam libraries of this computer.
Needs Python 3 with UnityPy and Pillow (pip install UnityPy Pillow).
Exit code 0 only when every icon was written.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

try:
    import UnityPy
    from PIL import Image
except ImportError as e:  # pragma: no cover
    sys.exit(f"extract_icons: {e}. Install with: python3 -m pip install UnityPy Pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DSB = os.path.join(REPO, "builder", "data", "dsb.json")
DEFAULT_OUT = os.path.join(REPO, "ui", "bicons")
MANIFEST = "manifest.txt"   # written last; src-tauri/build.rs checks it

SLOT_GLYPHS = ["EquipmentSlot.png", "EquipmentSlot_Icon_Weapon.png", "EquipmentSlot_Icon_Shield.png",
               "EquipmentSlot_Icon_Helmet.png", "EquipmentSlot_Icon_Armor.png", "EquipmentSlot_Icon_Accessory.png",
               "EquipmentSlot_Icon_accessory2.png", "EquipmentSlot_Icon_Ammo.png"]

# output file name -> the game's sprite name (the wiki saved these under its own names)
ALIAS = {
    "stats/Talent_red": "Talent_Idiot",
    "stats/SkillCooldownIcon": "TooltipIcon_CoolTime",
    "stats/SkillEnergyCostIcon": "TooltipIcon_Energy",
}

# in dsb.json but not shipped by the game build the builder data comes from (25846389): the builder shows
# no picture for it. The wiki's copy came from a July 2026 build.
KNOWN_ABSENT = {"stats/Talent_DontKnow.png"}

NEAREST = getattr(Image, "Resampling", Image).NEAREST


# ---------------------------------------------------------------- finding the game

def steam_roots():
    home = os.path.expanduser("~")
    roots = [os.path.join(home, "Library", "Application Support", "Steam"),      # macOS
             os.path.join(home, ".steam", "steam"), os.path.join(home, ".local", "share", "Steam")]
    for var in ("ProgramFiles(x86)", "ProgramFiles"):
        if os.environ.get(var):
            roots.append(os.path.join(os.environ[var], "Steam"))
    roots.append(r"C:\Program Files (x86)\Steam")
    libs = []
    for r in roots:
        vdf = os.path.join(r, "steamapps", "libraryfolders.vdf")
        if os.path.isfile(vdf):
            try:
                txt = open(vdf, encoding="utf-8", errors="replace").read()
                libs += [p.replace("\\\\", "\\") for p in re.findall(r'"path"\s+"([^"]+)"', txt)]
            except OSError:
                pass
        libs.append(r)
    seen, out = set(), []
    for lib in libs:
        if lib not in seen:
            seen.add(lib)
            out.append(lib)
    return out


def find_data_dir(game):
    """Return the Unity Data folder under game (a Steam game folder, an .app, or the Data folder itself)."""
    def is_data(d):
        return os.path.isfile(os.path.join(d, "data.unity3d")) or \
            os.path.isfile(os.path.join(d, "sharedassets0.assets"))
    if is_data(game):
        return game
    cands = [os.path.join(game, "Contents", "Resources", "Data")]
    cands += glob.glob(os.path.join(game, "*.app", "Contents", "Resources", "Data"))
    cands += glob.glob(os.path.join(game, "*_Data"))
    for c in cands:
        if is_data(c):
            return c
    return None


def resolve_game(arg):
    game = arg or os.environ.get("DS_GAME_DIR")
    if game:
        d = find_data_dir(os.path.expanduser(game))
        if not d:
            sys.exit(f"extract_icons: no Unity data (data.unity3d or sharedassets0.assets) under {game}")
        return d
    for lib in steam_roots():
        d = find_data_dir(os.path.join(lib, "steamapps", "common", "Dungeon Settlers"))
        if d:
            return d
    sys.exit("extract_icons: Dungeon Settlers not found in the Steam libraries. "
             "Pass --game <folder> or set DS_GAME_DIR (the Steam folder 'Dungeon Settlers').")


# ---------------------------------------------------------------- the wanted list (same as the old copy_icons.sh)

def wanted_paths():
    d = json.load(open(DSB, encoding="utf-8"))
    s = []
    seen = set()

    def add(p):
        if p not in seen:
            seen.add(p)
            s.append(p)
    for p in re.findall(r'"(icons/[^"]+\.png)"', json.dumps(d, ensure_ascii=False)):
        add(p)
    for race in d["portraits"]:
        for p in d["portraits"][race]:
            add("icons/portraits/" + p + ".png")
    for f in SLOT_GLYPHS:
        add("icons/ui/" + f)
    return [p[len("icons/"):] for p in s]


# ---------------------------------------------------------------- loading the game files

def serialized_files(path):
    """[(label, SerializedFile)] of a loose .assets file, data.unity3d or an Addressables bundle."""
    env = UnityPy.load(path)
    out = []
    for f in env.files.values():
        if hasattr(f, "objects"):
            out.append((os.path.basename(path), f))
        elif hasattr(f, "files"):
            for name, m in f.files.items():
                if hasattr(m, "objects"):
                    out.append((name, m))
    return out


class Game:
    def __init__(self, data):
        self.data = data
        packed = os.path.join(data, "data.unity3d")
        main = {}
        if os.path.isfile(os.path.join(data, "sharedassets0.assets")):
            for n in ("sharedassets0.assets", "resources.assets"):
                for _, sf in serialized_files(os.path.join(data, n)):
                    main[n] = sf
        else:   # B.0.4.29 and later: the loose files sit inside data.unity3d
            for name, sf in serialized_files(packed):
                if name in ("sharedassets0.assets", "resources.assets"):
                    main[name] = sf
        if set(main) != {"sharedassets0.assets", "resources.assets"}:
            sys.exit(f"extract_icons: sharedassets0.assets / resources.assets not found in {data}")
        self.shared, self.res = main["sharedassets0.assets"], main["resources.assets"]
        aa = os.path.join(data, "StreamingAssets", "aa")
        bundles = sorted(glob.glob(os.path.join(aa, "**", "*.bundle"), recursive=True))
        self.bundles = {}   # bundle base name -> [SerializedFile]
        for b in bundles:
            base = os.path.basename(b)
            if base.startswith(("table_", "audio", "fmod")):
                continue
            self.bundles[b] = [sf for _, sf in serialized_files(b)]
        # the wiki's two sprite sets: DB icons (sharedassets0 + resources + icon_/entity_/sprite_ bundles)
        # and UI icons (sharedassets0 + resources + every bundle but table_/audio/fmod)
        db_b = [b for b in self.bundles if re.match(r"(icon|entity|sprite)_.*_assets_.*\.bundle$", os.path.basename(b))]
        self.db = self._index([self.shared, self.res] + [sf for b in db_b for sf in self.bundles[b]], db_b)
        self.ui = self._index([self.shared, self.res] + [sf for b in self.bundles for sf in self.bundles[b]], [])
        self.unit_files = [self.res] + [sf for b in self.bundles if "entity_unit" in os.path.basename(b)
                                        for sf in self.bundles[b]]
        self.icon_files = {id(sf) for b in self.bundles if os.path.basename(b).startswith("icon_")
                           for sf in self.bundles[b]}

    @staticmethod
    def _index(files, _):
        idx = collections.defaultdict(list)   # lower name -> [(area, name, sf, obj)]
        for sf in files:
            for o in sf.objects.values():
                if o.type.name != "Sprite":
                    continue
                try:
                    d = o.read(check_read=False)
                    r = d.m_Rect
                except Exception:
                    continue
                if d.m_Name:
                    idx[d.m_Name.lower()].append((int(r.width * r.height), d.m_Name, sf, o))
        return idx

    def best(self, name, ui=False, icon_only=False):
        c = (self.ui if ui else self.db).get(name.lower())
        if not c:
            return None
        if icon_only:   # an item's inventory icon, not its worn sprites
            c = [t for t in c if id(t[2]) in self.icon_files] or c
        return max(c, key=lambda t: t[0])   # first of the largest, as the wiki

    def image(self, name, **kw):
        t = self.best(name, **kw)
        if t is None:
            return None
        return t[3].read(check_read=False).image


# ---------------------------------------------------------------- units (port of the wiki's unit logic)

ANIM_ORDER = ["idle", "stand", "standing", "move", "walk", "run", "attack", "cast", "casting", "skill", "hit",
              "damage", "lapse", "dead", "death", "item", "summon", "spawn"]
FADE_IN = {"summon", "spawn"}


def valid_unit_icon(img):
    w, h = img.size
    if max(w, h) < 12:
        return False
    px = list(img.getdata())
    opaque = [(r, g, b) for r, g, b, a in px if a > 25]
    if len(opaque) < max(1, len(px) // 100):
        return False
    if len(set(opaque)) <= 3:
        return False
    return True


def prefab_body_sprites(game):
    out = {}
    for sf in game.unit_files:
        go_name, tr, sr = {}, {}, {}
        for o in sf.objects.values():
            t = o.type.name
            try:
                if t == "GameObject":
                    go_name[o.path_id] = o.read(check_read=False).m_Name
                elif t == "Transform":
                    d = o.read(check_read=False)
                    tr[o.path_id] = (d.m_GameObject.path_id, [c.path_id for c in d.m_Children])
                elif t == "SpriteRenderer":
                    d = o.read(check_read=False)
                    if d.m_Sprite.m_FileID == 0:
                        sr[d.m_GameObject.path_id] = d.m_Sprite.m_PathID
            except Exception:
                continue
        sprites = {o.path_id: o for o in sf.objects.values() if o.type.name == "Sprite"}
        for tid, (gid, kids) in tr.items():
            key = go_name.get(gid)
            if not (key and key.startswith("UNIT_")) or key in out:
                continue
            stack = list(kids)
            while stack:
                cg, ck = tr.get(stack.pop(), (None, ()))
                if go_name.get(cg) == "Body" and cg in sr:
                    sp = sprites.get(sr[cg])
                    if sp is not None:
                        out[key] = sp
                    break
                stack += ck
    return out


def unit_candidates(game, lk):
    out = [lk] if lk in game.db else []
    byanim = collections.defaultdict(list)
    for n in game.db:
        if re.match(re.escape(lk) + r"_[a-z0-9]+_\d+$", n):
            byanim[n[len(lk) + 1:].rsplit("_", 1)[0]].append((int(n.rsplit("_", 1)[1]), n))

    def ak(a):
        return (ANIM_ORDER.index(a) if a in ANIM_ORDER else len(ANIM_ORDER), a)
    for anim in sorted(byanim, key=ak):
        fl = sorted(byanim[anim])
        if anim in FADE_IN:
            fl = fl[::-1]
        out += [n for _, n in fl]
    return out


def unit_art(game, bodies, key):
    body = bodies.get(key)
    if body is not None:
        try:
            img = body.read(check_read=False).image.convert("RGBA")
            if valid_unit_icon(img):
                return img
        except Exception:
            pass
    for name in unit_candidates(game, key.lower()):
        try:
            img = game.image(name).convert("RGBA")
        except Exception:
            continue
        if valid_unit_icon(img):
            return img
    return None


# the game's AbyssBasicMaterial shader (wiki abyss_shader.py): invert, contrast 2.61, HSV (sat .29, val .30),
# overlay-multiply (#5A1095 x glow 8 x ExpBar_Color, blend .584), gamma space
_OCOL = (0.35294119, 0.06347915, 0.58490562)
_OTEX = (34 / 255.0, 189 / 255.0, 120 / 255.0)
_MULT = [_OCOL[i] * 8.0 * _OTEX[i] for i in range(3)]
_LUT = {}


def _abyss_px(r, g, b):
    c = [1 - r / 255.0, 1 - g / 255.0, 1 - b / 255.0]
    c = [(x - 0.5) * 2.61 + 0.5 for x in c]
    R, G, B = c
    c = [0.150687 * R + 0.125031 * G + 0.024282 * B,
         0.063687 * R + 0.212031 * G + 0.024282 * B,
         0.063600 * R + 0.124944 * G + 0.111282 * B]
    c = [c[i] * (1 - 0.584) + (c[i] * _MULT[i]) * 0.584 for i in range(3)]
    return tuple(max(0, min(255, round(x * 255))) for x in c)


def abyss(img):
    im = img.convert("RGBA").copy()
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            k = (r, g, b)
            if k not in _LUT:
                _LUT[k] = _abyss_px(r, g, b)
            px[x, y] = _LUT[k] + (a,)
    return im


# ---------------------------------------------------------------- main

def upscale64(img):
    w, h = img.size
    s = max(1, 64 // max(w, h))
    return img.resize((w * s, h * s), NEAREST) if s > 1 else img


def extract(game, rel, units):
    group, fname = rel.split("/", 1)
    name = fname[:-4]
    alias = ALIAS.get(f"{group}/{name}")
    if group == "items":
        img = game.image(name, icon_only=True)
        return upscale64(img) if img is not None else None
    if group == "units":
        return units(name)
    if group == "skilltree":
        return game.image(name[len("SKILLTREE_"):] if name.startswith("SKILLTREE_") else name)
    if group == "ui":
        return game.image(alias or name, ui=True)
    img = game.image(alias or name)
    if img is None:   # a UI sprite outside the DB set (uiprefab bundle)
        img = game.image(alias or name, ui=True)
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", help="game folder (default: env DS_GAME_DIR, then the Steam libraries)")
    ap.add_argument("--out", default=DEFAULT_OUT, help="output folder (default: ui/bicons)")
    a = ap.parse_args()
    data = resolve_game(a.game)
    print(f"game data: {data}")
    want = wanted_paths()
    game = Game(data)
    bodies = prefab_body_sprites(game)
    cache = {}

    def units(key):
        if key not in cache:
            if key.startswith("UNIT_Abyss"):
                base = units("UNIT_" + key[len("UNIT_Abyss"):])
                cache[key] = abyss(base) if base is not None else None
            else:
                cache[key] = unit_art(game, bodies, key)
        return cache[key]

    os.makedirs(a.out, exist_ok=True)
    missing = []
    for rel in want:
        try:
            img = extract(game, rel, units)
        except Exception as e:
            print(f"error: {rel}: {e}", file=sys.stderr)
            img = None
        if img is None:
            missing.append(rel)
            continue
        p = os.path.join(a.out, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        img.save(p)
    stamp = os.path.join(a.out, MANIFEST)
    for m in missing:
        print(f"{'not in this game build (known)' if m in KNOWN_ABSENT else 'missing'}: {m}", file=sys.stderr)
    bad = [m for m in missing if m not in KNOWN_ABSENT]
    if bad:
        if os.path.exists(stamp):
            os.remove(stamp)
        sys.exit(f"extract_icons: {len(bad)} of {len(want)} icons not found in this game build")
    with open(os.path.join(a.out, "NOTICE.md"), "w", encoding="utf-8") as f:
        f.write("# Origin of the files in this folder\n\n"
                "These icons are game art from Dungeon Settlers. They are not our own work and they are not under\n"
                "this repo's MIT license. The rights belong to the game's developer and publisher. They are not in\n"
                "git: `builder/gen/extract_icons.py` extracts them from your own installed game.\n")
    with open(stamp, "w", encoding="utf-8") as f:
        f.write("\n".join(m for m in want if m not in missing) + "\n")
    print(f"icons in {a.out}: {len(want) - len(missing)} of {len(want)}")


if __name__ == "__main__":
    main()
