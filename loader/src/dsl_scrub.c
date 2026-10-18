// dsl_scrub.c - crash reports (owner 2026-10-18: "ask first"): personal data out of the log text before it leaves the
// PC, and a small gzip writer (no zlib deflate in the Windows build). Pure C: the unit test (loader/test/crashrep)
// runs it on this Mac's real logs. Passes, in this order: folder paths, multiplayer player names, the exact names of
// this PC (dsl_crash.c collects them), e-mail addresses, Steam ids, IPv4, IPv6 / MAC.
#include "dsl_scrub.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t n, cap; int bad; } Sb;
static void sb_put(Sb *b, const char *s, size_t n) {
    if (b->bad) return;
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 4096; while (c < b->n + n + 1) c *= 2;
        char *q = realloc(b->p, c); if (!q) { b->bad = 1; return; }
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void sb_c(Sb *b, char c) { sb_put(b, &c, 1); }
static void sb_s(Sb *b, const char *s) { sb_put(b, s, strlen(s)); }

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int is_alpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alnum(int c) { return is_alpha(c) || is_digit(c); }
static int is_hex(int c) { return is_digit(c) || (lower(c) >= 'a' && lower(c) <= 'f'); }
static int ieq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return 0;
    return 1;
}
static int at(const char *s, size_t n, size_t i, const char *w) { size_t l = strlen(w); return i + l <= n && ieq(s + i, w, l); }
#define U(i) ((unsigned char)s[i])

/* ------------------------------------------------------------------ 1. folder paths: the segment after /Users/,
   /home/, C:\Users\, \Documents and Settings\ (any case, either slash). It ends at the next slash; with no slash in
   the next 64 bytes, at a space or a quote. Shared / Public / Default stay. */
static const char *const KEEP_SEG[] = { "Shared", "Public", "Default", "Default User", "All Users", "<user>", NULL };
static void p_paths(const char *s, size_t n, Sb *o) {
    for (size_t i = 0; i < n;) {
        size_t pl = 0; char c = s[i];
        if ((c == '/' || c == '\\') && at(s, n, i + 1, "users") && i + 6 < n && (s[i + 6] == '/' || s[i + 6] == '\\')) pl = 7;
        else if (c == '/' && at(s, n, i + 1, "home/")) pl = 6;
        else if ((c == '/' || c == '\\') && at(s, n, i + 1, "documents and settings") && i + 23 < n && (s[i + 23] == '/' || s[i + 23] == '\\')) pl = 24;
        if (pl) {
            size_t j = i + pl, k, lim = j + 64 < n ? j + 64 : n; int sep = 0;
            for (k = j; k < lim; k++) { char d = s[k]; if (d == '/' || d == '\\') { sep = 1; break; } if (strchr("\"'<>|\r\n\t:*?", d)) break; }
            if (!sep) for (k = j; k < lim && !strchr("\"'<>|\r\n\t :*?,;)]}", s[k]); k++) {}
            size_t sl = k - j; int keep = sl == 0;
            for (int q = 0; KEEP_SEG[q] && !keep; q++) keep = strlen(KEEP_SEG[q]) == sl && ieq(s + j, KEEP_SEG[q], sl);
            sb_put(o, s + i, pl);
            if (keep) sb_put(o, s + j, sl); else sb_s(o, "<user>");
            i = k; continue;
        }
        sb_c(o, c); i++;
    }
}

/* ------------------------------------------------------------------ 2. multiplayer log lines (ds_multiplayer src):
   "player 2 (NAME) joined", "spectator 1 (NAME)", ' as "NAME"', 'name "NAME"', "waiting for NAME (rc", "name NAME
   allow_speedup" (mp.json line), "server: mail ... -> NAME", "mp: joining NAME (", "invite from NAME: answer" */
