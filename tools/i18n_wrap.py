#!/usr/bin/env python3
"""i18n_wrap.py - the mechanical part of the loader's translation work: player text literals -> TR() / TRN().

Each entry is (file, old text, new text). It applies an entry when the old text is found exactly once and the new
text is not there yet, so it can run again on a newer main: entries already applied are skipped, entries whose old
text moved are listed as MISSING (fix those by hand). Structural changes (step ids, status codes, fonts, the language
poll) are hand edits in the same branch, not in this list.

    python3 tools/i18n_wrap.py [--check]      (--check: report only, change nothing)
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

E = []


def W(path, old, new):
    E.append((path, old, new))


# ---------------------------------------------------------------- loader/src/dsl_main.c
F = "loader/src/dsl_main.c"
W(F, 'dsl_os_notice("Dungeon Settlers: mods are off", status_why);\n}\n/* the launcher',
  'dsl_os_notice(TR("Dungeon Settlers: mods are off"), status_why);\n}\n/* the launcher')
W(F, '    dsl_os_notice("Dungeon Settlers: mods are off", status_why);\n}\n\n/* GameAssembly mapped',
  '    dsl_os_notice(TR("Dungeon Settlers: mods are off"), status_why);\n}\n\n/* GameAssembly mapped')
W(F, 'snprintf(status_why, sizeof status_why, "The game files changed after the start check. Mods stay off. Restart the game.");',
  'snprintf(status_why, sizeof status_why, "%s", TR("The game files changed after the start check. Mods stay off. Restart the game."));')
W(F, 'snprintf(status_why, sizeof status_why, "The game was updated. Mods stay off until they are updated for this game version.");',
  'snprintf(status_why, sizeof status_why, "%s", TR("The game was updated. Mods stay off until they are updated for this game version."));')
W(F, 'snprintf(status_why, sizeof status_why, "This game is build %s, but the launcher started this world for build %s. "\n'
     '                 "Mods stay off. Start the world from the launcher again.", dsl_build_id, pin);',
  'snprintf(status_why, sizeof status_why, TR("This game is build %s, but the launcher started this world for build %s. "\n'
  '                 "Mods stay off. Start the world from the launcher again."), dsl_build_id, pin);')
W(F, 'if (why) *why = !p ? "not loaded: not made for this game version or this computer" : p->state == 2 ? "it crashed while starting"\n'
     '                  : p->state == 3 ? "not loaded: it needs another mod, clashes with one, or crashed last time" : "";',
  'if (why) *why = !p ? TR_NOOP("not loaded: not made for this game version or this computer") : p->state == 2 ? TR_NOOP("it crashed while starting")\n'
  '                  : p->state == 3 ? TR_NOOP("not loaded: it needs another mod, clashes with one, or crashed last time") : "";   /* keys: the Mods window translates */')
W(F, 'snprintf(why, cap, "BepInEx mods and MelonLoader mods cannot run together. Turn off the mods of one of them.");',
  'snprintf(why, cap, "%s", TR("BepInEx mods and MelonLoader mods cannot run together. Turn off the mods of one of them."));')

# ---------------------------------------------------------------- loader/ui/dsl_win.c (the Mods window)
F = "loader/ui/dsl_win.c"
for a in ["Go back to the title screen to change mods.", "Restarting with the new mods...",
          "Switch mods on or off, then Save. The world loads with these mods.",
          "Switch mods on or off, then Apply. The game restarts in the background and brings you back here."]:
    W(F, f'snprintf(s, sizeof s, "{a}");', f'snprintf(s, sizeof s, "%s", TR("{a}"));')
W(F, 'snprintf(s, sizeof s, "Mods cannot change now: %s", busy);', 'snprintf(s, sizeof s, TR("Mods cannot change now: %s"), busy);')
W(F, 'snprintf(m_note, sizeof m_note, "The group could not be deleted.");', 'snprintf(m_note, sizeof m_note, "%s", TR("The group could not be deleted."));')
W(F, 'snprintf(q, sizeof q, "Delete the group \\"%s\\"? The mods stay installed.", del_name);\n    U->notice(q, "Delete", "Cancel", del_ok, NULL, NULL);',
  'snprintf(q, sizeof q, TR("Delete the group \\"%s\\"? The mods stay installed."), del_name); dsi_utf8_fix_end(q);\n    U->notice(q, TR("Delete"), TR("Cancel"), del_ok, NULL, NULL);')
W(F, 'n ? tip : "No mods: the game without mods.");', 'n ? tip : TR("No mods: the game without mods."));')
W(F, 'U->tooltip(g_del[g], NULL, "Delete this group");', 'U->tooltip(g_del[g], NULL, TR("Delete this group"));')
W(F, 'snprintf(m_note, sizeof m_note, "The group could not be saved.");', 'snprintf(m_note, sizeof m_note, "%s", TR("The group could not be saved."));')
W(F, 'snprintf(m_note, sizeof m_note, "The mods for this world could not be saved.");', 'snprintf(m_note, sizeof m_note, "%s", TR("The mods for this world could not be saved."));')
W(F, 'snprintf(m_note, sizeof m_note, "Saved. %s loads with these mods.", world_name[0] ? world_name : "This world");',
  'snprintf(m_note, sizeof m_note, world_name[0] ? TR("Saved. %s loads with these mods.") : TR("Saved. This world loads with these mods."), world_name); dsi_utf8_fix_end(m_note);')
W(F, 'snprintf(m_note, sizeof m_note, "The mods did not change: %s", why[0] ? why : "the game did not allow the change");',
  'snprintf(m_note, sizeof m_note, TR("The mods did not change: %s"), why[0] ? why : TR("the game did not allow the change")); dsi_utf8_fix_end(m_note);')
W(F, 'else if (n > 0) snprintf(m_note, sizeof m_note, n == 1 ? "Found 1 new mod. Turn it on, then click Apply." : "Found %d new mods. Turn them on, then click Apply.", n);',
  'else if (n > 0) snprintf(m_note, sizeof m_note, TRN("Found %d new mod. Turn it on, then click Apply.", "Found %d new mods. Turn them on, then click Apply.", n), n);')
W(F, 'snprintf(m_note, sizeof m_note, "The mods folder has no new mods.");', 'snprintf(m_note, sizeof m_note, "%s", TR("The mods folder has no new mods."));')
W(F, 'char wn[160]; snprintf(wn, sizeof wn, "%s", world_name[0] ? world_name : "this world");',
  'char wn[160]; snprintf(wn, sizeof wn, "%s", world_name[0] ? world_name : TR("this world"));')
W(F, 'char hd[200]; snprintf(hd, sizeof hd, world_mode ? "Mods for %s" : "Installed mods", wn);',
  'char hd[200]; if (world_mode) snprintf(hd, sizeof hd, TR("Mods for %s"), wn); else snprintf(hd, sizeof hd, "%s", TR("Installed mods")); dsi_utf8_fix_end(hd);')
W(F, 'DSLObj of = named(U->button(root, m_lw - 200, 0, 200, 34, "Open mods folder", folder_click, NULL), 0, "mods-folder", folder_click, NULL, "Open mods folder");\n'
     '        U->tooltip(of, "Open mods folder", "Put the mod\'s .zip file in this folder. Click Refresh to add it to this list. Then turn it on and click Apply.");\n'
     '        DSLObj rf = named(U->button(root, m_lw - 316, 0, 108, 34, "Refresh", refresh_click, NULL), 0, "mods-refresh", refresh_click, NULL, "Refresh");\n'
     '        U->tooltip(rf, "Refresh", "Check the mods folder for new mods.");',
  'DSLObj of = named(U->button(root, m_lw - 200, 0, 200, 34, TR("Open mods folder"), folder_click, NULL), 0, "mods-folder", folder_click, NULL, TR("Open mods folder"));\n'
  '        U->tooltip(of, TR("Open mods folder"), TR("Put the mod\'s .zip file in this folder. Click Refresh to add it to this list. Then turn it on and click Apply."));\n'
  '        DSLObj rf = named(U->button(root, m_lw - 316, 0, 108, 34, TR("Refresh"), refresh_click, NULL), 0, "mods-refresh", refresh_click, NULL, TR("Refresh"));\n'
  '        U->tooltip(rf, TR("Refresh"), TR("Check the mods folder for new mods."));')
W(F, 'U->text(list, 12, 8, m_lw - 56, 40, "No mods are installed.", 16,', 'U->text(list, 12, 8, m_lw - 56, 40, TR("No mods are installed."), 16,')
W(F, 'U->text(root, m_rx, 0, m_rw, 32, "Groups", 20,', 'U->text(root, m_rx, 0, m_rw, 32, TR("Groups"), 20,')
W(F, 'm_save = named(U->button(root, m_rx, 40, m_rw, 40, "Save these mods as a group", save_click, NULL), 0, "group-new", save_click, NULL, "Save these mods as a group");',
  'm_save = named(U->button(root, m_rx, 40, m_rw, 40, TR("Save these mods as a group"), save_click, NULL), 0, "group-new", save_click, NULL, TR("Save these mods as a group"));')
W(F, 'm_field = named(U->field(root, m_rx, 40, m_rw - 184, 40, "Group name", "",', 'm_field = named(U->field(root, m_rx, 40, m_rw - 184, 40, TR("Group name"), "",')
W(F, 'm_fsave = named(U->button(root, m_rx + m_rw - 176, 40, 84, 40, "Save", fsave_click, NULL), 0, "group-save", fsave_click, NULL, "Save");',
  'm_fsave = named(U->button(root, m_rx + m_rw - 176, 40, 84, 40, TR("Save"), fsave_click, NULL), 0, "group-save", fsave_click, NULL, TR("Save"));')
W(F, 'm_fcancel = named(U->button(root, m_rx + m_rw - 84, 40, 84, 40, "Cancel", fcancel_click, NULL), 0, "group-cancel", fcancel_click, NULL, "Cancel");',
  'm_fcancel = named(U->button(root, m_rx + m_rw - 84, 40, 84, 40, TR("Cancel"), fcancel_click, NULL), 0, "group-cancel", fcancel_click, NULL, TR("Cancel"));')
W(F, '"No groups yet. Set the switches, then save them as a group.", 15,', 'TR("No groups yet. Set the switches, then save them as a group."), 15,')
W(F, 'm_undo = named(U->button(root, m_rx, h - 52, 134, 44, "Undo changes", undo_click, NULL), 0, "undo", undo_click, NULL, "Undo changes");',
  'm_undo = named(U->button(root, m_rx, h - 52, 134, 44, TR("Undo changes"), undo_click, NULL), 0, "undo", undo_click, NULL, TR("Undo changes"));')
W(F, 'const char *al = world_mode ? "Save for this world" : "Apply";', 'const char *al = world_mode ? TR("Save for this world") : TR("Apply");')
W(F, 'U->tooltip(m_apply, al, "This world loads with these mods. The mods running now do not change.");',
  'U->tooltip(m_apply, al, TR("This world loads with these mods. The mods running now do not change."));')
W(F, 'else U->tooltip(m_apply, "Apply", "The game restarts in the background with these mods, behind a loading screen, and brings you back to the same place.");',
  'else U->tooltip(m_apply, al, TR("The game restarts in the background with these mods, behind a loading screen, and brings you back to the same place."));')
W(F, 'U->tooltip(x, NULL, "Close (Esc)");', 'U->tooltip(x, NULL, TR("Close (Esc)"));')
W(F, 'const char *title = t == 0 ? "Mods" : tab_at(t)->title,', 'const char *title = t == 0 ? TR("Mods") : tab_at(t)->title,')

# ---------------------------------------------------------------- loader/src/dsl_reg.c
F = "loader/src/dsl_reg.c"
W(F, 'st = runs ? 1 : 0; why = runs ? "" : !on ? "off" : (br && *br) ? br : "BepInEx did not start";',
  'st = runs ? 1 : 0; why = runs ? "" : !on ? "off" : (br && *br) ? br : TR_NOOP("BepInEx did not start");   /* reasons: English keys, the Mods window translates */')
W(F, 'why = dsl_bie_state() < 0 ? "BepInEx is not available in this start" : "BepInEx plugin: not running in this start";',
  'why = dsl_bie_state() < 0 ? TR_NOOP("BepInEx is not available in this start") : TR_NOOP("BepInEx plugin: not running in this start");')
W(F, 'why = mr && *mr ? mr : dsl_ml_state() < 0 ? "MelonLoader is not available in this start" : "MelonLoader mod: not running in this start";',
  'why = mr && *mr ? mr : dsl_ml_state() < 0 ? TR_NOOP("MelonLoader is not available in this start") : TR_NOOP("MelonLoader mod: not running in this start");')
W(F, 'x->reason = xstrdup(st == 1 ? "" : !on ? "off" : (why ? why : "did not load"));',
  'x->reason = xstrdup(st == 1 ? "" : !on ? "off" : (why ? why : TR_NOOP("did not load")));')
W(F, 'snprintf(out, cap, "Mods: %s", nm);\n    return k != NULL;', 'snprintf(out, cap, TR("Mods: %s"), nm); dsi_utf8_fix_end(out);\n    return k != NULL;')
W(F, 'snprintf(out, cap, "Mods for this world: %s", nm); }', 'snprintf(out, cap, TR("Mods for this world: %s"), nm); dsi_utf8_fix_end(out); }')
W(F, 'if (dsl_soft_busy()) return "the game is restarting with new mods";', 'if (dsl_soft_busy()) return TR("the game is restarting with new mods");')
W(F, '#define TITLE_ONLY "Go back to the title screen to change mods."', '#define TITLE_ONLY TR("Go back to the title screen to change mods.")')
W(F, 'snprintf(why, cap, TITLE_ONLY);', 'snprintf(why, cap, "%s", TITLE_ONLY);')
W(F, 'snprintf(why, cap, "the mod settings could not be saved");', 'snprintf(why, cap, "%s", TR("the mod settings could not be saved"));')
W(F, 'snprintf(why, cap, "%s mods need the Dungeon Settlers Modded app", hn);', 'snprintf(why, cap, TR("%s mods need the Dungeon Settlers Modded app"), hn);')
W(F, 'snprintf(why, cap, "the %s download could not start", hn);', 'snprintf(why, cap, TR("the %s download could not start"), hn);')
W(F, 'snprintf(t, sizeof t, "Preparing mods (first start)\\nDownloading %s (%s). The game restarts with the new mods when it is ready.", hn, dsl_net_size(nk));\n'
     '        if (u) u->notice(t, "OK", NULL, NULL, NULL, NULL);',
  'snprintf(t, sizeof t, TR("Preparing mods (first start)\\nDownloading %s (%s). The game restarts with the new mods when it is ready."), hn, dsl_net_size(nk)); dsi_utf8_fix_end(t);\n'
  '        if (u) u->notice(t, TR("OK"), NULL, NULL, NULL, NULL);')
W(F, 'snprintf(why, cap, "the game cannot restart right now (loading, or a restart already runs)");', 'snprintf(why, cap, "%s", TR("the game cannot restart right now (loading, or a restart already runs)"));')
W(F, 'char t[500]; snprintf(t, sizeof t, "The world cannot be loaded now: %s", why);\n        if (u) u->notice(t, "OK", NULL, NULL, NULL, NULL);',
  'char t[500]; snprintf(t, sizeof t, TR("The world cannot be loaded now: %s"), why); dsi_utf8_fix_end(t);\n        if (u) u->notice(t, TR("OK"), NULL, NULL, NULL, NULL);')
W(F, 'if (missing) { snprintf(t, sizeof t, "This world uses: %s.\\nInstall the missing mods to load it.", nm); u->notice(t, "OK", NULL, NULL, NULL, NULL); return; }',
  'if (missing) { snprintf(t, sizeof t, TR("This world uses: %s.\\nInstall the missing mods to load it."), nm); dsi_utf8_fix_end(t); u->notice(t, TR("OK"), NULL, NULL, NULL, NULL); return; }')
W(F, 'if (busy) { snprintf(t, sizeof t, "This world uses: %s.\\nIt cannot be loaded now: %s", nm, busy); u->notice(t, "OK", NULL, NULL, NULL, NULL); return; }',
  'if (busy) { snprintf(t, sizeof t, TR("This world uses: %s.\\nIt cannot be loaded now: %s"), nm, busy); dsi_utf8_fix_end(t); u->notice(t, TR("OK"), NULL, NULL, NULL, NULL); return; }')
W(F, 'snprintf(t, sizeof t, "This world uses: %s.\\nLoad it with them?", nm);\n    u->notice(t, "Load", "Cancel", k_ok, k_cancel, NULL);',
  'snprintf(t, sizeof t, TR("This world uses: %s.\\nLoad it with them?"), nm); dsi_utf8_fix_end(t);\n    u->notice(t, TR("Load"), TR("Cancel"), k_ok, k_cancel, NULL);')
W(F, 'const DSLUi *u = dsl_ui_kit(); char t[500]; snprintf(t, sizeof t, "The mods could not be applied: %s", w2);\n                if (u) u->notice(t, "OK", NULL, NULL, NULL, NULL);',
  'const DSLUi *u = dsl_ui_kit(); char t[500]; snprintf(t, sizeof t, TR("The mods could not be applied: %s"), w2); dsi_utf8_fix_end(t);\n                if (u) u->notice(t, TR("OK"), NULL, NULL, NULL, NULL);')
W(F, 'snprintf(t, sizeof t, "%s could not be installed: %s\\nThe mods stay as they were.", dsl_net_name(bp_kind), why);\n            if (u) u->notice(t, "OK", NULL, NULL, NULL, NULL);',
  'snprintf(t, sizeof t, TR("%s could not be installed: %s\\nThe mods stay as they were."), dsl_net_name(bp_kind), why); dsi_utf8_fix_end(t);\n            if (u) u->notice(t, TR("OK"), NULL, NULL, NULL, NULL);')
W(F, 'char t[300]; snprintf(t, sizeof t, "Preparing mods (first start): %s prepares its files for this game version. This takes about 2 minutes, once.", dsl_net_name(tip_first_run));',
  'char t[300]; snprintf(t, sizeof t, TR("Preparing mods (first start): %s prepares its files for this game version. This takes about 2 minutes, once."), dsl_net_name(tip_first_run)); dsi_utf8_fix_end(t);')

# ---------------------------------------------------------------- loader/src/dsl_rows.c (Load list, New Campaign)
F = "loader/src/dsl_rows.c"
W(F, 'if (!n) { snprintf(line, lcap, "<color=#A69085>Mods:</color> none"); snprintf(all, acap, "No mods."); return; }',
  'if (!n) { snprintf(line, lcap, "<color=#A69085>%s</color> %s", TR("Mods:"), TR("none")); snprintf(all, acap, "%s", TR("No mods.")); return; }')
W(F, 'char one[300]; snprintf(one, sizeof one, "%s%s", nm ? nm : ids[i], nm ? "" : " (not installed)");',
  'char one[300]; if (nm) snprintf(one, sizeof one, "%s", nm); else snprintf(one, sizeof one, TR("%s (not installed)"), ids[i]);')
W(F, 'snprintf(line, lcap, "<color=#A69085>Mods:</color> %s", shown);', 'snprintf(line, lcap, "<color=#A69085>%s</color> %s", TR("Mods:"), shown); dsi_utf8_fix_end(line);')
W(F, 'u->tooltip(w->line, "Mods of this world", all);', 'dsi_utf8_fix_end(all); u->tooltip(w->line, TR("Mods of this world"), all);')
W(F, 'if (!w->chg) { w->chg = u->button(tr, 0, 0, 10, 10, "Change", change_click, (void *)(intptr_t)i);\n'
     '                       u->tooltip(w->chg, "Change", "Choose the mods this world loads with."); }',
  'if (!w->chg) { w->chg = u->button(tr, 0, 0, 10, 10, TR("Change"), change_click, (void *)(intptr_t)i);\n'
  '                       u->tooltip(w->chg, TR("Change"), TR("Choose the mods this world loads with.")); }')
W(F, 'nc_btn = u->button(tr, 0, 0, 10, 10, "Change", nc_click, NULL); }', 'nc_btn = u->button(tr, 0, 0, 10, 10, TR("Change"), nc_click, NULL); }')

# ---------------------------------------------------------------- loader/src/dsl_world.c (refused world loads)
F = "loader/src/dsl_world.c"
W(F, '#define REFUSE_TEXT "This world uses other mods."', '#define REFUSE_TEXT TR("This world uses other mods.")')
W(F, '#define REFUSE_IN_WORLD "This world uses other mods. Go back to the title screen to load it."',
  '#define REFUSE_IN_WORLD TR("This world uses other mods. Go back to the title screen to load it.")')
W(F, 'char msg[1600]; snprintf(msg, sizeof msg, "The list of mods for each world could not be read, so no world is loaded now. The file is kept as %s. Restart the game, and if this message comes back, send that file with a bug report.", kept);',
  'char msg[1600]; snprintf(msg, sizeof msg, TR("The list of mods for each world could not be read, so no world is loaded now. The file is kept as %s. Restart the game, and if this message comes back, send that file with a bug report."), kept); dsi_utf8_fix_end(msg);')

# ---------------------------------------------------------------- loader/ui/dsl_pkg.c (mods folder refresh)
F = "loader/ui/dsl_pkg.c"
W(F, 'if (bad == 1) snprintf(fail, fcap, "%s is not a mod for Dungeon Settlers Modded. It was moved to the folder mods/.failed.", first_bad);\n'
     '        else snprintf(fail, fcap, "%d files are not mods for Dungeon Settlers Modded. They were moved to the folder mods/.failed.", bad);',
  'if (bad == 1) snprintf(fail, fcap, TR("%s is not a mod for Dungeon Settlers Modded. It was moved to the folder mods/.failed."), first_bad);\n'
  '        else snprintf(fail, fcap, TRN("%d file is not a mod for Dungeon Settlers Modded. It was moved to the folder mods/.failed.", "%d files are not mods for Dungeon Settlers Modded. They were moved to the folder mods/.failed.", bad), bad);\n'
  '        dsi_utf8_fix_end(fail);')

# ---------------------------------------------------------------- loader/start (the OS parts of the loader)
F = "loader/start/mac/dsl_start_mac.c"
W(F, 'if (t && m) CFUserNotificationDisplayNotice(0, kCFUserNotificationCautionAlertLevel, NULL, NULL, NULL, t, m, CFSTR("OK"));',
  'CFStringRef ok = CFStringCreateWithCString(NULL, TR("OK"), kCFStringEncodingUTF8);\n'
  '    if (t && m) CFUserNotificationDisplayNotice(0, kCFUserNotificationCautionAlertLevel, NULL, NULL, NULL, t, m, ok ? ok : CFSTR("OK"));\n'
  '    if (ok) CFRelease(ok);')
F = "loader/start/win/dsl_start_win.c"
W(F, 'snprintf(why, n, "The mod loader could not point the game to the Dungeon Settlers Modded save folder, so mods are off. "\n'
     '             "This session runs the normal game with your normal saves, as if started from Steam. Close the game and "\n'
     '             "start Dungeon Settlers Modded again to play with mods.");',
  'snprintf(why, n, "%s", TR("The mod loader could not point the game to the Dungeon Settlers Modded save folder, so mods are off. "\n'
  '             "This session runs the normal game with your normal saves, as if started from Steam. Close the game and "\n'
  '             "start Dungeon Settlers Modded again to play with mods."));')
F = "loader/start/mac/dsl_bie_mac.c"
W(F, 'snprintf(why, cap, "%s", !strncmp(last, "ERROR ", 6) ? last + 6 : "the BepInEx install failed");',
  'snprintf(why, cap, "%s", !strncmp(last, "ERROR ", 6) ? last + 6 : TR("the BepInEx install failed"));')
F = "loader/start/mac/dsl_ml_mac.c"
W(F, 'snprintf(why, cap, "%s", !strncmp(last, "ERROR ", 6) ? last + 6 : "the MelonLoader install failed");',
  'snprintf(why, cap, "%s", !strncmp(last, "ERROR ", 6) ? last + 6 : TR("the MelonLoader install failed"));')
F = "loader/start/win/bie_win.c"
W(F, 'snprintf(why, wn, "%s could not be downloaded (error %#lx). Check the internet connection.", what, (unsigned long)r);',
  'snprintf(why, wn, TR("%s could not be downloaded (error %#lx). Check the internet connection."), what, (unsigned long)r);')
W(F, 'snprintf(why, wn, "The %s download is damaged. Try again.", what);', 'snprintf(why, wn, TR("The %s download is damaged. Try again."), what);')
W(F, 'snprintf(why, wn, "%s could not be unpacked into the game copy (disk full?). The game starts without it.", what);',
  'snprintf(why, wn, TR("%s could not be unpacked into the game copy (disk full?). The game starts without it."), what);')
W(F, 'snprintf(mwhy, sizeof mwhy, "BepInEx mods and MelonLoader mods cannot run together: the MelonLoader mods are off this time.");',
  'snprintf(mwhy, sizeof mwhy, "%s", TR("BepInEx mods and MelonLoader mods cannot run together: the MelonLoader mods are off this time."));')
W(F, 'snprintf(why, wn, "%s could not be marked as installed (disk full?).", what);', 'snprintf(why, wn, TR("%s could not be marked as installed (disk full?)."), what);')


def main():
    check = "--check" in sys.argv
    done = skip = miss = 0
    texts = {}
    for path, old, new in E:
        p = os.path.join(ROOT, path)
        if p not in texts:
            texts[p] = open(p, encoding="utf-8").read()
        s = texts[p]
        if new in s:
            skip += 1
            continue
        n = s.count(old)
        if n != 1:
            miss += 1
            print(f"MISSING ({n}x) {path}: {old[:90]!r}")
            continue
        texts[p] = s.replace(old, new, 1)
        done += 1
    if not check:
        for p, s in texts.items():
            if s != open(p, encoding="utf-8").read():
                open(p, "w", encoding="utf-8").write(s)
    print(f"i18n_wrap: {done} applied, {skip} already there, {miss} missing ({len(E)} entries)")
    sys.exit(1 if miss else 0)


if __name__ == "__main__":
    main()
