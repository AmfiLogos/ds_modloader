// BepInEx host of the Windows start part (MASTER_PLAN "BepInEx on Windows"). Built into the starter exe and into the
// baselib.dll proxy (the soft restart prepares B with it).
// A BepInEx plugin is a mod folder <ROOT>\mods\<id>\ with mod.json "kind": "bepinex" and its plugin files (DLLs and
// whatever they ship). A folder with DLLs and no mod.json (a plugin dropped in as it comes from Nexus) gets a mod.json
// written for it (id = folder name, scope gameplay: every multiplayer player needs it).
// Per start (bie_prepare, for the profile the game starts with):
//   - no enabled BepInEx mod: DOORSTOP_DISABLE=1 for the game process (Doorstop, if installed, does nothing);
//   - else: BepInEx be.788 (pinned sha256) is in the game copy (downloaded once into <ROOT>\cache, unpacked), the copy's
//     BepInEx\plugins\<id>\ holds exactly the enabled BepInEx mods (others moved out to BepInEx\plugins_off\), and
//     DOORSTOP_DISABLE is not set.
// Doorstop's own process variables (DOORSTOP_INITIALIZED, ...) never reach a child process (bie_env_keep).
#include "bie_win.h"
#include "dsl_internal.h"
#include "zlib.h"
#include <windows.h>
#include <urlmon.h>
#include <bcrypt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIE_URL  L"https://builds.bepinex.dev/projects/bepinex_be/788/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.788%2B5b766a3.zip"
#define BIE_SHA  "f4cc496bd098a0df4164b81e3737297707f13a47c2478dba2f60eefab784817a"
#define BIE_TAG  "be.788"

static void (*logf_)(const char *fmt, ...);
static void lg(const char *fmt, ...) {
    if (!logf_) return;
    char b[1600]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    logf_("%s", b);
}
void bie_set_log(void (*f)(const char *fmt, ...)) { logf_ = f; }

static int exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
static int is_dir(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static void mkdirs(const char *p) {
    char b[1200]; snprintf(b, sizeof b, "%s", p);
    for (char *s = b + 3; *s; s++) if (*s == '\\' || *s == '/') { char c = *s; *s = 0; CreateDirectoryA(b, NULL); *s = c; }
    CreateDirectoryA(b, NULL);
}
static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    if (L < 0) { fclose(f); return NULL; }
    char *b = malloc((size_t)L + 1); size_t r = fread(b, 1, (size_t)L, f); fclose(f); b[r] = 0; if (n) *n = r; return b;
}
static int sha256_hex(const unsigned char *d, size_t n, char out[65]) {
    BCRYPT_ALG_HANDLE a = NULL; BCRYPT_HASH_HANDLE h = NULL; unsigned char md[32]; int r = -1;
    if (!BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, NULL, 0) && !BCryptCreateHash(a, &h, NULL, 0, NULL, 0, 0) &&
        !BCryptHashData(h, (PUCHAR)d, (ULONG)n, 0) && !BCryptFinishHash(h, md, 32, 0)) {
        for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", md[i]); r = 0;
    }
    if (h) BCryptDestroyHash(h);
    if (a) BCryptCloseAlgorithmProvider(a, 0);
    return r;
}

