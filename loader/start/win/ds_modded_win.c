// "Dungeon Settlers Modded.exe": the Windows start part's starter (no window; the shortcut install.bat makes points
// here). Same contract as the Mac stub (app/stub/ds_modded.c), Windows paths:
//   ROOT    = the folder of this exe (install.bat: %LOCALAPPDATA%\Dungeon Settlers Modded)
//   game    = ROOT\game (full copy of Steam's folder made by install.bat; baselib.dll = the loader proxy)
//   mods    = ROOT\mods\<id>\ (DSL_MODS_DIR)
//   PROFILE = ROOT\profiles\modded (DSRIG_DIR, DSL_LOG_DIR, logs), DATA = PROFILE\data (the game's save folder,
//             redirected there by the proxy)
// Steps: Steam not running -> steam.exe -silent, wait up to 90 s for a signed-in user (the game starts in every case;
// DSL_STEAM=0 tells the proxy to turn the game's Steam check off) -> first start: COPY the normal game's saves (read
// only, never overwrite) -> environment -> start ROOT\game\DungeonSettlers.exe -> exit.
// First start (no DATA\Mods\active.json yet): the default mod profile = every mod in ROOT\mods except those whose mod.json
// says "default_on": false (Hell difficulty ships off by default, PLAN_V13 section 7). The Mods window owns the file after.
// Log: PROFILE\starter.log. --dry: print the environment and the command line instead of starting the game.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "bie_win.h"
#define DSI_IMPLEMENTATION   /* before dsl_internal.h (it includes ds_i18n.h): TR() tables of this exe */
#include "dsl_internal.h"
#include "ds_i18n.h"   /* loader/src: TR() text in the game's language (lang/<code>.json) */

#define APPID L"2798330"
static wchar_t root[MAX_PATH], game[MAX_PATH], profile[MAX_PATH], data[MAX_PATH];
static FILE *lg;

static void logf_(const char *fmt, ...) {
    if (!lg) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(lg, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_list ap; va_start(ap, fmt); vfprintf(lg, fmt, ap); va_end(ap);
    fputc('\n', lg); fflush(lg);
}
/* a dialog from UTF-8 text (printf style; TR() formats): MultiByteToWideChar(CP_UTF8) + MessageBoxW */
static void msgbox(const char *title, const char *fmt, ...) {
    char m[2048]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    dsi_utf8_fix_end(m);
    wchar_t wm[2048], wt[300];
    if (!MultiByteToWideChar(CP_UTF8, 0, m, -1, wm, 2048)) wm[0] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 300)) wcscpy(wt, L"Dungeon Settlers Modded");
    logf_("dialog: %s", m);
    MessageBoxW(NULL, wm, wt, MB_OK | MB_ICONWARNING);
}
static int fail(const char *msg) {
    logf_("ERROR %s", msg);
    msgbox("Dungeon Settlers Modded", "%s", msg);
    return 1;
}
static int exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
static void mkdirs(const wchar_t *p) { SHCreateDirectoryExW(NULL, p, NULL); }

/* ---- Steam */
static DWORD reg_dword(const wchar_t *key, const wchar_t *name) {
    DWORD v = 0, n = sizeof v;
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, NULL, &v, &n) == ERROR_SUCCESS ? v : 0;
}
static int steam_running(void) {
    DWORD pid = reg_dword(L"Software\\Valve\\Steam\\ActiveProcess", L"pid");
    if (!pid) return 0;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    DWORD code = 0; int alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}
