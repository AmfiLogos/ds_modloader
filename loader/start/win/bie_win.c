// BepInEx host of the Windows start part (MASTER_PLAN "BepInEx on Windows"). Built into the starter exe and into the
// baselib.dll proxy (the soft restart prepares B with it).
// A BepInEx plugin is a mod folder <ROOT>\mods\<id>\ with mod.json "kind": "bepinex" and its plugin files (DLLs and
// whatever they ship). A folder with DLLs and no mod.json (a plugin dropped in as it comes from Nexus) gets a mod.json
// written for it (id = folder name, scope gameplay: every multiplayer player needs it).
// Per start (bie_prepare, for the profile the game starts with):
//   - no enabled BepInEx mod: DOORSTOP_DISABLE=1 for the game process (Doorstop, if installed, does nothing);
//   - a BepInEx mod whose mod.json requires / optional / conflicts fail (src/dsl_deps.h, the loader's own rule) is not
//     run; the Mods window says why;
//   - else: BepInEx be.788 (pinned sha256) is in the game copy (downloaded once into <ROOT>\cache, unpacked), the copy's
//     BepInEx\plugins\<id>\ holds exactly the enabled BepInEx mods (others moved out to BepInEx\plugins_off\), and
//     DOORSTOP_DISABLE is not set.
// Doorstop's own process variables (DOORSTOP_INITIALIZED, ...) never reach a child process (bie_env_keep).
// MelonLoader mods (mod.json "kind": "melon", lane melon v13) the same way: the official MelonLoader 0.7.3 x64 zip
// (pinned sha256: version.dll + MelonLoader\ in the game copy, downloaded once into <ROOT>\cache), the copy's
// Mods\<id>\ holds exactly the enabled melon mods (with a manifest.json, MelonLoader's rule for sub-folders), and
// UserData\Loader.cfg says "disable = true" for a start without melon mods. One .NET host per start: a profile with
// BepInEx and melon mods keeps the BepInEx ones (DSL_ML_REASON says why the melon ones are off).
#include "bie_win.h"
#include "dsl_internal.h"
#include "dsl_deps.h"
#include "zlib.h"
#include <windows.h>
#include <urlmon.h>
#include <bcrypt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsl_u8_names_win.h"                     /* last: paths and environment are UTF-8 (wide Windows calls) */