/* ---- zip (stored + deflate), enough for the BepInEx release zip */
static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int unzip_to(const unsigned char *z, size_t n, const char *dest) {
    size_t e = n >= 22 ? n - 22 : 0;
    while (e > 0 && rd32(z + e) != 0x06054b50) e--;
    if (rd32(z + e) != 0x06054b50) return -1;
    uint32_t cnt = rd16(z + e + 10), cd = rd32(z + e + 16), files = 0;
    const unsigned char *p = z + cd;
    for (uint32_t i = 0; i < cnt; i++) {
        if (p + 46 > z + n || rd32(p) != 0x02014b50) return -1;
        uint16_t meth = rd16(p + 10), nl = rd16(p + 28), xl = rd16(p + 30), cl = rd16(p + 32);
        uint32_t csz = rd32(p + 20), usz = rd32(p + 24), lho = rd32(p + 42);
        char name[512]; size_t L = nl < 511 ? nl : 511; memcpy(name, p + 46, L); name[L] = 0;
        p += 46 + nl + xl + cl;
        if (strstr(name, "..") || name[0] == '/' || name[0] == '\\') return -1;         /* no path escape */
        char out[1400]; snprintf(out, sizeof out, "%s\\%s", dest, name);
        for (char *s = out; *s; s++) if (*s == '/') *s = '\\';
        if (L && (name[L - 1] == '/' || name[L - 1] == '\\')) { mkdirs(out); continue; }
        char dir[1400]; snprintf(dir, sizeof dir, "%s", out); char *sl = strrchr(dir, '\\'); if (sl) { *sl = 0; mkdirs(dir); }
        const unsigned char *lh = z + lho;
        if (lh + 30 > z + n || rd32(lh) != 0x04034b50) return -1;
        const unsigned char *data = lh + 30 + rd16(lh + 26) + rd16(lh + 28);
        if (data + csz > z + n) return -1;
        unsigned char *buf = malloc(usz ? usz : 1);
        if (meth == 0) memcpy(buf, data, usz);
        else if (meth == 8) {
            z_stream s; memset(&s, 0, sizeof s);
            if (inflateInit2(&s, -15) != Z_OK) { free(buf); return -1; }
            s.next_in = (Bytef *)data; s.avail_in = csz; s.next_out = buf; s.avail_out = usz;
            int r = inflate(&s, Z_FINISH); inflateEnd(&s);
            if (r != Z_STREAM_END || s.total_out != usz) { free(buf); return -1; }
        } else { free(buf); return -1; }
        FILE *f = fopen(out, "wb"); if (!f) { free(buf); return -1; }
        fwrite(buf, 1, usz, f); fclose(f); free(buf); files++;
    }
    return (int)files;
}

/* BepInEx in the game copy: <game>\BepInEx\core + dotnet + winhttp.dll + doorstop_config.ini */
static int installed(const char *game) {
    char p[1200]; snprintf(p, sizeof p, "%s\\BepInEx\\core\\BepInEx.Unity.IL2CPP.dll", game);
    char q[1200]; snprintf(q, sizeof q, "%s\\winhttp.dll", game);
    return exists(p) && exists(q);
}
static int install(const char *root, const char *game, char *why, size_t wn) {
    if (installed(game)) return 0;
    char cache[1200], zp[1300]; snprintf(cache, sizeof cache, "%s\\cache", root); mkdirs(cache);
    snprintf(zp, sizeof zp, "%s\\BepInEx-%s-win-x64.zip", cache, BIE_TAG);
    size_t n = 0; unsigned char *z = (unsigned char *)slurp(zp, &n); char h[65] = "";
    if (!z || sha256_hex(z, n, h) || strcmp(h, BIE_SHA)) {
        free(z); z = NULL;
        wchar_t wz[1300]; MultiByteToWideChar(CP_ACP, 0, zp, -1, wz, 1300);
        lg("bepinex: downloading BepInEx %s", BIE_TAG);
        HRESULT r = URLDownloadToFileW(NULL, BIE_URL, wz, 0, NULL);
        if (r != S_OK) { snprintf(why, wn, "BepInEx %s could not be downloaded (error %#lx). Check the internet connection.", BIE_TAG, (unsigned long)r); return -1; }
        z = (unsigned char *)slurp(zp, &n);
        if (!z || sha256_hex(z, n, h) || strcmp(h, BIE_SHA)) { free(z); snprintf(why, wn, "The BepInEx %s download is not the expected file (sha256 %s).", BIE_TAG, h); return -1; }
    }
    int k = unzip_to(z, n, game); free(z);
    if (k <= 0) { snprintf(why, wn, "BepInEx %s could not be unpacked into the game copy.", BIE_TAG); return -1; }
    lg("bepinex: BepInEx %s installed in %s (%d files)", BIE_TAG, game, k);
    return installed(game) ? 0 : -1;
}