static int name_ok(size_t a, size_t b) { return b > a && b - a <= 100; }
static void p_players(const char *s, size_t n, Sb *o) {
    size_t ls = 0;                                               /* start of the current line */
    for (size_t i = 0; i < n;) {
        size_t a = 0, b = 0;
        if ((at(s, n, i, "player ") || at(s, n, i, "spectator ")) && (i == 0 || !is_alpha(U(i - 1)))) {
            size_t j = i + (lower(U(i)) == 'p' ? 7 : 10), k = j;
            while (k < n && is_digit(U(k))) k++;
            if (k > j && k + 1 < n && s[k] == ' ' && s[k + 1] == '(') {
                size_t e = k + 2;
                while (e < n && s[e] != '\n' && !(s[e] == ')' && (e + 1 == n || strchr(" \r\n,:;.", s[e + 1])))) e++;
                if (e < n && s[e] == ')') a = k + 2, b = e;
            }
        } else if (at(s, n, i, " as \"") || at(s, n, i, "name \"")) {
            size_t j = i + (s[i] == ' ' ? 5 : 6), e = j;
            while (e < n && s[e] != '"' && s[e] != '\n') e++;
            if (e < n && s[e] == '"') a = j, b = e;
        } else if (at(s, n, i, "waiting for ")) {
            size_t j = i + 12, e = j;
            while (e < n && s[e] != '\n' && !at(s, n, e, " (rc ")) e++;
            if (e < n && s[e] != '\n') a = j, b = e;
        } else if (at(s, n, i, " name ")) {
            size_t j = i + 6, e = j;
            while (e < n && s[e] != '\n' && !at(s, n, e, " allow_speedup")) e++;
            if (e < n && s[e] != '\n') a = j, b = e;
        } else if (at(s, n, i, "mp: joining ") || at(s, n, i, "invite from ")) {   /* "joining NAME (addr): rc", "invite from NAME: answer" */
            size_t j = i + 12, e = j; const char *stop = s[i] == 'm' ? " (" : ": answer";
            while (e < n && s[e] != '\n' && !at(s, n, e, stop)) e++;
            if (e < n && s[e] != '\n') a = j, b = e;
        } else if (at(s, n, i, "-> ")) {
            int mail = 0; for (size_t q = ls; q + 12 <= i && !mail; q++) mail = !memcmp(s + q, "server: mail", 12);
            size_t j = i + 3, e = j; while (e < n && s[e] != '\n' && s[e] != '\r') e++;
            if (mail && !at(s, n, j, "nobody")) a = j, b = e;
        }
        if (name_ok(a, b)) { sb_put(o, s + i, a - i); sb_s(o, "<player>"); i = b; continue; }
        if (s[i] == '\n') ls = i + 1;
        sb_c(o, s[i]); i++;
    }
}

/* ------------------------------------------------------------------ 3. the exact names of this PC */
static const char *const COMMON[] = { "user", "users", "admin", "root", "steam", "player", "guest", "host", "test", "the",
    "owner", "mac", "windows", "default", "public", "shared", "home", "local", "localhost", "mods", "game", NULL };
void dsl_scrub_init(DslScrub *s) { memset(s, 0, sizeof *s); }
void dsl_scrub_name(DslScrub *s, const char *name) {
    if (!name || s->n >= DSL_SCRUB_NAMES) return;
    while (*name == ' ' || *name == '\t') name++;
    size_t l = strlen(name); while (l && (name[l - 1] == ' ' || name[l - 1] == '\t' || name[l - 1] == '\r' || name[l - 1] == '\n')) l--;
    if (l < 3 || l >= sizeof s->name[0]) return;
    int digits = 1; for (size_t i = 0; i < l; i++) digits &= is_digit((unsigned char)name[i]);
    if (digits) return;
    for (int q = 0; COMMON[q]; q++) if (strlen(COMMON[q]) == l && ieq(name, COMMON[q], l)) return;
    for (int k = 0; k < s->n; k++) if (strlen(s->name[k]) == l && ieq(s->name[k], name, l)) return;
    memcpy(s->name[s->n], name, l); s->name[s->n][l] = 0; s->n++;
}
static void p_names(const DslScrub *sc, const char *s, size_t n, Sb *o) {
    int ord[DSL_SCRUB_NAMES], m = sc ? sc->n : 0; size_t len[DSL_SCRUB_NAMES]; unsigned char first[256] = { 0 };
    for (int k = 0; k < m; k++) {                                 /* longest first: "Ann Lee" before "Ann" */
        len[k] = strlen(sc->name[k]); int p = k;
        while (p > 0 && len[ord[p - 1]] < len[k]) { ord[p] = ord[p - 1]; p--; }
        ord[p] = k;
        unsigned char f = (unsigned char)sc->name[k][0]; first[f] = 1; first[lower(f)] = 1; if (f >= 'a' && f <= 'z') first[f - 32] = 1;
    }
    for (size_t i = 0; i < n;) {
        int hit = 0;
        if (first[U(i)]) for (int q = 0; q < m && !hit; q++) {
            int k = ord[q]; size_t l = len[k];
            if (i + l > n || !ieq(s + i, sc->name[k], l)) continue;
            /* a short name only as a whole word (letters): "Max" is not taken out of "max_players" */
            if (l < 6 && ((i > 0 && is_alpha(U(i - 1))) || (i + l < n && (is_alpha(U(i + l)) || s[i + l] == '_')))) continue;
            sb_s(o, "<name>"); i += l; hit = 1;
        }
        if (!hit) { sb_c(o, s[i]); i++; }
    }
}