#define BIE_URL  L"https://builds.bepinex.dev/projects/bepinex_be/788/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.788%2B5b766a3.zip"
#define BIE_SHA  "f4cc496bd098a0df4164b81e3737297707f13a47c2478dba2f60eefab784817a"
#define BIE_TAG  "be.788"
#define ML_URL   L"https://github.com/LavaGang/MelonLoader/releases/download/v0.7.3/MelonLoader.x64.zip"
#define ML_SHA   "5b2b2f3d1cd42b59ec886c5bdc2663edae87a0097a4f4a8f58c0965a99dda416"
#define ML_TAG   "0.7.3"

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
    /* every offset and size is checked against the zip's own length (a damaged file must fail, never read past it) */
    if (n < 22) return -1;
    size_t e = n - 22;
    while (e > 0 && rd32(z + e) != 0x06054b50) e--;
    if (rd32(z + e) != 0x06054b50) return -1;
    uint32_t cnt = rd16(z + e + 10), cd = rd32(z + e + 16), files = 0;
    if (cd > n) return -1;
    const unsigned char *p = z + cd, *end = z + n;
    for (uint32_t i = 0; i < cnt; i++) {
        if ((size_t)(end - p) < 46 || rd32(p) != 0x02014b50) return -1;
        uint16_t meth = rd16(p + 10), nl = rd16(p + 28), xl = rd16(p + 30), cl = rd16(p + 32);
        uint32_t csz = rd32(p + 20), usz = rd32(p + 24), lho = rd32(p + 42);
        if ((size_t)(end - p) < 46u + nl + xl + cl || nl == 0) return -1;
        char name[512]; size_t L = nl < 511 ? nl : 511; memcpy(name, p + 46, L); name[L] = 0;
        p += 46 + nl + xl + cl;
        if (strlen(name) != L || strstr(name, "..") || name[0] == '/' || name[0] == '\\' || strchr(name, ':')) return -1;   /* no path escape */
        char out[1400]; snprintf(out, sizeof out, "%s\\%s", dest, name);
        for (char *s = out; *s; s++) if (*s == '/') *s = '\\';
        if (name[L - 1] == '/' || name[L - 1] == '\\') { mkdirs(out); continue; }
        char dir[1400]; snprintf(dir, sizeof dir, "%s", out); char *sl = strrchr(dir, '\\'); if (sl) { *sl = 0; mkdirs(dir); }
        if (lho > n || n - lho < 30 || rd32(z + lho) != 0x04034b50) return -1;
        const unsigned char *lh = z + lho;
        size_t doff = 30u + rd16(lh + 26) + rd16(lh + 28);
        if ((size_t)(end - lh) < doff || (size_t)(end - lh) - doff < csz) return -1;
        const unsigned char *data = lh + doff;
        if (usz > (512u << 20) || (meth == 0 && csz != usz)) return -1;   /* stored: the sizes must agree; no file over 512 MB */
        unsigned char *buf = malloc(usz ? usz : 1);
        if (!buf) return -1;
        if (meth == 0) memcpy(buf, data, usz);
        else if (meth == 8) {
            z_stream s; memset(&s, 0, sizeof s);
            if (inflateInit2(&s, -15) != Z_OK) { free(buf); return -1; }
            s.next_in = (Bytef *)data; s.avail_in = csz; s.next_out = buf; s.avail_out = usz;
            int r = inflate(&s, Z_FINISH); inflateEnd(&s);
            if (r != Z_STREAM_END || s.total_out != usz) { free(buf); return -1; }
        } else { free(buf); return -1; }
        FILE *f = fopen(out, "wb"); if (!f) { free(buf); return -1; }
        size_t w = fwrite(buf, 1, usz, f); int c = fclose(f); free(buf);
        if (w != usz || c) { lg("unzip: writing %s failed (disk full?)", out); return -1; }
        files++;
    }
    return (int)files;
}

/* a zip file unpacked into dest (the starter's mod packages); files written, or -1 */
int bie_unzip_file(const char *zip, const char *dest) {
    size_t n = 0; unsigned char *z = (unsigned char *)slurp(zip, &n);
    if (!z) return -1;
    int k = unzip_to(z, n, dest); free(z);
    return k;
}

/* BepInEx in the game copy: <game>\BepInEx\core + dotnet + winhttp.dll + doorstop_config.ini */
/* each loader counts as installed only after a full unpack: <game>\\.dsm_ok_<what> is written last (an unpack cut by
   a full disk or a closed game leaves no marker and is done again at the next start) */
