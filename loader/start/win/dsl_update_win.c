// Windows OS part of the auto-update (src/dsl_update_os.h). ROOT = $DSML_ROOT (set by the starter exe), else the
// folder above DSL_MODS_DIR (ROOT\mods). Layout: ROOT\Dungeon Settlers Modded.exe, ROOT\RELEASE, ROOT\game (the game
// copy, our baselib.dll), ROOT\mods\<id>\, ROOT\profiles\ (saves: never touched).
// Kit: the release's "Dungeon-Settlers-Modded-Windows.zip" (folder "Dungeon Settlers Modded\files\..."). Files are
// swapped one by one: a running game maps baselib.dll and the mod DLLs, and a loaded DLL can be renamed but not
// overwritten (a folder with a loaded DLL cannot be renamed at all). Old file -> backup\<same relative path>, new file in.
// Mod: ROOT\mods\<id>\ the same way, file by file; old files the new version lacks go to the backup too.
// Narrow paths here are in the ANSI code page, like getenv and fopen of the core (dsl_update.c); converted to wide inside.
// Built twice: into baselib.dll (check + download) and into the starter exe (install, start/dsl_update_start.c).
// Network: wininet (loaded when first needed): follows redirects, no cache, a User-Agent (the GitHub API needs one).
#include "dsl_internal.h"
#include "../../src/dsl_update_os.h"
#include "bie_win.h"
#include <windows.h>
#include <wininet.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static char root_s[1024];

/* ------------------------------------------------------------------ paths */
static void wide(const char *s, wchar_t *w, int n) { if (!MultiByteToWideChar(CP_ACP, 0, s, -1, w, n)) w[0] = 0; }
static void bs(char *p) { for (; *p; p++) if (*p == '/') *p = '\\'; }

