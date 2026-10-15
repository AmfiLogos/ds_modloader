#!/bin/zsh
# Build the mod/rig dylib (arm64 only). Output: ../build/dsrig.dylib (ad-hoc signed).
# rig + determinism (rig_*), multiplayer glue (mp_*), dsnet (net/, net/steam/), orders (ord_*), UI (ui_*).
# ord_test/ord_smoke.c only adds the ord_inject / ord_select / ord_hover / ord_stats test commands.
# Multiplayer menu (mpmenu*).
# Transitions (trn_*): floor seeds, flow barrier, host-only autosave.
# Mac math swap (windows lane, win/NOTES.md section 7): off unless DSMP_MATH=1 or <DSRIG_DIR>/dsmp_math.on.
# Needs win/math/build/libdsmath_arm64.a (run win/math/build_math.sh once).
set -e; cd "${0:A:h}"; mkdir -p ../build
nice -n 19 clang -arch arm64 -mmacosx-version-min=12.0 -O2 -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/dsrig.dylib -o ../build/dsrig.dylib.tmp \
  rig_main.c rig_hook.c rig_sock.c rig_cmd.c rig_win.c rig_det.c \
  mp_core.c mp_config.c mp_game.c \
  net/dsnet_link.c net/dsnet_server.c net/dsnet_client.c net/steam/dsnet_steam.c \
  ord_core.c ord_game.c ord_mp.c ord_test/ord_smoke.c \
  ui_game.c ui_model.c ui_teams.c ui_teams_model.c mpmenu.c mpmenu_model.c \
  trn_logic.c trn_game.c trn_mp.c \
  ../win/math/math_mac.c -I../win/math ../win/math/build/libdsmath_arm64.a
codesign -f -s - ../build/dsrig.dylib.tmp 2>/dev/null
mv -f ../build/dsrig.dylib.tmp ../build/dsrig.dylib     # new inode: never overwrite a mapped dylib
echo built ../build/dsrig.dylib
