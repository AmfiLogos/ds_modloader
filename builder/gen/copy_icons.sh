#!/bin/sh
# Copies the icons the Unit Builder screen shows (every icons/... path in data/dsb.json, the portraits of the
# builder's portrait pools, the equipment slot glyphs) from the wiki site into ../ui/bicons/ (same sub paths).
# The wiki repo is only read. Run after gen_tables.js on a data update.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
site=${DS_WIKI_SITE:-$HOME/dungeonsettlersdb/site}
out=$here/../ui/bicons
node -e '
const d=require(process.argv[1]); const s=new Set();
JSON.stringify(d).replace(/"(icons\/[^"]+\.png)"/g,(m,p)=>s.add(p));
for (const r in d.portraits) for (const p of d.portraits[r]) s.add("icons/portraits/"+p+".png");
for (const f of ["EquipmentSlot.png","EquipmentSlot_Icon_Weapon.png","EquipmentSlot_Icon_Shield.png","EquipmentSlot_Icon_Helmet.png",
  "EquipmentSlot_Icon_Armor.png","EquipmentSlot_Icon_Accessory.png","EquipmentSlot_Icon_accessory2.png","EquipmentSlot_Icon_Ammo.png"]) s.add("icons/ui/"+f);
console.log([...s].join("\n"));' "$here/data/dsb.json" | while read -r p; do
  rel=${p#icons/}
  if [ -f "$site/$p" ]; then mkdir -p "$out/$(dirname "$rel")"; cp "$site/$p" "$out/$rel"; else echo "missing: $p" >&2; fi
done
echo "icons in $out: $(find "$out" -name '*.png' | wc -l | tr -d ' ')"