/* ------------------------------------------------------------------ 4. e-mail addresses */
static int email_local(int c) { return is_alnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-'; }
static void p_email(const char *s, size_t n, Sb *o) {
    for (size_t i = 0; i < n;) {
        if (s[i] == '@') {
            size_t k = 0; while (k < o->n && email_local((unsigned char)o->p[o->n - 1 - k])) k++;
            size_t j = i + 1, dot = 0, lab = 0; int lab_alpha = 1;
            while (j < n && (is_alnum(U(j)) || s[j] == '-' || s[j] == '.')) {
                if (s[j] == '.') { if (j + 1 < n && (is_alnum(U(j + 1)))) { dot = j; lab = 0; lab_alpha = 1; } else break; }
                else { lab++; lab_alpha &= is_alpha(U(j)); }
                j++;
            }
            if (k && dot && lab >= 2 && lab_alpha) { o->n -= k; o->p[o->n] = 0; sb_s(o, "<email>"); i = j; continue; }
        }
        sb_c(o, s[i]); i++;
    }
}

/* ------------------------------------------------------------------ 5. Steam ids: SteamID64 7656119xxxxxxxxxx,
   [U:1:n], STEAM_x:y:n */
static void p_steam(const char *s, size_t n, Sb *o) {
    for (size_t i = 0; i < n;) {
        if (is_digit(U(i)) && (i == 0 || !is_digit(U(i - 1)))) {
            size_t k = i; while (k < n && is_digit(U(k))) k++;
            if (k - i == 17 && !memcmp(s + i, "7656119", 7)) { sb_s(o, "<steamid>"); i = k; continue; }
            sb_put(o, s + i, k - i); i = k; continue;
        }
        if (at(s, n, i, "[U:1:")) {
            size_t k = i + 5; while (k < n && is_digit(U(k))) k++;
            if (k > i + 5 && k < n && s[k] == ']') { sb_s(o, "[U:1:<id>]"); i = k + 1; continue; }
        }
        if (at(s, n, i, "STEAM_") && i + 10 < n && is_digit(U(i + 6)) && s[i + 7] == ':' && is_digit(U(i + 8)) && s[i + 9] == ':' && is_digit(U(i + 10))) {
            size_t k = i + 10; while (k < n && is_digit(U(k))) k++;
            sb_s(o, "STEAM_<id>"); i = k; continue;
        }
        sb_c(o, s[i]); i++;
    }
}

/* ------------------------------------------------------------------ 6. IPv4 (127.x.x.x and 0.0.0.0 stay) */
static void p_ipv4(const char *s, size_t n, Sb *o) {
    for (size_t i = 0; i < n;) {
        if (is_digit(U(i)) && (i == 0 || (!is_alnum(U(i - 1)) && s[i - 1] != '.'))) {
            size_t k = i; int oc[4], no = 0;
            while (no < 4) {
                size_t d = k; int v = 0; while (d < n && d - k < 3 && is_digit(U(d))) v = v * 10 + (s[d++] - '0');
                if (d == k || (d < n && is_digit(U(d))) || v > 255) break;
                oc[no++] = v; k = d;
                if (no < 4) { if (k < n && s[k] == '.') k++; else break; }
            }
            int tail_ok = no == 4 && !(k < n && (is_digit(U(k)) || (s[k] == '.' && k + 1 < n && is_digit(U(k + 1)))));
            if (tail_ok) {
                if (oc[0] == 127 || (!oc[0] && !oc[1] && !oc[2] && !oc[3])) sb_put(o, s + i, k - i); else sb_s(o, "<ip>");
                i = k; continue;
            }
            size_t e = i; while (e < n && (is_digit(U(e)) || s[e] == '.')) e++;   /* a longer dotted number: as it is */
            sb_put(o, s + i, e - i); i = e; continue;
        }
        sb_c(o, s[i]); i++;
    }
}

/* ------------------------------------------------------------------ 7. IPv6 ("::" or 8 groups; ::1 stays) and MAC
   (6 groups of 2 hex). Never a time (12:34:56) or a C++ name (std::string: next to letters). */
static int v6_char(int c) { return is_hex(c) || c == ':' || c == '.'; }
static void p_ipv6(const char *s, size_t n, Sb *o) {
    for (size_t i = 0; i < n;) {
        if ((is_hex(U(i)) || s[i] == ':') && (i == 0 || (!is_alnum(U(i - 1)) && s[i - 1] != '_' && s[i - 1] != ':' && s[i - 1] != '.'))) {
            size_t j = i; while (j < n && v6_char(U(j))) j++;
            size_t e = j; while (e > i && s[e - 1] == '.') e--;           /* end of a sentence */
            int nextok = !(j < n && (is_alnum(U(j)) || s[j] == '_'));
            int colons = 0, dbl = 0, triple = 0, grp_bad = 0, glen = 0, two_hex = 1, dots = 0;
            for (size_t q = i; q < e; q++) {
                if (s[q] == ':') { colons++; if (q + 1 < e && s[q + 1] == ':') { dbl++; if (q + 2 < e && s[q + 2] == ':') triple = 1; } if (glen != 2) two_hex = 0; glen = 0; }
                else if (s[q] == '.') dots++;
                else if (++glen > 4) grp_bad = 1;
            }
            if (glen != 2) two_hex = 0;
            int v6 = nextok && !triple && !grp_bad && colons >= 2 && (dbl == 1 || colons == 7) && e - i >= 3 && (dots == 0 || dots == 3);
            int mac = nextok && colons == 5 && !dbl && two_hex && !dots;
            if (v6 || mac) {
                int keep = v6 && ((e - i == 3 && !memcmp(s + i, "::1", 3)) || (e - i == 2));
                if (keep) sb_put(o, s + i, e - i); else sb_s(o, mac ? "<mac>" : "<ip6>");
                i = e; continue;
            }
            sb_put(o, s + i, j - i); i = j; continue;
        }
        sb_c(o, s[i]); i++;
    }
}

char *dsl_scrub(const DslScrub *sc, const char *in, size_t n, size_t *out_n) {
    Sb a = { 0 }, b = { 0 };
    sb_put(&a, "", 0); sb_put(&b, "", 0);
    p_paths(in, n, &a);
    b.n = 0; p_players(a.p, a.n, &b);
    a.n = 0; p_names(sc, b.p, b.n, &a);
    b.n = 0; p_email(a.p, a.n, &b);
    a.n = 0; p_steam(b.p, b.n, &a);
    b.n = 0; p_ipv4(a.p, a.n, &b);
    a.n = 0; p_ipv6(b.p, b.n, &a);
    free(b.p);
    if (a.bad || b.bad) { free(a.p); return NULL; }
    if (out_n) *out_n = a.n;
    return a.p;
}

/* ------------------------------------------------------------------ gzip: one final fixed-Huffman block
   (RFC 1951 3.2.6), LZ77 with hash chains over a 32 KB window; RFC 1952 header and trailer */
typedef struct { unsigned char *p; size_t n, cap; uint32_t bb; int bc, bad; } Bw;
static void bw_byte(Bw *w, unsigned char c) {
    if (w->bad) return;
    if (w->n + 1 > w->cap) { size_t c2 = w->cap ? w->cap * 2 : 65536; unsigned char *q = realloc(w->p, c2); if (!q) { w->bad = 1; return; } w->p = q; w->cap = c2; }
    w->p[w->n++] = c;
}
static void bw_bits(Bw *w, uint32_t v, int nb) { w->bb |= v << w->bc; w->bc += nb; while (w->bc >= 8) { bw_byte(w, (unsigned char)(w->bb & 255)); w->bb >>= 8; w->bc -= 8; } }
static void bw_code(Bw *w, uint32_t code, int len) {             /* Huffman codes go most significant bit first */
    uint32_t r = 0; for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    bw_bits(w, r, len);
}
static void put_lit(Bw *w, int c) {
    if (c < 144) bw_code(w, 0x30u + (uint32_t)c, 8);
    else if (c < 256) bw_code(w, 0x190u + (uint32_t)(c - 144), 9);
    else if (c < 280) bw_code(w, (uint32_t)(c - 256), 7);
    else bw_code(w, 0xC0u + (uint32_t)(c - 280), 8);
}
static const uint16_t LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                    4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static void put_match(Bw *w, int len, int dist) {
    int i = 28; while (LBASE[i] > len) i--;
    put_lit(w, 257 + i); if (LEXT[i]) bw_bits(w, (uint32_t)(len - LBASE[i]), LEXT[i]);
    int d = 29; while (DBASE[d] > dist) d--;
    bw_code(w, (uint32_t)d, 5); if (DEXT[d]) bw_bits(w, (uint32_t)(dist - DBASE[d]), DEXT[d]);
}
static uint32_t crc32_of(const unsigned char *p, size_t n) {
    static uint32_t t[256]; static int ready;
    if (!ready) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } ready = 1; }
    uint32_t c = 0xFFFFFFFFu; for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
