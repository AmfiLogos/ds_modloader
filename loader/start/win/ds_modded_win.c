// "Dungeon Settlers Modded.exe": the Windows start part's starter (no window; the shortcut install.bat makes points
// here). Same contract as the Mac stub (app/stub/ds_modded.c), Windows paths:
//   ROOT    = the folder of this exe (install.bat: %LOCALAPPDATA%\Dungeon Settlers Modded)
//   game    = ROOT\game (full copy of Steam's folder made by install.bat; baselib.dll = the loader proxy)
//   mods    = ROOT\mods\<id>\ (DSL_MODS_DIR)
//   update  = ROOT\update (auto-update, DSML_ROOT = ROOT): a downloaded update (ready.txt) is installed here, before the
//             game opens, with a small progress window (start/dsl_update_start.c); DSML_UPDATE=<label> tells the game
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
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "bie_win.h"
#include "dsl_internal.h"
#include "dsl_u8_win.h"
#include "dsl_update_os.h"

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
static int fail(const wchar_t *msg) {
    logf_("ERROR %ls", msg);
    MessageBoxW(NULL, msg, L"Dungeon Settlers Modded", MB_OK | MB_ICONWARNING);
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
/* the lists are UTF-16LE with a BOM (any file name, any install path; --lists writes them); a list without the BOM is
   an older install's OEM text and gets OEM lines */
static void log_line(const wchar_t *list, const wchar_t *path) {
    wchar_t lp[MAX_PATH]; _snwprintf(lp, MAX_PATH, L"%ls\\%ls", root, list);
    if (!exists(lp)) return;                                  /* no lists: an install without them (older kit) */
    FILE *f = _wfopen(lp, L"rb"); if (!f) return;
    unsigned char bom[2] = { 0, 0 }; size_t nb = fread(bom, 1, 2, f); fclose(f);
    int u16 = nb == 2 && bom[0] == 0xFF && bom[1] == 0xFE;
    f = _wfopen(lp, L"ab"); if (!f) return;
    if (u16) { fwrite(path, sizeof(wchar_t), wcslen(path), f); fwrite(L"\r\n", sizeof(wchar_t), 2, f); }
    else { char a[MAX_PATH * 3]; WideCharToMultiByte(CP_OEMCP, 0, path, -1, a, sizeof a, NULL, NULL); fprintf(f, "%s\r\n", a); }
    fclose(f);
}
/* --lists (install.bat, after the copy): install_files.log / install_dirs.log of what the install put there, fresh */
static FILE *lf_files, *lf_dirs;
static void wline(FILE *f, const wchar_t *p) { fwrite(p, sizeof(wchar_t), wcslen(p), f); fwrite(L"\r\n", sizeof(wchar_t), 2, f); }
static void list_tree(const wchar_t *p) {
    DWORD at = GetFileAttributesW(p); if (at == INVALID_FILE_ATTRIBUTES) return;
    if (!(at & FILE_ATTRIBUTE_DIRECTORY)) { wline(lf_files, p); return; }
    wline(lf_dirs, p);
    wchar_t pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%ls\\*", p);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t c[MAX_PATH]; _snwprintf(c, MAX_PATH, L"%ls\\%ls", p, fd.cFileName); list_tree(c);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static int write_lists(void) {
    wchar_t fp[MAX_PATH], dp[MAX_PATH], prev[MAX_PATH], x[MAX_PATH];
    _snwprintf(fp, MAX_PATH, L"%ls\\install_files.log", root); _snwprintf(dp, MAX_PATH, L"%ls\\install_dirs.log", root);
    _snwprintf(prev, MAX_PATH, L"%ls\\install_files.prev.log", root);
    if (exists(fp)) CopyFileW(fp, prev, FALSE);
    lf_files = _wfopen(fp, L"wb"); lf_dirs = _wfopen(dp, L"wb");
    if (!lf_files || !lf_dirs) { if (lf_files) fclose(lf_files); if (lf_dirs) fclose(lf_dirs); return 1; }
    fwrite("\xFF\xFE", 1, 2, lf_files); fwrite("\xFF\xFE", 1, 2, lf_dirs);
    list_tree(game); _snwprintf(x, MAX_PATH, L"%ls\\mods", root); list_tree(x);
    _snwprintf(x, MAX_PATH, L"%ls\\Dungeon Settlers Modded.exe", root); list_tree(x);
    _snwprintf(x, MAX_PATH, L"%ls\\RELEASE", root); list_tree(x);
    fclose(lf_files); fclose(lf_dirs);
    return 0;
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
/* new versions the running game could not take (its DLL was loaded, loader/ui/dsl_pkg.c): mods\.pending\<id> -> mods\<id> */
static void apply_pending(const wchar_t *md) {
    wchar_t pd[MAX_PATH], pat[MAX_PATH]; _snwprintf(pd, MAX_PATH, L"%ls\\.pending", md); _snwprintf(pat, MAX_PATH, L"%ls\\*", pd);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        wchar_t src[MAX_PATH], dst[MAX_PATH], ts[32]; char id[64];
        _snwprintf(src, MAX_PATH, L"%ls\\%ls", pd, fd.cFileName); _snwprintf(dst, MAX_PATH, L"%ls\\%ls", md, fd.cFileName);
        if (!read_mod_id(src, id, sizeof id)) continue;
        stamp(ts, 32);
        if (exists(dst)) {
            wchar_t rd[MAX_PATH], old[MAX_PATH]; _snwprintf(rd, MAX_PATH, L"%ls\\retired", root); mkdirs(rd);
            _snwprintf(old, MAX_PATH, L"%ls\\mods-%ls-%ls", rd, fd.cFileName, ts);
            if (!MoveFileW(dst, old)) { logf_("mods: %s still in use (%lu): the new version waits", id, GetLastError()); continue; }
            log_line(L"install_dirs.log", rd); log_tree(old);          /* the uninstall removes the retired copy too */
        }
        if (MoveFileW(src, dst)) { log_tree(dst); logf_("mods: new version of %s installed (from .pending)", id); }
        else logf_("mods: cannot move the new version of %s (%lu)", id, GetLastError());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void install_packages(void) {
    wchar_t md[MAX_PATH], pat[MAX_PATH]; _snwprintf(md, MAX_PATH, L"%ls\\mods", root); _snwprintf(pat, MAX_PATH, L"%ls\\*.zip", md);
    apply_pending(md);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t zp[MAX_PATH], un[MAX_PATH], ts[32], dst[MAX_PATH];
        _snwprintf(zp, MAX_PATH, L"%ls\\%ls", md, fd.cFileName);
        _snwprintf(un, MAX_PATH, L"%ls\\.unpack-%lu", md, GetCurrentProcessId());
        stamp(ts, 32); mkdirs(un);
        char z8[MAX_PATH * 3], u8[MAX_PATH * 3], id[64] = "";
        WideCharToMultiByte(CP_UTF8, 0, zp, -1, z8, sizeof z8, NULL, NULL); WideCharToMultiByte(CP_UTF8, 0, un, -1, u8, sizeof u8, NULL, NULL);
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
        else if (src[0]) {                                 /* a mod, but its folder could not be put in place */
            logf_("mods: %ls (%s) could not be put in place (%lu): moved to mods\\.failed", fd.cFileName, id, GetLastError());
            wchar_t msg[600]; _snwprintf(msg, 600, L"%ls could not be installed (the mods folder could not be changed). It was moved to the mods\\.failed folder. The game starts without the new version.", fd.cFileName);
            MessageBoxW(NULL, msg, L"Dungeon Settlers Modded", MB_OK | MB_ICONWARNING);
        }
        else {
            logf_("mods: %ls is not a mod package (unzip %d, id '%s'): moved to mods\\.failed", fd.cFileName, k, id);
            wchar_t msg[600]; _snwprintf(msg, 600, L"%ls is not a mod for Dungeon Settlers Modded. It was moved to the mods\\.failed folder. The game starts without it.", fd.cFileName);
            MessageBoxW(NULL, msg, L"Dungeon Settlers Modded", MB_OK | MB_ICONWARNING);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* ---- auto-update: installs happen here, before the game opens (start/dsl_update_start.c has the logic, dsl_update_win.c
   the file swaps). The shared code logs through dsl_log (here: starter.log). */
void dsl_log(const char *fmt, ...) {
    if (!lg) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(lg, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_list ap; va_start(ap, fmt); vfprintf(lg, fmt, ap); va_end(ap);
    fputc('\n', lg); fflush(lg);
}

/* "running": another starter of this install (named mutex, held for this starter's whole run) or its game
   (a process whose image is ROOT\game\DungeonSettlers.exe) */
static HANDLE upd_mutex;
static int game_runs(const wchar_t *exe) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = { sizeof pe }; int found = 0;
    for (BOOL ok = Process32FirstW(s, &pe); ok && !found; ok = Process32NextW(s, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"DungeonSettlers.exe")) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) { found = 1; logf_("update: DungeonSettlers.exe pid %lu cannot be opened: counted as running", pe.th32ProcessID); break; }
        wchar_t img[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(h, 0, img, &n) && !_wcsicmp(img, exe)) found = 1;
        CloseHandle(h);
    }
    CloseHandle(s);
    return found;
}
static int starter_running(void) {
    wchar_t lr[MAX_PATH], nm[64]; wcsncpy(lr, root, MAX_PATH - 1); lr[MAX_PATH - 1] = 0; CharLowerW(lr);
    unsigned h = 2166136261u; for (wchar_t *c = lr; *c; c++) h = (h ^ (unsigned)*c) * 16777619u;   /* FNV-1a of the folder */
    _snwprintf(nm, 64, L"Local\\DungeonSettlersModdedStarter-%08x", h);
    upd_mutex = CreateMutexW(NULL, FALSE, nm);
    return upd_mutex && GetLastError() == ERROR_ALREADY_EXISTS;
}

/* the window "Updating Dungeon Settlers Modded to 0.1.N": own thread + message loop, text and bar drawn in WM_PAINT.
   DSL_UPDATE_WINDOW=hidden (tests): made off-screen and never shown. */
#define WM_UPD_PROGRESS (WM_APP + 1)
#define WM_UPD_CLOSE (WM_APP + 2)
static HWND uw_hwnd; static HANDLE uw_thread, uw_ready; static volatile LONG uw_pct; static wchar_t uw_title[300]; static HFONT uw_font;
static LRESULT CALLBACK uw_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_UPD_PROGRESS: InvalidateRect(h, NULL, FALSE); return 0;
    case WM_UPD_CLOSE: DestroyWindow(h); return 0;
    case WM_CLOSE: return 0;                               /* the install is not stopped halfway */
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        HGDIOBJ of = SelectObject(dc, uw_font ? (HGDIOBJ)uw_font : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        RECT tr = { 20, 16, rc.right - 20, 44 }; DrawTextW(dc, uw_title, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT bar = { 20, 54, rc.right - 20, 74 }; LONG pct = uw_pct; if (pct < 0) pct = 0; if (pct > 100) pct = 100;
        HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255)), fg = CreateSolidBrush(RGB(6, 176, 37)), fr = CreateSolidBrush(RGB(160, 160, 160));
        FillRect(dc, &bar, bg);
        RECT in = { bar.left + 1, bar.top + 1, bar.left + 1 + (bar.right - bar.left - 2) * pct / 100, bar.bottom - 1 };
        if (in.right > in.left) FillRect(dc, &in, fg);
        FrameRect(dc, &bar, fr);
        DeleteObject(bg); DeleteObject(fg); DeleteObject(fr);
        SelectObject(dc, of); EndPaint(h, &ps); return 0;
    }
    }
    return DefWindowProcW(h, m, wp, lp);
}
static DWORD WINAPI uw_main(LPVOID arg) {
    (void)arg;
    char hv[16] = ""; int hidden = GetEnvironmentVariableA("DSL_UPDATE_WINDOW", hv, sizeof hv) && !_stricmp(hv, "hidden");
    NONCLIENTMETRICSW nm = { sizeof nm };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof nm, &nm, 0)) uw_font = CreateFontIndirectW(&nm.lfMessageFont);
    WNDCLASSW wc = { 0 }; wc.lpfnWndProc = uw_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"DSModdedUpdate";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;      /* no thick frame, no maximize: not resizable */
    RECT r = { 0, 0, 440, 120 }, wa; AdjustWindowRect(&r, st, FALSE);   /* 440 x 120 inside */
    int w = r.right - r.left, hh = r.bottom - r.top, x = -32000, y = -32000;
    if (!hidden && SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) { x = wa.left + (wa.right - wa.left - w) / 2; y = wa.top + (wa.bottom - wa.top - hh) / 2; }
    uw_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Dungeon Settlers Modded", st, x, y, w, hh, NULL, NULL, wc.hInstance, NULL);
    if (!uw_hwnd) logf_("update window: not created (%lu)", GetLastError());
    else if (!hidden) { ShowWindow(uw_hwnd, SW_SHOWNORMAL); UpdateWindow(uw_hwnd); SetForegroundWindow(uw_hwnd); }
    else logf_("update window: made hidden (DSL_UPDATE_WINDOW=hidden)");
    SetEvent(uw_ready);
    if (uw_hwnd) { MSG msg; while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    uw_hwnd = NULL;
    if (uw_font) { DeleteObject(uw_font); uw_font = NULL; }
    return 0;
}
static void uw_open(const char *title) {
    if (uw_thread) return;
    if (!MultiByteToWideChar(CP_UTF8, 0, title, -1, uw_title, 300)) wcscpy(uw_title, L"Updating Dungeon Settlers Modded");
    uw_pct = 0; uw_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    uw_thread = CreateThread(NULL, 0, uw_main, NULL, 0, NULL);
    if (uw_thread && uw_ready) WaitForSingleObject(uw_ready, 5000);
}
static void uw_progress(int pct) {
    InterlockedExchange(&uw_pct, pct);
    if (uw_hwnd) PostMessageW(uw_hwnd, WM_UPD_PROGRESS, (WPARAM)pct, 0);
}
static void uw_close(void) {
    if (!uw_thread) return;
    if (uw_hwnd) PostMessageW(uw_hwnd, WM_UPD_CLOSE, 0, 0);
    if (WaitForSingleObject(uw_thread, 5000) != WAIT_OBJECT_0) logf_("update window: thread did not end in 5 s");
    CloseHandle(uw_thread); uw_thread = NULL;
    if (uw_ready) { CloseHandle(uw_ready); uw_ready = NULL; }
}
static const DslUpdWin upd_win = { uw_open, uw_progress, uw_close };

