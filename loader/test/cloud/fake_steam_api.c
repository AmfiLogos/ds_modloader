// Fake Steam Cloud for loader/test/cloud (never the real Steam): the ISteamRemoteStorage flat calls dsl_cloud.c uses,
// backed by the folder $FAKE_CLOUD_DIR. Remote time = the file's mtime. $FAKE_QUOTA_TOTAL bytes in all (default 10 GB);
// $FAKE_GAME_USE bytes taken by the game's own files. $FAKE_CLOUD_OFF=1: cloud off for the app.
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define EX __attribute__((visibility("default")))
static void path(char *o, size_t n, const char *f) { snprintf(o, n, "%s/%s", getenv("FAKE_CLOUD_DIR"), f); }
EX void *SteamAPI_SteamRemoteStorage_v016(void) { return getenv("FAKE_CLOUD_DIR") ? (void *)1 : NULL; }
EX bool SteamAPI_ISteamRemoteStorage_FileWrite(void *s, const char *f, const void *d, int32_t n) {
    (void)s; char p[1200]; path(p, sizeof p, f); FILE *o = fopen(p, "wb"); if (!o) return false;
    bool ok = fwrite(d, 1, (size_t)n, o) == (size_t)n; return fclose(o) == 0 && ok; }
EX int32_t SteamAPI_ISteamRemoteStorage_FileRead(void *s, const char *f, void *d, int32_t n) {
    (void)s; char p[1200]; path(p, sizeof p, f); FILE *i = fopen(p, "rb"); if (!i) return 0;
    int32_t r = (int32_t)fread(d, 1, (size_t)n, i); fclose(i); return r; }
EX int32_t SteamAPI_ISteamRemoteStorage_GetFileSize(void *s, const char *f) { (void)s; char p[1200]; struct stat t; path(p, sizeof p, f); return stat(p, &t) ? 0 : (int32_t)t.st_size; }
EX int64_t SteamAPI_ISteamRemoteStorage_GetFileTimestamp(void *s, const char *f) { (void)s; char p[1200]; struct stat t; path(p, sizeof p, f); return stat(p, &t) ? 0 : (int64_t)t.st_mtime; }
EX bool SteamAPI_ISteamRemoteStorage_FileDelete(void *s, const char *f) { (void)s; char p[1200]; path(p, sizeof p, f); return unlink(p) == 0; }
static char names[1000][256]; static int32_t sizes[1000]; static int nn;
EX int32_t SteamAPI_ISteamRemoteStorage_GetFileCount(void *s) {
    (void)s; nn = 0; DIR *d = opendir(getenv("FAKE_CLOUD_DIR")); struct dirent *e; if (!d) return 0;
    while ((e = readdir(d)) && nn < 1000) if (e->d_name[0] != '.') { snprintf(names[nn], 256, "%s", e->d_name); sizes[nn] = SteamAPI_ISteamRemoteStorage_GetFileSize(s, e->d_name); nn++; }
    closedir(d); return nn; }
EX const char *SteamAPI_ISteamRemoteStorage_GetFileNameAndSize(void *s, int i, int32_t *n) { (void)s; if (i < 0 || i >= nn) return ""; *n = sizes[i]; return names[i]; }
EX bool SteamAPI_ISteamRemoteStorage_GetQuota(void *s, uint64_t *total, uint64_t *avail) {
    uint64_t t = getenv("FAKE_QUOTA_TOTAL") ? strtoull(getenv("FAKE_QUOTA_TOTAL"), 0, 10) : 10000000000ULL, used = getenv("FAKE_GAME_USE") ? strtoull(getenv("FAKE_GAME_USE"), 0, 10) : 0;
    SteamAPI_ISteamRemoteStorage_GetFileCount(s); for (int i = 0; i < nn; i++) used += (uint64_t)sizes[i];
    *total = t; *avail = t > used ? t - used : 0; return true; }
EX bool SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount(void *s) { (void)s; return true; }
EX bool SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp(void *s) { (void)s; return !getenv("FAKE_CLOUD_OFF"); }