static int steam_user(void) { return steam_running() && reg_dword(L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser") != 0; }
static int ensure_steam(void) {
    if (steam_user()) { logf_("steam: running, user signed in"); return 1; }
    if (!steam_running()) {
        wchar_t sp[MAX_PATH]; DWORD n = sizeof sp;
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe", RRF_RT_REG_SZ, NULL, sp, &n) != ERROR_SUCCESS) {
            n = sizeof sp;
            if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, sp, &n) != ERROR_SUCCESS) {
                logf_("steam: not installed (no SteamPath): the game starts without Steam"); return 0;
            }
            wcsncat(sp, L"\\steam.exe", MAX_PATH - wcslen(sp) - 1);
        }
        for (wchar_t *p = sp; *p; p++) if (*p == L'/') *p = L'\\';
        HINSTANCE r = ShellExecuteW(NULL, L"open", sp, L"-silent", NULL, SW_SHOWMINNOACTIVE);
        logf_("steam: started %ls -silent (%lld)", sp, (long long)(INT_PTR)r);
    }
    for (int i = 0; i < 180; i++) {                       /* 90 s */
        if (steam_user()) { logf_("steam: user signed in after %.1f s", i / 2.0); return 1; }
        Sleep(500);
    }
    logf_("steam: no signed-in user after 90 s: the game starts without Steam");
    return 0;
}

/* ---- first start: copy the normal game's saves (never overwrite) */
static void copy_tree(const wchar_t *src, const wchar_t *dst) {
    DWORD a = GetFileAttributesW(src);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) { if (!CopyFileW(src, dst, TRUE) && GetLastError() != ERROR_FILE_EXISTS) logf_("copy %ls failed (%lu)", src, GetLastError()); return; }
    mkdirs(dst);
    wchar_t pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%ls\\*", src);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t s[MAX_PATH], d[MAX_PATH];
        _snwprintf(s, MAX_PATH, L"%ls\\%ls", src, fd.cFileName); _snwprintf(d, MAX_PATH, L"%ls\\%ls", dst, fd.cFileName);
        copy_tree(s, d);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void copy_saves(void) {
    wchar_t flag[MAX_PATH]; _snwprintf(flag, MAX_PATH, L"%ls\\.copied_from_normal_game", data);
    if (exists(flag)) return;
    wchar_t *low = NULL, src[MAX_PATH];
    if (SHGetKnownFolderPath(&FOLDERID_LocalAppDataLow, 0, NULL, &low) != S_OK) { logf_("no LocalLow folder: saves not copied"); return; }
    _snwprintf(src, MAX_PATH, L"%ls\\CanOpener\\Dungeon Settlers", low);
    CoTaskMemFree(low);
    const wchar_t *items[] = { L"Saves", L"OutGame.json", L"UserSetting.json", L"UserCampaignLog.json" };
    for (int i = 0; i < 4; i++) {
        wchar_t s[MAX_PATH], d[MAX_PATH];
        _snwprintf(s, MAX_PATH, L"%ls\\%ls", src, items[i]); _snwprintf(d, MAX_PATH, L"%ls\\%ls", data, items[i]);
        copy_tree(s, d);
    }
    FILE *f = _wfopen(flag, L"w"); if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%04d-%02d-%02d %02d:%02d\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute); fclose(f); }
    logf_("first run: saves copied from %ls", src);
}

/* ---- mod packages: a <id>-<version>.zip dropped into ROOT\mods is unpacked to ROOT\mods\<id> (one top folder <id>\
   or the files at the top). The old folder of that id goes to ROOT\retired, the zip to mods\.installed (bad: .failed).
   A new mod is off until the player turns it on in the Mods window. */