#define WSIZE 32768
#define HSIZE 32768
int dsl_gzip(const void *in, size_t n, unsigned char **out, size_t *out_n) {
    const unsigned char *s = in; Bw w = { 0 };
    int32_t *head = malloc(HSIZE * sizeof *head), *prev = malloc(WSIZE * sizeof *prev);
    if (!head || !prev) { free(head); free(prev); return -1; }
    for (int i = 0; i < HSIZE; i++) head[i] = -1;
    static const unsigned char hdr[10] = { 0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 255 };
    for (int i = 0; i < 10; i++) bw_byte(&w, hdr[i]);
    bw_bits(&w, 1, 1); bw_bits(&w, 1, 2);                         /* BFINAL, BTYPE 01 = fixed codes */
#define HASH(p) ((((uint32_t)s[p] << 10) ^ ((uint32_t)s[(p) + 1] << 5) ^ s[(p) + 2]) & (HSIZE - 1))
#define INSERT(p) do { uint32_t h_ = HASH(p); prev[(p) & (WSIZE - 1)] = head[h_]; head[h_] = (int32_t)(p); } while (0)
    size_t i = 0;
    while (i < n) {
        int best = 0, bd = 0;
        if (i + 2 < n) {
            int32_t c = head[HASH(i)]; int chain = 48;
            int max = n - i < 258 ? (int)(n - i) : 258;
            while (c >= 0 && (size_t)c < i && i - (size_t)c <= WSIZE && chain-- > 0) {
                if (s[c + best] == s[i + best]) {
                    int l = 0; while (l < max && s[c + l] == s[i + l]) l++;
                    if (l > best) { best = l; bd = (int)(i - (size_t)c); if (l == max) break; }
                }
                int32_t nc = prev[c & (WSIZE - 1)];
                if (nc >= c) break;
                c = nc;
            }
            INSERT(i);
        }
        if (best >= 3) {
            put_match(&w, best, bd);
            for (size_t k = i + 1; k < i + (size_t)best; k++) if (k + 2 < n) INSERT(k);
            i += (size_t)best;
        } else { put_lit(&w, s[i]); i++; }
    }
    put_lit(&w, 256);
    if (w.bc) bw_bits(&w, 0, 8 - w.bc);
    uint32_t crc = crc32_of(s, n), sz = (uint32_t)n;
    for (int k = 0; k < 4; k++) bw_byte(&w, (unsigned char)(crc >> (8 * k)));
    for (int k = 0; k < 4; k++) bw_byte(&w, (unsigned char)(sz >> (8 * k)));
    free(head); free(prev);
    if (w.bad) { free(w.p); return -1; }
    *out = w.p; *out_n = w.n;
    return 0;
}