static int marker(const char *game, const char *what, char *p, size_t n) { return snprintf(p, n, "%s\\.dsm_ok_%s", game, what); }
static int complete(const char *game, const char *what) { char p[1200]; marker(game, what, p, sizeof p); return exists(p); }
static int installed(const char *game) {
    char p[1200]; snprintf(p, sizeof p, "%s\\BepInEx\\core\\BepInEx.Unity.IL2CPP.dll", game);
    char q[1200]; snprintf(q, sizeof q, "%s\\winhttp.dll", game);
    return exists(p) && exists(q) && complete(game, "bepinex");
}
/* MelonLoader in the game copy: <game>\version.dll + MelonLoader\net6 */
static int ml_installed(const char *game) {
    char p[1200]; snprintf(p, sizeof p, "%s\\MelonLoader\\net6\\MelonLoader.dll", game);
    char q[1200]; snprintf(q, sizeof q, "%s\\version.dll", game);
    return exists(p) && exists(q) && complete(game, "melon");
}
/* the pinned zip (cache, else download) unpacked into the game copy */
static int fetch_unpack(const char *root, const char *game, const wchar_t *url, const char *sha, const char *zipname,
                        const char *what, const char *mk, const char *proxy, char *why, size_t wn) {
    char mp[1200]; marker(game, mk, mp, sizeof mp); DeleteFileA(mp);
    char cache[1200], zp[1300]; snprintf(cache, sizeof cache, "%s\\cache", root); mkdirs(cache);
    snprintf(zp, sizeof zp, "%s\\%s", cache, zipname);
    size_t n = 0; unsigned char *z = (unsigned char *)slurp(zp, &n); char h[65] = "";
    if (!z || sha256_hex(z, n, h) || strcmp(h, sha)) {
        free(z); z = NULL;
        wchar_t wz[1300]; MultiByteToWideChar(CP_UTF8, 0, zp, -1, wz, 1300);
        lg("%s: downloading", what);
        HRESULT r = URLDownloadToFileW(NULL, url, wz, 0, NULL);
        if (r != S_OK) { snprintf(why, wn, TR("%s could not be downloaded (error %#lx). Check the internet connection."), what, (unsigned long)r); return -1; }
        z = (unsigned char *)slurp(zp, &n);
        if (!z || sha256_hex(z, n, h) || strcmp(h, sha)) { free(z); lg("%s: download sha256 %s, expected %s", what, h, sha); snprintf(why, wn, TR("The %s download is damaged. Try again."), what); return -1; }
    }
    int k = unzip_to(z, n, game); free(z);
    if (k <= 0) {                                            /* half unpacked: its proxy DLL goes, so the game starts without it */
        char pp[1200]; snprintf(pp, sizeof pp, "%s\\%s", game, proxy); DeleteFileA(pp);
        snprintf(why, wn, TR("%s could not be unpacked into the game copy (disk full?). The game starts without it."), what);
        lg("%s: unpack failed (%d): %s removed, no completion marker", what, k, proxy); return -1;
    }
    FILE *f = fopen(mp, "wb"); int ok = f && fprintf(f, "%s\n", zipname) > 0; if (f && fclose(f)) ok = 0;
    if (!ok) { DeleteFileA(mp); snprintf(why, wn, TR("%s could not be marked as installed (disk full?)."), what); return -1; }
    lg("%s installed in %s (%d files)", what, game, k);
    return 0;
}
static int install(const char *root, const char *game, char *why, size_t wn) {
    if (installed(game)) return 0;
    if (fetch_unpack(root, game, BIE_URL, BIE_SHA, "BepInEx-" BIE_TAG "-win-x64.zip", "BepInEx " BIE_TAG, "bepinex", "winhttp.dll", why, wn)) return -1;
    return installed(game) ? 0 : -1;
}
static int ml_install(const char *root, const char *game, char *why, size_t wn) {
    if (ml_installed(game)) return 0;
    if (fetch_unpack(root, game, ML_URL, ML_SHA, "MelonLoader-" ML_TAG "-x64.zip", "MelonLoader " ML_TAG, "melon", "version.dll", why, wn)) return -1;
    return ml_installed(game) ? 0 : -1;
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
/* a MelonLoader mod DLL carries MelonLoader's attribute names (MelonInfoAttribute); a BepInEx plugin does not */
static int has_melon_dll(const char *dir) {
    char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd); int r = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char p[1400]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { r = has_melon_dll(p); continue; }
        size_t L = strlen(fd.cFileName); if (L < 5 || _stricmp(fd.cFileName + L - 4, ".dll")) continue;
        size_t n = 0; char *b = slurp(p, &n);
        for (size_t i = 0; b && !r && i + 18 <= n; i++) if (b[i] == 'M' && !memcmp(b + i, "MelonInfoAttribute", 18)) r = 1;
        free(b);
    } while (!r && FindNextFileA(h, &fd));
    FindClose(h); return r;
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
        int ml = has_melon_dll(d);
        FILE *f = fopen(mj, "w"); if (!f) continue;
        /* the shared shape (lane bepinexmac): platforms.any.files = the plugin DLLs (AnyCPU) with their sha256 */
        fprintf(f, "{\n \"id\": \"%s\",\n \"name\": \"%s\",\n \"version\": \"0\",\n \"kind\": \"%s\",\n \"scope\": \"gameplay\",\n"
                   " \"platforms\": {\"any\": {\"files\": {%s}}},\n"
                   " \"description\": \"%s (%s %s). Every multiplayer player needs it.\"\n}\n", fd.cFileName, fd.cFileName,
                   ml ? "melon" : "bepinex", files, ml ? "MelonLoader mod" : "BepInEx plugin", ml ? "MelonLoader" : "BepInEx", ml ? ML_TAG : BIE_TAG);
        fclose(f);
        lg("bepinex: %s has plugin DLLs and no mod.json: mod.json written (kind %s)", fd.cFileName, ml ? "melon" : "bepinex");
    } while (FindNextFileA(h, &fd));
    FindClose(h);
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
static void clean_off(const char *off) {               /* leftovers of earlier starts, once nothing uses them */
    char pat[1400]; snprintf(pat, sizeof pat, "%s\\*", off);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { if (fd.cFileName[0] != '.' && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { char d[1500]; snprintf(d, sizeof d, "%s\\%s", off, fd.cFileName); rm_tree(d); } }
    while (FindNextFileA(h, &fd));
    FindClose(h);
}
/* a plugin folder out of the way: deleted when possible; a running game still has its DLLs open, then renamed */
static void move_out(const char *off, const char *dir, const char *name, const char *tag) {
    char to[1500]; mkdirs(off);
    SYSTEMTIME t; GetLocalTime(&t);
    snprintf(to, sizeof to, "%s\\%s-%04d%02d%02d-%02d%02d%02d-%lu", off, name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    if (MoveFileExA(dir, to, 0)) { lg("%s: plugin folder %s moved out%s", tag, name, rm_tree(to) ? " (still in use: removed at a later start)" : " and removed"); }
    else lg("%s: plugin folder %s could not be moved out (error %lu): it stays loaded", tag, name, GetLastError());
}
/* dest (BepInEx\plugins or Mods) holds exactly the wanted mod folders that this loader put there (.dsmodded) */
static void sync_mods(const char *mods, const char *dest, const char *off, char want[][128], int nw, int manifest, const char *tag) {
    char pat[1400]; WIN32_FIND_DATAA fd; HANDLE h;
    clean_off(off);
    if (is_dir(dest)) {                                      /* folders of mods that are off (or gone) leave */
        snprintf(pat, sizeof pat, "%s\\*", dest);
        h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.cFileName[0] == '.' || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                int keep = 0; for (int i = 0; i < nw; i++) if (!_stricmp(want[i], fd.cFileName)) keep = 1;
                if (keep) continue;
                char d[1500], mk[1600]; snprintf(d, sizeof d, "%s\\%s", dest, fd.cFileName);
                snprintf(mk, sizeof mk, "%s\\.dsmodded", d);
                if (!exists(mk)) continue;                    /* only folders this loader put there */
                move_out(off, d, fd.cFileName, tag);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    for (int i = 0; i < nw; i++) {                          /* the enabled ones, fresh from the mod folder */
        char s[1400], d[1500]; snprintf(s, sizeof s, "%s\\%s", mods, want[i]); snprintf(d, sizeof d, "%s\\%s", dest, want[i]);
        if (copy_tree(s, d)) lg("%s: %s: some files could not be copied (a running game keeps the old ones)", tag, want[i]);
        char mk[1600]; snprintf(mk, sizeof mk, "%s\\.dsmodded", d);
        FILE *f = fopen(mk, "w"); if (f) { fputs("copied from the loader's mods folder; replaced at every start\n", f); fclose(f); }
        if (manifest) {                                      /* MelonLoader loads a Mods sub-folder only with a manifest.json */
            snprintf(mk, sizeof mk, "%s\\manifest.json", d);
            if (!exists(mk) && (f = fopen(mk, "w"))) { fprintf(f, "{\"name\": \"%s\"}\n", want[i]); fclose(f); }
        }
    }
}
#ifdef DSL_ML_GUARD
#include "ml_guard.h"                                   /* DSMelonGuard.dll bytes (build_win.sh, ds_multiplayer ext/melon/plugin) */
#endif
/* our MelonLoader plugin (Plugins\DSMelonGuard.dll): native mods keep il2cpp's null for an icall the game stripped
   (MelonLoader's managed stand-in kills the game when C code calls it) */
static void ml_guard(const char *game) {
#ifdef DSL_ML_GUARD
    char d[1300], p[1400]; snprintf(d, sizeof d, "%s\\Plugins", game); mkdirs(d); snprintf(p, sizeof p, "%s\\DSMelonGuard.dll", d);
    size_t n = 0; char *old = slurp(p, &n);
    int same = old && n == sizeof ml_guard_dll && !memcmp(old, ml_guard_dll, n); free(old);
    if (same) return;
    FILE *f = fopen(p, "wb"); if (!f) { lg("melon: cannot write %s", p); return; }
    fwrite(ml_guard_dll, 1, sizeof ml_guard_dll, f); fclose(f);
    lg("melon: guard plugin written (%u bytes)", (unsigned)sizeof ml_guard_dll);
#else
    (void)game; lg("melon: built without the guard plugin");
#endif
}
/* MelonLoader on or off for the next start: its own setting file (it reads it once, at its start) */
static void ml_config(const char *game, int on) {
    char d[1300], p[1400]; snprintf(d, sizeof d, "%s\\UserData", game); mkdirs(d); snprintf(p, sizeof p, "%s\\Loader.cfg", d);
    FILE *f = fopen(p, "w"); if (!f) { lg("melon: cannot write %s", p); return; }
    fprintf(f, "[loader]\ndisable = %s\ndisable_start_screen = true\n\n[console]\nhide_console = true\n", on ? "false" : "true");
    fclose(f);
}

int bie_prepare(const char *root, const char *game, const char *profile, int *any, char *why, size_t wn) {
    *any = 0; if (wn) why[0] = 0;
    char mods[1200]; snprintf(mods, sizeof mods, "%s\\mods", root);
    wrap_loose(mods);
    char *pt = profile ? slurp(profile, NULL) : NULL; JNode *prof = pt ? json_parse(pt) : NULL; free(pt);
    JNode *en = json_get(prof, "enabled"), *ord = json_get(prof, "order"), *prov = json_get(prof, "provided");
    char want[64][128], mwant[64][128]; int nw = 0, nm = 0;
    /* every installed mod for the dependency rule (src/dsl_deps.h), in folder order like the loader */
    enum { MD = 160 }; static DslDep dep[MD]; JNode *mfs[MD]; int nd = 0, nmf = 0;
    char names[MD][128]; int nn = 0;
    char pat[1300]; snprintf(pat, sizeof pat, "%s\\*", mods);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (fd.cFileName[0] != '.' && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && nn < MD) snprintf(names[nn++], 128, "%s", fd.cFileName); }
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    qsort(names, (size_t)nn, 128, (int (*)(const void *, const void *))strcmp);
    for (int k = 0; k < nn; k++) {
        char mj[1400]; snprintf(mj, sizeof mj, "%s\\%s\\mod.json", mods, names[k]);
        char *t = slurp(mj, NULL); JNode *m = t ? json_parse(t) : NULL; free(t);
        const char *id = json_str(m, "id");
        if (!id || _stricmp(id, names[k]) || !dsl_dep_add(dep, &nd, MD, id, m, en, ord, k)) { json_free(m); continue; }   /* folder names: any case */
        mfs[nmf++] = m;
    }
    if (prov && prov->t == JN_ARR) for (JNode *c = prov->child; c; c = c->next)
        if (c->t == JN_STR) { DslDep *x = dsl_dep_add(dep, &nd, MD, c->str, NULL, NULL, NULL, 0); if (x) x->fixed = x->on = 1; }
    dsl_deps_resolve(dep, nd, DSL_LOADER_VERSION, NULL);
    for (int i = 0; i < nd && nw < 64 && nm < 64; i++) {
        int b = !strcmp(dep[i].kind, "bepinex"), m = !strcmp(dep[i].kind, "melon");
        if ((!b && !m) || !dep[i].on) continue;               /* no profile = every installed mod on (the loader's rule) */
        if (!dep[i].ok) { char t[400]; lg("bepinex: %s is not run: %s", dep[i].id, dsl_dep_text(&dep[i], t, sizeof t)); continue; }
        if (b) snprintf(want[nw++], 128, "%s", dep[i].id);
        if (m) snprintf(mwant[nm++], 128, "%s", dep[i].id);
    }
    for (int i = 0; i < nmf; i++) json_free(mfs[i]);
    json_free(prof);
    /* MelonLoader mods: one .NET host per start (BepInEx wins), installed at first use, off through its setting file */
    char mwhy[600] = "";
    if (nm && nw) { snprintf(mwhy, sizeof mwhy, "%s", TR("BepInEx mods and MelonLoader mods cannot run together: the MelonLoader mods are off this time.")); nm = 0; }
    if (nm && ml_install(root, game, mwhy, sizeof mwhy)) nm = 0;
    if (mwhy[0]) lg("melon: OFF: %s", mwhy);
    bie_env_text("DSL_ML_REASON", mwhy);                               /* the Mods window's "Not running: <reason>" */
    if (ml_installed(game)) {
        char mdir[1300], moff[1300]; snprintf(mdir, sizeof mdir, "%s\\Mods", game); snprintf(moff, sizeof moff, "%s\\Mods_off", game);
        sync_mods(mods, mdir, moff, mwant, nm, 1, "melon");
        ml_config(game, nm > 0);
        ml_guard(game);
        lg("melon: %d MelonLoader mod(s) on%s", nm, nm ? "" : " (MelonLoader off for this start)");
    }
    *any = nw > 0;
    char plugins[1300], poff[1300]; snprintf(plugins, sizeof plugins, "%s\\BepInEx\\plugins", game); snprintf(poff, sizeof poff, "%s\\BepInEx\\plugins_off", game);
    if (nw && install(root, game, why, wn)) { *any = 0; clean_off(poff); lg("bepinex: OFF: %s", why); return -1; }
    sync_mods(mods, plugins, poff, want, nw, 0, "bepinex");
    lg("bepinex: %d plugin mod(s) on%s", nw, nw ? "" : " (Doorstop off for this start)");
    return 0;
}

/* environment of a game process we start: Doorstop's own variables never pass on; DOORSTOP_DISABLE per the set */
void bie_env_text(const char *name, const char *utf8) {
    if (!utf8 || !*utf8) { SetEnvironmentVariableA(name, NULL); return; }
    char b[2400]; size_t o = 0;
    for (const unsigned char *c = (const unsigned char *)utf8; *c && o + 4 < sizeof b; c++)
        if (*c >= 0x80 || *c == '%') o += (size_t)snprintf(b + o, sizeof b - o, "%%%02X", *c); else b[o++] = (char)*c;
    b[o] = 0;
    SetEnvironmentVariableA(name, b);
}
int bie_env_keep(const wchar_t *line) { return _wcsnicmp(line, L"DOORSTOP_", 9) != 0; }

/* for dsl_bie_win.c (the core's BepInEx calls) */
int bie_installed(const char *game) { return installed(game); }
int bie_install_now(const char *root, const char *game, char *why, size_t wn) { return install(root, game, why, wn); }
/* for dsl_ml_win.c (the core's MelonLoader calls) */
int bie_ml_installed(const char *game) { return ml_installed(game); }
int bie_ml_install_now(const char *root, const char *game, char *why, size_t wn) { return ml_install(root, game, why, wn); }