const char *dsl_uos_root(void) {
    if (root_s[0]) return root_s;
    const char *r = getenv("DSML_ROOT");
    if (r && *r) snprintf(root_s, sizeof root_s, "%s", r);
    else {
        const char *m = getenv("DSL_MODS_DIR"); if (!m || !*m) return "";
        snprintf(root_s, sizeof root_s, "%s", m); bs(root_s);
        size_t l = strlen(root_s); while (l > 3 && root_s[l - 1] == '\\') root_s[--l] = 0;
        char *s = strrchr(root_s, '\\'); if (s && s > root_s + 2) *s = 0;
    }
    bs(root_s);
    return root_s;
}
const char *dsl_uos_platform(void) { return "win-x64"; }
int dsl_uos_exists(const char *p) { wchar_t w[1100]; wide(p, w, 1100); return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES; }
static int is_dir(const char *p) {
    wchar_t w[1100]; wide(p, w, 1100); DWORD a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
int dsl_uos_mkdirs(const char *path) {
    wchar_t w[1100]; wide(path, w, 1100);
    for (wchar_t *s = w; *s; s++) if (*s == L'/') *s = L'\\';
    for (wchar_t *s = w + 3; *s; s++) if (*s == L'\\') { *s = 0; CreateDirectoryW(w, NULL); *s = L'\\'; }
    if (CreateDirectoryW(w, NULL)) return 0;
    DWORD a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) ? 0 : -1;
}
int dsl_uos_move(const char *from, const char *to) {
    char par[1100]; snprintf(par, sizeof par, "%s", to); bs(par);
    char *s = strrchr(par, '\\'); if (s) { *s = 0; dsl_uos_mkdirs(par); }
    wchar_t f[1100], t[1100]; wide(from, f, 1100); wide(to, t, 1100);
    for (wchar_t *c = f; *c; c++) if (*c == L'/') *c = L'\\';
    for (wchar_t *c = t; *c; c++) if (*c == L'/') *c = L'\\';
    if (MoveFileExW(f, t, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) return 0;
    dsl_log("update: move %s -> %s failed (error %lu)", from, to, GetLastError());
    return -1;
}
int dsl_uos_release(char *out, size_t cap) {
    char p[1200]; snprintf(p, sizeof p, "%s\\RELEASE", dsl_uos_root());
    FILE *f = fopen(p, "r"); if (!f) return -1;
    if (!fgets(out, (int)cap, f)) out[0] = 0;
    fclose(f);
    out[strcspn(out, "\r\n ")] = 0; return out[0] ? 0 : -1;
}

/* ------------------------------------------------------------------ download (wininet, loaded at first use) */
typedef HINTERNET (WINAPI *OpenW_t)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINTERNET (WINAPI *OpenUrlW_t)(HINTERNET, LPCWSTR, LPCWSTR, DWORD, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *Read_t)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL (WINAPI *Close_t)(HINTERNET);
typedef BOOL (WINAPI *Query_t)(HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *SetOpt_t)(HINTERNET, DWORD, LPVOID, DWORD);
static OpenW_t i_open; static OpenUrlW_t i_url; static Read_t i_read; static Close_t i_close; static Query_t i_query; static SetOpt_t i_opt;
static int net_load(void) {
    if (i_open) return 0;
    HMODULE m = LoadLibraryW(L"wininet.dll");
    if (!m) { dsl_log("update: wininet.dll not loaded (error %lu)", GetLastError()); return -1; }
    i_url = (OpenUrlW_t)(void *)GetProcAddress(m, "InternetOpenUrlW"); i_read = (Read_t)(void *)GetProcAddress(m, "InternetReadFile");
    i_close = (Close_t)(void *)GetProcAddress(m, "InternetCloseHandle"); i_query = (Query_t)(void *)GetProcAddress(m, "HttpQueryInfoW");
    i_opt = (SetOpt_t)(void *)GetProcAddress(m, "InternetSetOptionW");
    OpenW_t o = (OpenW_t)(void *)GetProcAddress(m, "InternetOpenW");
    if (!o || !i_url || !i_read || !i_close || !i_query || !i_opt) { dsl_log("update: wininet.dll without the needed functions"); return -1; }
    i_open = o; return 0;
}
int dsl_uos_fetch(const char *url, const char *out_path) {
    if (net_load()) return -1;
    HINTERNET in = i_open(L"DungeonSettlersModded", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!in) { dsl_log("update: no internet handle (error %lu)", GetLastError()); return -1; }
    DWORD to = 15000; i_opt(in, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
    to = 60000; i_opt(in, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
    wchar_t wu[1100]; MultiByteToWideChar(CP_UTF8, 0, url, -1, wu, 1100);
    HINTERNET h = i_url(in, wu, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L,
                        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_UI |
                        INTERNET_FLAG_NO_COOKIES, 0);
    if (!h) { dsl_log("update: %s: no connection (error %lu)", url, GetLastError()); i_close(in); return -1; }
    DWORD code = 0, cl = sizeof code;
    if (!i_query(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &cl, NULL) || code != 200) {
        dsl_log("update: %s: HTTP %lu", url, code); i_close(h); i_close(in); return -1;
    }
    wchar_t wo[1100]; wide(out_path, wo, 1100);
    FILE *f = _wfopen(wo, L"wb");
    if (!f) { dsl_log("update: cannot write %s", out_path); i_close(h); i_close(in); return -1; }
    char *buf = malloc(65536); DWORD n = 0; int ok = buf != NULL;
    while (ok) {
        if (!i_read(h, buf, 65536, &n)) { ok = 0; dsl_log("update: %s: read failed (error %lu)", url, GetLastError()); break; }
        if (!n) break;
        if (fwrite(buf, 1, n, f) != n) { ok = 0; dsl_log("update: %s: disk write failed", out_path); break; }
    }
    free(buf);
    if (fclose(f)) ok = 0;
    i_close(h); i_close(in);
    return ok ? 0 : -1;
}
int dsl_uos_sha256(const char *path, char hex[65]) {
    wchar_t w[1100]; wide(path, w, 1100);
    FILE *f = _wfopen(w, L"rb"); if (!f) return -1;
    BCRYPT_ALG_HANDLE a = NULL; BCRYPT_HASH_HANDLE h = NULL; unsigned char d[32]; int r = -1;
    if (!BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, NULL, 0) && !BCryptCreateHash(a, &h, NULL, 0, NULL, 0, 0)) {
        unsigned char *b = malloc(1 << 16); size_t n; r = b ? 0 : -1;
        while (b && (n = fread(b, 1, 1 << 16, f)) > 0) if (BCryptHashData(h, b, (ULONG)n, 0)) { r = -1; break; }
        free(b);
        if (!r && !BCryptFinishHash(h, d, 32, 0)) { for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", d[i]); }
        else r = -1;
    }
    if (h) BCryptDestroyHash(h);
    if (a) BCryptCloseAlgorithmProvider(a, 0);
    fclose(f);
    return r;
}
int dsl_uos_kit_asset(const char *n) { return !strcmp(n, "Dungeon-Settlers-Modded-Windows.zip"); }

/* ------------------------------------------------------------------ file-by-file swap */
/* a file the update puts in place goes into ROOT\install_files.log when it is not there yet (the uninstaller deletes
   only the files in that list). No list = an install without one: nothing written. */
static void log_installed(const char *path) {
    char lp[1200]; snprintf(lp, sizeof lp, "%s\\install_files.log", dsl_uos_root());
    wchar_t w[1300], wp[1100]; wide(lp, w, 1300); wide(path, wp, 1100);
    FILE *f = _wfopen(w, L"rb"); if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 4); size_t k = b ? fread(b, 1, (size_t)n, f) : 0; fclose(f);
    if (!b) return;
    int u16 = k >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE, have = 0;   /* new lists: UTF-16LE, BOM */
    if (u16) {
        b[k] = b[k + 1] = 0; wchar_t *t = (wchar_t *)(b + 2), *ctx = NULL;
        for (wchar_t *l = wcstok(t, L"\r\n", &ctx); l && !have; l = wcstok(NULL, L"\r\n", &ctx)) have = !_wcsicmp(l, wp);
    } else {
        b[k] = 0; char *ctx = NULL;
        for (char *l = strtok_s(b, "\r\n", &ctx); l && !have; l = strtok_s(NULL, "\r\n", &ctx)) have = !_stricmp(l, path);
    }
    free(b);
    if (have) return;
    f = _wfopen(w, L"ab"); if (!f) { dsl_log("update: cannot add %s to install_files.log", path); return; }
    if (u16) { fwrite(wp, sizeof(wchar_t), wcslen(wp), f); fwrite(L"\r\n", sizeof(wchar_t), 2, f); }
    else fprintf(f, "%s\r\n", path);
    fclose(f);
}
/* recorded first (pending file), then the old file into the backup (when there is one), then the new one into place */
static int swap_in(const char *newp, const char *path, const char *bk, DslMoves *mv) {
    int old = dsl_uos_exists(path);
    if (dsl_umove_add(mv, path, old ? bk : "")) { dsl_log("update: more than %d files to swap", DSL_UMOVES); return -1; }
    if (old && dsl_uos_move(path, bk)) return -1;
    if (dsl_uos_move(newp, path)) return -1;
    log_installed(path);
    return 0;
}
static void prog(DslMoves *mv, int pct) { if (mv->progress) mv->progress(pct); }
/* every file under src (rel = path below src): swap into dst\rel, old one to bk\rel */
static int swap_tree(const char *src, const char *dst, const char *bk, const char *rel, DslMoves *mv, int *count) {
    char pat[1300]; snprintf(pat, sizeof pat, "%s%s%s\\*", src, rel[0] ? "\\" : "", rel);
    wchar_t wp[1300]; wide(pat, wp, 1300);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(wp, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int r = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        char nm[600], r2[1100]; if (!WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, nm, sizeof nm, NULL, NULL)) { r = -1; break; }
        snprintf(r2, sizeof r2, "%s%s%s", rel, rel[0] ? "\\" : "", nm);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if ((r = swap_tree(src, dst, bk, r2, mv, count))) break; continue; }
        char s[1500], d[1500], b[1500];
        snprintf(s, sizeof s, "%s\\%s", src, r2); snprintf(d, sizeof d, "%s\\%s", dst, r2); snprintf(b, sizeof b, "%s\\%s", bk, r2);
        if (is_dir(d)) { dsl_log("update: %s is a folder, the update has a file there", d); r = -1; break; }
        if ((r = swap_in(s, d, b, mv))) break;
        (*count)++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return r;
}
/* files under dst that src lacks: moved into bk (recorded) */
static int retire_extra(const char *src, const char *dst, const char *bk, const char *rel, DslMoves *mv) {
    char pat[1300]; snprintf(pat, sizeof pat, "%s%s%s\\*", dst, rel[0] ? "\\" : "", rel);
    wchar_t wp[1300]; wide(pat, wp, 1300);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(wp, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int r = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        char nm[600], r2[1100]; if (!WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, nm, sizeof nm, NULL, NULL)) { r = -1; break; }
        snprintf(r2, sizeof r2, "%s%s%s", rel, rel[0] ? "\\" : "", nm);
        char s[1500], d[1500], b[1500];
        snprintf(s, sizeof s, "%s\\%s", src, r2); snprintf(d, sizeof d, "%s\\%s", dst, r2); snprintf(b, sizeof b, "%s\\%s", bk, r2);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if ((r = retire_extra(src, dst, bk, r2, mv))) break; continue; }
        if (dsl_uos_exists(s)) continue;
        if (dsl_umove_add(mv, d, b)) { dsl_log("update: more than %d files to swap", DSL_UMOVES); r = -1; break; }
        if ((r = dsl_uos_move(d, b))) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return r;
}
static int unpack(const char *zip, const char *dst) {
    dsl_uos_mkdirs(dst);
    int k = bie_unzip_file(zip, dst);
    if (k <= 0) dsl_log("update: unzip of %s gave %d", zip, k);
    return k > 0 ? 0 : -1;
}
static void first_line(const char *p, char *out, size_t n) {
    out[0] = 0; FILE *f = fopen(p, "r"); if (!f) return;
    if (!fgets(out, (int)n, f)) out[0] = 0;
    fclose(f); out[strcspn(out, "\r\n ")] = 0;
}