/* 1 = this process is done (a new kit was installed and its starter runs now, or that start failed) */
static int update_step(int dry, int *code) {
    char r8[MAX_PATH * 3], label[200];
    WideCharToMultiByte(CP_ACP, 0, root, -1, r8, sizeof r8, NULL, NULL);
    _putenv_s("DSML_ROOT", r8);                            /* dsl_uos_root() reads the CRT copy */
    wchar_t gexe[MAX_PATH]; _snwprintf(gexe, MAX_PATH, L"%ls\\game\\DungeonSettlers.exe", root);
    int other = starter_running(), running = other || game_runs(gexe);
    if (running) logf_("update: %s runs", other ? "another starter of this install" : "the game of this install");
    int kit = dsl_update_starter(r8, running, &upd_win, label, sizeof label);
    if (!kit) { SetEnvironmentVariableA("DSML_UPDATE", label[0] ? label : NULL); return 0; }
    /* the new kit's starter goes on from here (pending.txt: its first start) */
    SetEnvironmentVariableA("DSML_UPDATE", NULL);
    if (upd_mutex) { CloseHandle(upd_mutex); upd_mutex = NULL; }
    wchar_t nexe[MAX_PATH], *cl = _wcsdup(GetCommandLineW());
    _snwprintf(nexe, MAX_PATH, L"%ls\\Dungeon Settlers Modded.exe", root);
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (!cl || !CreateProcessW(nexe, cl, NULL, NULL, FALSE, 0, NULL, root, &si, &pi)) {
        logf_("update: CreateProcess %ls failed (%lu)", nexe, GetLastError()); free(cl);
        *code = fail(L"Dungeon Settlers Modded was updated, but the new version could not be started. Start it again from its shortcut.");
        return 1;
    }
    free(cl);
    logf_("update: new kit installed: started its starter (pid %lu)", pi.dwProcessId);
    if (dry) WaitForSingleObject(pi.hProcess, 120000);    /* --dry (tests): its lines are in the log when this one ends */
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    *code = 0; return 1;
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

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show) {
    (void)hi; (void)hp; (void)show;
    int argc = 0; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int dry = 0;
    for (int i = 1; i < argc; i++) if (!wcscmp(argv[i], L"--dry")) dry = 1;

    GetModuleFileNameW(NULL, root, MAX_PATH);
    wchar_t *sl = wcsrchr(root, L'\\'); if (sl) *sl = 0;
    _snwprintf(game, MAX_PATH, L"%ls\\game", root);
    for (int i = 1; i < argc; i++) if (!wcscmp(argv[i], L"--lists")) return write_lists();   /* install.bat: no game start */
    _snwprintf(profile, MAX_PATH, L"%ls\\profiles\\modded", root);
    _snwprintf(data, MAX_PATH, L"%ls\\data", profile);
    mkdirs(data);
    { wchar_t lp[MAX_PATH]; _snwprintf(lp, MAX_PATH, L"%ls\\starter.log", profile); lg = _wfopen(lp, L"a"); }
    logf_("---- start pid %lu, root %ls", GetCurrentProcessId(), root);
    { int code = 0; if (update_step(dry, &code)) return code; }   /* before the game files are checked: it may put them back */

    wchar_t exe[MAX_PATH], bl[MAX_PATH], blo[MAX_PATH], bfile[MAX_PATH];
    _snwprintf(exe, MAX_PATH, L"%ls\\DungeonSettlers.exe", game);
    _snwprintf(bl, MAX_PATH, L"%ls\\baselib.dll", game);
    _snwprintf(blo, MAX_PATH, L"%ls\\baselib_orig.dll", game);
    _snwprintf(bfile, MAX_PATH, L"%ls\\dsmodded_build.txt", game);
    if (!exists(exe) || !exists(bl) || !exists(blo))
        return fail(L"The modded game copy is missing or incomplete.\nRun install.bat from the Dungeon Settlers Modded kit again.");
    char build[64] = "";
    { FILE *f = _wfopen(bfile, L"r"); if (f) { if (fgets(build, sizeof build, f)) build[strcspn(build, "\r\n ")] = 0; fclose(f); } }
    wchar_t wbuild[64]; MultiByteToWideChar(CP_UTF8, 0, build, -1, wbuild, 64);

    copy_saves();
    install_packages();
    default_profile();
    int steam = dry ? steam_user() : ensure_steam();

    /* BepInEx plugins of the profile the game starts with (bie_win.c): Doorstop on only when one is enabled */
    char r8[MAX_PATH * 3], g8[MAX_PATH * 3], p8[MAX_PATH * 3], why[600];
    WideCharToMultiByte(CP_UTF8, 0, root, -1, r8, sizeof r8, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, game, -1, g8, sizeof g8, NULL, NULL);
    { wchar_t pw[MAX_PATH]; _snwprintf(pw, MAX_PATH, L"%ls\\Mods\\active.json", data); WideCharToMultiByte(CP_UTF8, 0, pw, -1, p8, sizeof p8, NULL, NULL); }
    bie_set_log(logf_);
    int bie_any = 0; why[0] = 0;
    if (bie_prepare(r8, g8, p8, &bie_any, why, sizeof why))
        MessageBoxA(NULL, why, "Dungeon Settlers Modded: BepInEx mods are off this time", MB_OK | MB_ICONWARNING);
    SetEnvironmentVariableW(L"DOORSTOP_DISABLE", bie_any ? NULL : L"1");
    dsl_u8_setenv("DSL_BIE_REASON", why[0] ? why : NULL);   /* the Mods window's "Not running: <reason>" */

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
    env(L"DSML_ROOT", root);                               /* auto-update: RELEASE, update\, the files it swaps */

    wchar_t line[4096];
    _snwprintf(line, 4096, L"\"%ls\" -logFile \"%ls\\player.log\"", exe, profile);
    for (int i = 1; i < argc; i++) if (wcscmp(argv[i], L"--dry")) { wcsncat(line, L" \"", 4096 - wcslen(line) - 1); wcsncat(line, argv[i], 4096 - wcslen(line) - 1); wcsncat(line, L"\"", 4096 - wcslen(line) - 1); }

    if (dry) {
        const wchar_t *keys[] = { L"SteamAppId", L"SteamGameId", L"DSRIG_DIR", L"DSRIG_LIVE", L"DSRIG_FPS", L"DSL_MODS_DIR",
                                  L"DSL_PROFILE", L"DSL_LOG_DIR", L"DSML_SESSION", L"DSML_BUILD", L"DSML_APP", L"DSML_DATA", L"DSL_STEAM", L"DSML_ROOT", L"DSML_UPDATE" };
        FILE *o = lg ? lg : stdout;
        for (int i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) { v[0] = 0; GetEnvironmentVariableW(keys[i], v, MAX_PATH * 2); fprintf(o, "ENV %ls=%ls\n", keys[i], v); }
        fprintf(o, "CWD %ls\nCMD %ls\n", game, line); fflush(o);
        return 0;
    }
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, game, &si, &pi)) {
        logf_("CreateProcess %ls failed (%lu)", exe, GetLastError());
        return fail(L"The game could not be started. See starter.log in the Dungeon Settlers Modded folder (profiles\\modded).");
    }
    logf_("started %ls (pid %lu, build %s, steam %d)", exe, pi.dwProcessId, build, steam);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return 0;
}