/* ---- the mods: <ROOT>\mods\<id>\mod.json kind "bepinex" */
static int dll_count(const char *dir) {
    char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd); int n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char p[1400]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) n += dll_count(p);
        else { size_t L = strlen(fd.cFileName); if (L > 4 && !_stricmp(fd.cFileName + L - 4, ".dll")) n++; }
    } while (FindNextFileA(h, &fd));
    FindClose(h); return n;
}
/* "rel/name.dll":"sha256" members for every DLL under dir */
static void list_dlls(const char *dir, const char *rel, char *out, size_t cap) {
    char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char p[1400], r[600]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName); snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { list_dlls(p, r, out, cap); continue; }
        size_t L = strlen(fd.cFileName); if (L < 5 || _stricmp(fd.cFileName + L - 4, ".dll")) continue;
        size_t n = 0; char *b = slurp(p, &n); char hx[65] = "";
        if (b && !sha256_hex((unsigned char *)b, n, hx)) { size_t k = strlen(out); snprintf(out + k, cap - k, "%s\"%s\": \"%s\"", k ? ", " : "", r, hx); }
        free(b);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
static void wrap_loose(const char *mods) {          /* a plugin folder dropped in without mod.json */
    char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", mods);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.' || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        char d[1400], mj[1500]; snprintf(d, sizeof d, "%s\\%s", mods, fd.cFileName); snprintf(mj, sizeof mj, "%s\\mod.json", d);
        if (exists(mj) || !dll_count(d)) continue;
        char files[8192]; files[0] = 0; list_dlls(d, "", files, sizeof files);
        FILE *f = fopen(mj, "w"); if (!f) continue;
        /* the shared shape (lane bepinexmac): platforms.any.files = the plugin DLLs (AnyCPU) with their sha256 */
        fprintf(f, "{\n \"id\": \"%s\",\n \"name\": \"%s\",\n \"version\": \"0\",\n \"kind\": \"bepinex\",\n \"scope\": \"gameplay\",\n"
                   " \"platforms\": {\"any\": {\"files\": {%s}}},\n"
                   " \"description\": \"BepInEx plugin (BepInEx %s). Every multiplayer player needs it.\"\n}\n", fd.cFileName, fd.cFileName, files, BIE_TAG);
        fclose(f);
        lg("bepinex: %s has plugin DLLs and no mod.json: mod.json written (kind bepinex)", fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
static int is_bepinex(const char *mods, const char *id) {
    char mj[1400]; snprintf(mj, sizeof mj, "%s\\%s\\mod.json", mods, id);
    char *t = slurp(mj, NULL); JNode *m = t ? json_parse(t) : NULL; free(t);
    const char *k = json_str(m, "kind"); int r = k && !strcmp(k, "bepinex");
    json_free(m); return r;
}
/* copy a mod folder (without its mod.json) into dst */
static int copy_tree(const char *src, const char *dst) {
    mkdirs(dst);
    char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", src);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd); int bad = 0;
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..") || !_stricmp(fd.cFileName, "mod.json")) continue;
        char s[1400], d[1400]; snprintf(s, sizeof s, "%s\\%s", src, fd.cFileName); snprintf(d, sizeof d, "%s\\%s", dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) bad |= copy_tree(s, d) != 0;
        else if (!CopyFileA(s, d, FALSE)) bad = 1;
    } while (FindNextFileA(h, &fd));
    FindClose(h); return bad ? -1 : 0;
}
/* our own copies only (the mod folder keeps the original): 0 = gone */
static int rm_tree(const char *dir) {
    char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char p[1400]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rm_tree(p); else DeleteFileA(p);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryA(dir) ? 0 : -1;
}
static void clean_off(const char *game) {              /* leftovers of earlier starts, once nothing uses them */
    char off[1300], pat[1400]; snprintf(off, sizeof off, "%s\\BepInEx\\plugins_off", game); snprintf(pat, sizeof pat, "%s\\*", off);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { if (fd.cFileName[0] != '.' && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { char d[1500]; snprintf(d, sizeof d, "%s\\%s", off, fd.cFileName); rm_tree(d); } }
    while (FindNextFileA(h, &fd));
    FindClose(h);
}
/* a plugin folder out of the way: deleted when possible; a running game still has its DLLs open, then renamed */
static void move_out(const char *game, const char *dir, const char *name) {
    char off[1300], to[1500]; snprintf(off, sizeof off, "%s\\BepInEx\\plugins_off", game); mkdirs(off);
    SYSTEMTIME t; GetLocalTime(&t);
    snprintf(to, sizeof to, "%s\\%s-%04d%02d%02d-%02d%02d%02d-%lu", off, name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    if (MoveFileExA(dir, to, 0)) { lg("bepinex: plugin folder %s moved out%s", name, rm_tree(to) ? " (still in use: removed at a later start)" : " and removed"); }
    else lg("bepinex: plugin folder %s could not be moved out (error %lu): it stays loaded", name, GetLastError());
}

int bie_prepare(const char *root, const char *game, const char *profile, int *any, char *why, size_t wn) {
    *any = 0; if (wn) why[0] = 0;
    char mods[1200]; snprintf(mods, sizeof mods, "%s\\mods", root);
    wrap_loose(mods);
    char *pt = profile ? slurp(profile, NULL) : NULL; JNode *prof = pt ? json_parse(pt) : NULL; free(pt);
    JNode *en = json_get(prof, "enabled");
    char want[64][128]; int nw = 0;
    char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", mods);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.' || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || nw >= 64) continue;
            if (!is_bepinex(mods, fd.cFileName)) continue;
            int on = 1;                                       /* no profile = every installed mod on (the loader's rule) */
            if (en && en->t == JN_ARR) { on = 0; for (JNode *c = en->child; c; c = c->next) if (c->t == JN_STR && !strcmp(c->str, fd.cFileName)) on = 1; }
            if (on) snprintf(want[nw++], 128, "%s", fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    json_free(prof);
    *any = nw > 0;
    char plugins[1300]; snprintf(plugins, sizeof plugins, "%s\\BepInEx\\plugins", game);
    clean_off(game);
    if (nw && install(root, game, why, wn)) { *any = 0; lg("bepinex: OFF: %s", why); return -1; }
    if (is_dir(plugins)) {                                   /* folders of mods that are off (or gone) leave */
        snprintf(pat, sizeof pat, "%s\\*", plugins);
        h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.cFileName[0] == '.' || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                int keep = 0; for (int i = 0; i < nw; i++) if (!_stricmp(want[i], fd.cFileName)) keep = 1;
                if (keep) continue;
                char d[1500], mk[1600]; snprintf(d, sizeof d, "%s\\%s", plugins, fd.cFileName);
                snprintf(mk, sizeof mk, "%s\\.dsmodded", d);
                if (!exists(mk)) continue;                    /* only folders this loader put there */
                move_out(game, d, fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    for (int i = 0; i < nw; i++) {                          /* the enabled ones, fresh from the mod folder */
        char s[1400], d[1500]; snprintf(s, sizeof s, "%s\\%s", mods, want[i]); snprintf(d, sizeof d, "%s\\%s", plugins, want[i]);
        if (copy_tree(s, d)) lg("bepinex: %s: some files could not be copied (a running game keeps the old ones)", want[i]);
        char mk[1600]; snprintf(mk, sizeof mk, "%s\\.dsmodded", d);
        FILE *f = fopen(mk, "w"); if (f) { fputs("copied from the loader's mods folder; replaced at every start\n", f); fclose(f); }
    }
    lg("bepinex: %d plugin mod(s) on%s", nw, nw ? "" : " (Doorstop off for this start)");
    return 0;
}

/* environment of a game process we start: Doorstop's own variables never pass on; DOORSTOP_DISABLE per the set */
int bie_env_keep(const wchar_t *line) { return _wcsnicmp(line, L"DOORSTOP_", 9) != 0; }