int dsl_uos_kit_install(const char *file, const char *ver, const char *backup, DslMoves *mv, char *why, size_t wn) {
    const char *root = dsl_uos_root();
    char u[1200]; snprintf(u, sizeof u, "%s.new", backup); bs(u);
    prog(mv, 10);
    if (unpack(file, u)) { snprintf(why, wn, TR("the download could not be unpacked")); return -1; }
    prog(mv, 50);
    /* files\ : at the top or one folder down ("Dungeon Settlers Modded\files") */
    char fdir[1400] = "", t[1500];
    snprintf(t, sizeof t, "%s\\files\\baselib.dll", u);
    if (dsl_uos_exists(t)) snprintf(fdir, sizeof fdir, "%s\\files", u);
    else {
        char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", u); wchar_t wp[1300]; wide(pat, wp, 1300);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(wp, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
                char nm[600]; if (!WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, nm, sizeof nm, NULL, NULL)) continue;
                snprintf(t, sizeof t, "%s\\%s\\files\\baselib.dll", u, nm);
                if (dsl_uos_exists(t)) { snprintf(fdir, sizeof fdir, "%s\\%s\\files", u, nm); break; }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    char exe[1500]; snprintf(exe, sizeof exe, "%s\\Dungeon Settlers Modded.exe", fdir);
    if (!fdir[0] || !dsl_uos_exists(exe)) { snprintf(why, wn, TR("the download holds no Dungeon Settlers Modded kit")); return -1; }
    /* the kit is made for one game build; another build needs the full installer (a new game copy) */
    char nb[64], ob[64], p[1500];
    snprintf(p, sizeof p, "%s\\BUILD.txt", fdir); first_line(p, nb, sizeof nb);
    snprintf(p, sizeof p, "%s\\game\\dsmodded_build.txt", root); first_line(p, ob, sizeof ob);
    if (!nb[0] || strcmp(nb, ob)) {
        dsl_log("update: the kit is for game build %s, the game copy is build %s", nb[0] ? nb : "?", ob[0] ? ob : "?");
        snprintf(why, wn, TR("this update needs the installer (new game version)")); return -1;
    }
    char rel[64]; snprintf(p, sizeof p, "%s\\RELEASE", fdir); first_line(p, rel, sizeof rel);
    if (strcmp(rel, ver)) {
        dsl_log("update: the v%s download says RELEASE %s", ver, rel[0] ? rel : "(none)");
        snprintf(why, wn, TR("the download's version number is wrong")); return -1;
    }
    prog(mv, 60);
    static const struct { const char *from, *to; } ONE[] = {
        { "baselib.dll", "game\\baselib.dll" }, { "Dungeon Settlers Modded.exe", "Dungeon Settlers Modded.exe" }, { "RELEASE", "RELEASE" },
    };
    for (size_t i = 0; i < sizeof ONE / sizeof ONE[0]; i++) {
        char s[1500], d[1300], b[1300];
        snprintf(s, sizeof s, "%s\\%s", fdir, ONE[i].from); snprintf(d, sizeof d, "%s\\%s", root, ONE[i].to); snprintf(b, sizeof b, "%s\\%s", backup, ONE[i].to);
        if (!dsl_uos_exists(s)) { snprintf(why, wn, TR("the kit has no %s"), ONE[i].from); return -1; }
        if (swap_in(s, d, b, mv)) { snprintf(why, wn, TR("%s could not be replaced"), ONE[i].to); return -1; }
    }
    /* the kit's mods, file by file; each mod folder the kit brings is replaced whole: its files the new kit lacks go to the
       backup first (a file only the old version had could still be read or loaded). The player's other mods stay. */
    char sm[1500], dm[1300], bm[1300]; int nf = 0, nx = mv->n;
    snprintf(sm, sizeof sm, "%s\\mods", fdir); snprintf(dm, sizeof dm, "%s\\mods", root); snprintf(bm, sizeof bm, "%s\\mods", backup);
    if (is_dir(sm)) {
        char pat[1600]; snprintf(pat, sizeof pat, "%s\\*", sm); wchar_t wp[1600]; wide(pat, wp, 1600);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(wp, &fd); int r = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
                char nm[600], s[2200], d[2000], b[2000];
                if (!WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, nm, sizeof nm, NULL, NULL)) { r = -1; break; }
                snprintf(s, sizeof s, "%s\\%s", sm, nm); snprintf(d, sizeof d, "%s\\%s", dm, nm); snprintf(b, sizeof b, "%s\\%s", bm, nm);
                if (is_dir(d) && (r = retire_extra(s, d, b, "", mv))) break;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (r) { snprintf(why, wn, TR("the mods could not be replaced")); return -1; }
    }
    nx = mv->n - nx;
    prog(mv, 75);
    if (is_dir(sm) && swap_tree(sm, dm, bm, "", mv, &nf)) { snprintf(why, wn, TR("the mods could not be replaced")); return -1; }
    dsl_log("update: Windows kit installed: baselib.dll, the starter, RELEASE and %d mod file(s), %d old mod file(s) the kit "
            "no longer has moved out (old ones in %s)", nf, nx, backup);
    return 0;
}