static int mod_id_ok(const char *id) {
    if (!id || !*id || strlen(id) > 60 || id[0] == '.') return 0;
    for (const char *c = id; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-')) return 0;
    return 1;
}
static int read_mod_id(const wchar_t *dir, char *id, size_t n) {   /* 1 = dir\mod.json with a good id */
    wchar_t mp[MAX_PATH]; _snwprintf(mp, MAX_PATH, L"%ls\\mod.json", dir);
    FILE *f = _wfopen(mp, L"rb"); if (!f) return 0;
    static char txt[1 << 16]; size_t k = fread(txt, 1, sizeof txt - 1, f); fclose(f); txt[k] = 0;
    JNode *m = json_parse(txt); const char *s = json_str(m, "id"); int ok = mod_id_ok(s);
    if (ok) snprintf(id, n, "%s", s);
    json_free(m);
    return ok;
}
static void stamp(wchar_t *out, size_t n) { SYSTEMTIME t; GetLocalTime(&t); _snwprintf(out, n, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); }
/* the install's file lists (install.bat writes them; the uninstaller deletes exactly the listed files, then the empty
   listed folders): every file and folder this starter makes under ROOT is added, so the uninstall removes it too */
static void log_line(const wchar_t *list, const wchar_t *path) {
    wchar_t lp[MAX_PATH]; _snwprintf(lp, MAX_PATH, L"%ls\\%ls", root, list);
    if (!exists(lp)) return;                                  /* no lists: an install without them (older kit) */
    FILE *f = _wfopen(lp, L"ab"); if (!f) return;
    char a[MAX_PATH * 3]; WideCharToMultiByte(CP_OEMCP, 0, path, -1, a, sizeof a, NULL, NULL);   /* cmd's dir /b writes OEM */
    fprintf(f, "%s\r\n", a); fclose(f);
}
static void log_tree(const wchar_t *p) {
    DWORD at = GetFileAttributesW(p); if (at == INVALID_FILE_ATTRIBUTES) return;
    if (!(at & FILE_ATTRIBUTE_DIRECTORY)) { log_line(L"install_files.log", p); return; }
    log_line(L"install_dirs.log", p);
    wchar_t pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%ls\\*", p);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t c[MAX_PATH]; _snwprintf(c, MAX_PATH, L"%ls\\%ls", p, fd.cFileName); log_tree(c);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void install_packages(void) {
    wchar_t md[MAX_PATH], pat[MAX_PATH]; _snwprintf(md, MAX_PATH, L"%ls\\mods", root); _snwprintf(pat, MAX_PATH, L"%ls\\*.zip", md);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t zp[MAX_PATH], un[MAX_PATH], ts[32], dst[MAX_PATH];
        _snwprintf(zp, MAX_PATH, L"%ls\\%ls", md, fd.cFileName);
        _snwprintf(un, MAX_PATH, L"%ls\\.unpack-%lu", md, GetCurrentProcessId());
        stamp(ts, 32); mkdirs(un);
        char z8[MAX_PATH * 3], u8[MAX_PATH * 3], id[64] = "";
        WideCharToMultiByte(CP_ACP, 0, zp, -1, z8, sizeof z8, NULL, NULL); WideCharToMultiByte(CP_ACP, 0, un, -1, u8, sizeof u8, NULL, NULL);
        int k = bie_unzip_file(z8, u8);
        wchar_t src[MAX_PATH] = L"";
        if (k > 0 && read_mod_id(un, id, sizeof id)) wcscpy(src, un);
        else if (k > 0) {                                  /* one folder down */
            wchar_t p2[MAX_PATH]; _snwprintf(p2, MAX_PATH, L"%ls\\*", un);
            WIN32_FIND_DATAW f2; HANDLE h2 = FindFirstFileW(p2, &f2);
            if (h2 != INVALID_HANDLE_VALUE) {
                do {
                    if (!(f2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || f2.cFileName[0] == L'.') continue;
                    wchar_t c[MAX_PATH]; _snwprintf(c, MAX_PATH, L"%ls\\%ls", un, f2.cFileName);
                    if (read_mod_id(c, id, sizeof id)) { wcscpy(src, c); break; }
                } while (FindNextFileW(h2, &f2));
                FindClose(h2);
            }
        }
        int ok = 0;
        if (src[0]) {
            wchar_t wid[64]; MultiByteToWideChar(CP_UTF8, 0, id, -1, wid, 64);
            _snwprintf(dst, MAX_PATH, L"%ls\\%ls", md, wid);
            if (exists(dst)) {
                wchar_t rd[MAX_PATH], old[MAX_PATH]; _snwprintf(rd, MAX_PATH, L"%ls\\retired", root); mkdirs(rd);
                _snwprintf(old, MAX_PATH, L"%ls\\mods-%ls-%ls", rd, wid, ts);
                if (!MoveFileW(dst, old)) logf_("mods: cannot move the old %s folder (%lu)", id, GetLastError());
                else { log_line(L"install_dirs.log", rd); log_tree(old); }
            }
            ok = !exists(dst) && MoveFileW(src, dst);
        }
        wchar_t keep[MAX_PATH], kz[MAX_PATH];
        _snwprintf(keep, MAX_PATH, L"%ls\\%ls", md, ok ? L".installed" : L".failed"); mkdirs(keep);
        _snwprintf(kz, MAX_PATH, L"%ls\\%ls-%ls", keep, ts, fd.cFileName);
        if (!MoveFileW(zp, kz)) logf_("mods: cannot move %ls (%lu)", zp, GetLastError());
        else { log_line(L"install_dirs.log", keep); log_tree(kz); }
        if (ok) log_tree(dst);
        if (exists(un) && !RemoveDirectoryW(un)) {         /* what is left of the unpack folder (empty: removed): out of the mods list */
            wchar_t rd[MAX_PATH], lo[MAX_PATH]; _snwprintf(rd, MAX_PATH, L"%ls\\retired", root); mkdirs(rd);
            _snwprintf(lo, MAX_PATH, L"%ls\\unpack-%ls", rd, ts);
            if (!MoveFileW(un, lo)) logf_("mods: cannot move %ls (%lu)", un, GetLastError());
            else { log_line(L"install_dirs.log", rd); log_tree(lo); }
        }
        if (ok) logf_("mods: %ls installed as mods\\%s (%d files)", fd.cFileName, id, k);
        else {
            logf_("mods: %ls is not a mod package (unzip %d, id '%s'): moved to mods\\.failed", fd.cFileName, k, id);
            char zn[MAX_PATH * 3]; if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, zn, sizeof zn, NULL, NULL)) zn[0] = 0;
            msgbox("Dungeon Settlers Modded", TR("%s is not a mod for Dungeon Settlers Modded. It was moved to the mods\\.failed folder. The game starts without it."), zn);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* ---- first start: default mod profile (never overwrites the player's choice) */
static void default_profile(void) {
    wchar_t ap[MAX_PATH], md[MAX_PATH], pat[MAX_PATH];
    _snwprintf(ap, MAX_PATH, L"%ls\\Mods\\active.json", data);
    if (exists(ap)) return;
    _snwprintf(md, MAX_PATH, L"%ls\\mods", root); _snwprintf(pat, MAX_PATH, L"%ls\\*", md);
    char ids[64][64]; int n = 0, off = 0;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.' || n >= 64) continue;
        wchar_t mp[MAX_PATH]; _snwprintf(mp, MAX_PATH, L"%ls\\%ls\\mod.json", md, fd.cFileName);
        FILE *f = _wfopen(mp, L"rb"); if (!f) continue;
        static char txt[1 << 16]; size_t k = fread(txt, 1, sizeof txt - 1, f); fclose(f); txt[k] = 0;
        JNode *m = json_parse(txt); const char *id = json_str(m, "id"); JNode *d = json_get(m, "default_on");
        if (id && strlen(id) < 64 && !strpbrk(id, "\"\\")) {
            if (d && d->t == JN_BOOL && !d->b) off++;
            else snprintf(ids[n++], 64, "%s", id);
        }
        json_free(m);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    wchar_t dir[MAX_PATH]; _snwprintf(dir, MAX_PATH, L"%ls\\Mods", data); mkdirs(dir);
    FILE *f = _wfopen(ap, L"wb"); if (!f) { logf_("default profile: cannot write %ls", ap); return; }
    fputs("{\"enabled\":[", f); for (int i = 0; i < n; i++) fprintf(f, "%s\"%s\"", i ? "," : "", ids[i]);
    fputs("],\"order\":[", f); for (int i = 0; i < n; i++) fprintf(f, "%s\"%s\"", i ? "," : "", ids[i]);
    fputs("]}\n", f); fclose(f);
    logf_("first start: default mod profile, %d mod(s) on, %d off by default", n, off);
}

static void env(const wchar_t *k, const wchar_t *v) { SetEnvironmentVariableW(k, v); }

/* the game's language for our dialogs: LanguageType of DATA\\UserSetting.json (the game's save folder of the modded copy;
   before the first start: the normal game's LocalLow file, which copy_saves copies in); no file = English.
   Texts: ROOT\\game\\lang (next to the loader, game\\baselib.dll; install.bat copies the kit's files\\lang there). */
static char *slurp_w(const wchar_t *p) {
    FILE *f = _wfopen(p, L"rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1 << 22)) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); if (!b) { fclose(f); return NULL; }
    size_t r = fread(b, 1, (size_t)n, f); b[r] = 0; fclose(f);
    return b;
}
static void setup_lang(void) {
    wchar_t ld[MAX_PATH], us[MAX_PATH]; char l8[MAX_PATH * 3];
    _snwprintf(ld, MAX_PATH, L"%ls\\lang", game);
    WideCharToMultiByte(CP_ACP, 0, ld, -1, l8, sizeof l8, NULL, NULL);   /* ds_i18n.h opens files with fopen (ANSI) */
    dsi_set_dir(l8);
    _snwprintf(us, MAX_PATH, L"%ls\\UserSetting.json", data);
    char *t = slurp_w(us);
    if (!t) {
        wchar_t *low = NULL;
        if (SHGetKnownFolderPath(&FOLDERID_LocalAppDataLow, 0, NULL, &low) == S_OK) {
            _snwprintf(us, MAX_PATH, L"%ls\\CanOpener\\Dungeon Settlers\\UserSetting.json", low); CoTaskMemFree(low);
            t = slurp_w(us);
        }
    }
    int lang = dsi_lang_from_usersetting(t);
    free(t);
    dsi_set_lang(lang);
    logf_("language %d (%s) from %ls, texts %s: %d loaded, %d dropped", lang, dsi_code(), exists(us) ? us : L"no UserSetting.json", l8, dsi_loaded(), dsi_dropped());
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show) {
    (void)hi; (void)hp; (void)show;
    int argc = 0; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int dry = 0;
    for (int i = 1; i < argc; i++) if (!wcscmp(argv[i], L"--dry")) dry = 1;

    GetModuleFileNameW(NULL, root, MAX_PATH);
    wchar_t *sl = wcsrchr(root, L'\\'); if (sl) *sl = 0;
    _snwprintf(game, MAX_PATH, L"%ls\\game", root);
    _snwprintf(profile, MAX_PATH, L"%ls\\profiles\\modded", root);
    _snwprintf(data, MAX_PATH, L"%ls\\data", profile);
    mkdirs(data);
    { wchar_t lp[MAX_PATH]; _snwprintf(lp, MAX_PATH, L"%ls\\starter.log", profile); lg = _wfopen(lp, L"a"); }
    logf_("---- start pid %lu, root %ls", GetCurrentProcessId(), root);
    setup_lang();                                                /* before the first dialog */

    wchar_t exe[MAX_PATH], bl[MAX_PATH], blo[MAX_PATH], bfile[MAX_PATH];
    _snwprintf(exe, MAX_PATH, L"%ls\\DungeonSettlers.exe", game);
    _snwprintf(bl, MAX_PATH, L"%ls\\baselib.dll", game);
    _snwprintf(blo, MAX_PATH, L"%ls\\baselib_orig.dll", game);
    _snwprintf(bfile, MAX_PATH, L"%ls\\dsmodded_build.txt", game);
    if (!exists(exe) || !exists(bl) || !exists(blo))
        return fail(TR("The modded game copy is missing or incomplete.\nRun install.bat from the Dungeon Settlers Modded kit again."));
    char build[64] = "";
    { FILE *f = _wfopen(bfile, L"r"); if (f) { if (fgets(build, sizeof build, f)) build[strcspn(build, "\r\n ")] = 0; fclose(f); } }
    wchar_t wbuild[64]; MultiByteToWideChar(CP_UTF8, 0, build, -1, wbuild, 64);

    copy_saves();
    install_packages();
    default_profile();
    int steam = dry ? steam_user() : ensure_steam();

    /* BepInEx plugins of the profile the game starts with (bie_win.c): Doorstop on only when one is enabled */
    char r8[MAX_PATH * 3], g8[MAX_PATH * 3], p8[MAX_PATH * 3], why[600];
    /* paths for bie_win.c's fopen (ANSI file names): CP_ACP is right here; dialog text goes through msgbox (CP_UTF8) */
    WideCharToMultiByte(CP_ACP, 0, root, -1, r8, sizeof r8, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, game, -1, g8, sizeof g8, NULL, NULL);
    { wchar_t pw[MAX_PATH]; _snwprintf(pw, MAX_PATH, L"%ls\\Mods\\active.json", data); WideCharToMultiByte(CP_ACP, 0, pw, -1, p8, sizeof p8, NULL, NULL); }
    bie_set_log(logf_);
    int bie_any = 0; why[0] = 0;
    if (bie_prepare(r8, g8, p8, &bie_any, why, sizeof why))
        msgbox(TR("Dungeon Settlers Modded: BepInEx mods are off this time"), "%s", why);   /* why: bie_win.c's text (TR) */
    SetEnvironmentVariableW(L"DOORSTOP_DISABLE", bie_any ? NULL : L"1");
    bie_env_text("DSL_BIE_REASON", why);                         /* the Mods window's "Not running: <reason>" (%XX-encoded UTF-8) */

    wchar_t v[MAX_PATH * 2];
    env(L"SteamAppId", APPID); env(L"SteamGameId", APPID);
    env(L"DSRIG_DIR", profile);
    env(L"DSRIG_LIVE", L"1");
    if (!GetEnvironmentVariableW(L"DSRIG_FPS", v, 8)) env(L"DSRIG_FPS", L"60");
    _snwprintf(v, MAX_PATH * 2, L"%ls\\mods", root); env(L"DSL_MODS_DIR", v);
    _snwprintf(v, MAX_PATH * 2, L"%ls\\Mods\\active.json", data); env(L"DSL_PROFILE", v);
    env(L"DSL_LOG_DIR", profile);
    _snwprintf(v, MAX_PATH * 2, L"%ls\\Mods\\worlds.json", data); env(L"DSML_SESSION", v);
    env(L"DSML_BUILD", wbuild);
    env(L"DSML_APP", game);
    env(L"DSML_DATA", data);
    { wchar_t *low = NULL;                                       /* the title's "Import a save" copies worlds from there */
      if (SHGetKnownFolderPath(&FOLDERID_LocalAppDataLow, 0, NULL, &low) == S_OK) {
          _snwprintf(v, MAX_PATH * 2, L"%ls\\CanOpener\\Dungeon Settlers\\Saves", low); env(L"DSML_GAME_SAVES", v); CoTaskMemFree(low); } }
    env(L"DSL_STEAM", steam ? L"1" : L"0");

    wchar_t line[4096];
    _snwprintf(line, 4096, L"\"%ls\" -logFile \"%ls\\player.log\"", exe, profile);
    for (int i = 1; i < argc; i++) if (wcscmp(argv[i], L"--dry")) { wcsncat(line, L" \"", 4096 - wcslen(line) - 1); wcsncat(line, argv[i], 4096 - wcslen(line) - 1); wcsncat(line, L"\"", 4096 - wcslen(line) - 1); }

    if (dry) {
        const wchar_t *keys[] = { L"SteamAppId", L"SteamGameId", L"DSRIG_DIR", L"DSRIG_LIVE", L"DSRIG_FPS", L"DSL_MODS_DIR",
                                  L"DSL_PROFILE", L"DSL_LOG_DIR", L"DSML_SESSION", L"DSML_BUILD", L"DSML_APP", L"DSML_DATA", L"DSL_STEAM" };
        FILE *o = lg ? lg : stdout;
        for (int i = 0; i < 13; i++) { GetEnvironmentVariableW(keys[i], v, MAX_PATH * 2); fprintf(o, "ENV %ls=%ls\n", keys[i], v); }
        fprintf(o, "CWD %ls\nCMD %ls\n", game, line); fflush(o);
        return 0;
    }
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, game, &si, &pi)) {
        logf_("CreateProcess %ls failed (%lu)", exe, GetLastError());
        return fail(TR("The game could not be started. See starter.log in the Dungeon Settlers Modded folder (profiles\\modded)."));
    }
    logf_("started %ls (pid %lu, build %s, steam %d)", exe, pi.dwProcessId, build, steam);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return 0;
}
