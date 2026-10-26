#!/bin/zsh
# make_instance.sh A|B  - build an isolated copy of Dungeon Settlers for the test rig.
# Copy = APFS clone of the installed app; stock GameAssembly (build 25846389) + Steam gate patch;
# own bundle id (prefs separate); LSBackgroundOnly. The real install is only READ.
set -e
X=$1; [[ $X == [K-Z] || $X == [A-Z][A-Z]* ]] || { echo "usage: $0 K..Z | a name of 2+ capitals (FSC)" >&2; exit 1; }   # A..J: the multiplayer rig's
REAL="$HOME/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app"
I=~/ds_launcher/loader/test_env/instances/$X; APP=$I/DungeonSettlers.app; FW=$APP/Contents/Frameworks
mkdir -p $I
if [[ ! -d $APP ]]; then cp -c -R "$REAL" $APP; fi
# stock GameAssembly (new inode via mv, never write over a mapped file)
codesign -v "$REAL/Contents/Frameworks/GameAssembly.dylib.stock_25846389"   # Steam-signed stock file, read only
cp -c "$REAL/Contents/Frameworks/GameAssembly.dylib.stock_25846389" "$FW/GameAssembly.dylib.rig_tmp"
python3 - "$FW/GameAssembly.dylib.rig_tmp" <<'PY'
import struct,sys
p=sys.argv[1]; b=bytearray(open(p,'rb').read())
n=struct.unpack('>I',b[4:8])[0]
for i in range(n):
    ct,cs,off,size,al=struct.unpack('>iiIII',b[8+20*i:28+20*i])
    if ct==0x0100000C: base=off
va=0xD25730  # StoreRuntimeHandler.SteamRuntime.get_IsEnabled: mov w0,#1 -> mov w0,#0
assert b[base+va:base+va+8].hex() in ('20008052c0035fd6','00008052c0035fd6'), b[base+va:base+va+8].hex()
b[base+va:base+va+4]=bytes.fromhex('00008052')
open(p,'wb').write(b); print('steam gate patched at slice base %#x'%base)
PY
codesign -f -s - "$FW/GameAssembly.dylib.rig_tmp" 2>/dev/null
mv -f "$FW/GameAssembly.dylib.rig_tmp" "$FW/GameAssembly.dylib"
python3 - "$FW/GameAssembly.dylib" <<'PY'
import struct,sys
b=open(sys.argv[1],'rb').read(); n=struct.unpack('>I',b[4:8])[0]
for i in range(n):
    ct,cs,off,size,al=struct.unpack('>iiIII',b[8+20*i:28+20*i])
    if ct==0x0100000C: base=off
print('verify after sign:', b[base+0xD25730:base+0xD25738].hex())
PY
# The live install carries the weapon-slot mod's asset changes (catalog.bin + uiprefab bundle). With the
# STOCK GameAssembly they break campaign loading (QuickSlotUI.Init KeyNotFoundException '4'), so put the
# stock assets back, but only after checking them against Steam's own seal (real app's CodeResources).
python3 - "$APP" <<'PY'
import plistlib, hashlib, os, shutil, sys
app = sys.argv[1]
real = os.path.expanduser('~/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app/Contents/_CodeSignature/CodeResources')
seal = plistlib.load(open(real, 'rb'))['files2']
stock = os.path.expanduser('~/ds_weaponslot/b25846389/assets/work/stock_aa')
sha = lambda p: hashlib.sha256(open(p, 'rb').read()).digest()
for rel, v in seal.items():
    if not isinstance(v, dict) or 'hash2' not in v: continue
    p = os.path.join(app, 'Contents', rel)
    if os.path.exists(p) and sha(p) == v['hash2']: continue
    cand = os.path.join(stock, os.path.basename(rel))
    if not os.path.exists(cand) or sha(cand) != v['hash2']:
        sys.exit(f'no verified stock copy for {rel}')
    shutil.copyfile(cand, p + '.rigtmp'); os.replace(p + '.rigtmp', p)
    print('restored stock', rel)
print('all sealed resources match Steam')
PY
# Only GameAssembly + UnityPlayer are needed in Frameworks (the owner's backups/mod dylibs are moved aside;
# they are not signed and block codesign). steam_appid.txt in MacOS also blocks codesign.
mkdir -p $I/fw_unused
for f in $FW/*(N); do b=${f:t}; [[ $b == GameAssembly.dylib || $b == UnityPlayer.dylib ]] || mv "$f" $I/fw_unused/; done
[[ -f "$APP/Contents/MacOS/steam_appid.txt" ]] && mv "$APP/Contents/MacOS/steam_appid.txt" $I/steam_appid.txt.moved
PL=$APP/Contents/Info.plist
plutil -replace CFBundleIdentifier -string "com.CanOpener.Dungeon-Settlers.mp$X" $PL
plutil -replace CFBundleName -string "DS rig $X" $PL
plutil -replace LSBackgroundOnly -bool YES $PL
plutil -replace LSUIElement -bool YES $PL
# Info.plist changed -> re-seal ad-hoc (no hardened runtime: DYLD_INSERT_LIBRARIES allowed, no library validation)
codesign -f -s - "$APP/Contents/MacOS/Dungeon Settlers" 2>&1 | tail -1
mkdir -p $I/home $I/data/Saves
# never a copy that can take focus or show in the Dock (lead 2026-10-15): check after signing
[[ $(plutil -extract LSBackgroundOnly raw $PL) == true && $(plutil -extract LSUIElement raw $PL) == true ]] || { echo "copy $X lacks the hidden-app flags" >&2; exit 1; }
echo "instance $X ready: $APP"