int dsl_uos_mod_install(const char *zip, const char *id, const char *ver, const char *backup, DslMoves *mv, char *why, size_t wn) {
    char u[1200]; snprintf(u, sizeof u, "%s.new", backup); bs(u);
    if (unpack(zip, u)) { snprintf(why, wn, TR("the %s download could not be unpacked"), id); return -1; }
    char nm[1400], t[1500]; snprintf(nm, sizeof nm, "%s\\%s", u, id); snprintf(t, sizeof t, "%s\\mod.json", nm);
    char *j = NULL; FILE *f = fopen(t, "rb");
    if (f) { j = calloc(1, 1 << 20); if (j && !fread(j, 1, (1 << 20) - 1, f)) j[0] = 0; fclose(f); }
    JNode *m = j ? json_parse(j) : NULL; free(j);
    const char *mid = json_str(m, "id"), *mv_ = json_str(m, "version");
    int ok = mid && !strcmp(mid, id), vok = mv_ && !strcmp(mv_, ver); json_free(m);
    if (!ok) { snprintf(why, wn, TR("the %s download is not that mod"), id); return -1; }
    if (!vok) { snprintf(why, wn, TR("the %s download's version number is wrong"), id); return -1; }
    char dst[1300], bk[1300]; snprintf(dst, sizeof dst, "%s\\mods\\%s", dsl_uos_root(), id); snprintf(bk, sizeof bk, "%s", backup); bs(bk);
    int nf = 0;
    if (retire_extra(nm, dst, bk, "", mv) || swap_tree(nm, dst, bk, "", mv, &nf)) { snprintf(why, wn, TR("the mod %s could not be replaced"), id); return -1; }
    dsl_log("update: mod %s installed into %s (%d file(s))", id, dst, nf);
    return 0;
}
