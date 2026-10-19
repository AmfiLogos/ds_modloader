// dsl_sign.c - the release signer (sign_release.sh builds it; the loader's own check, loader/src/dsl_sig.c, is linked in).
// The secret key (base64 of the 32-byte ed25519 seed) only ever comes on stdin and is wiped after use.
//   dsl_sign keygen                 a new secret on stdout (sign_release.sh --keygen pipes it into the Keychain)
//   dsl_sign pub    < secret        its public key (base64)
//   dsl_sign sign F < secret        base64 signature of the exact bytes of file F
//   dsl_sign verify F SIG           exit 0 = SIG (a .sig file) signs F with a key the loader trusts (prints which)
#include "../../loader/src/third_party/monocypher/monocypher.h"
#include "../../loader/src/third_party/monocypher/monocypher-ed25519.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dsl_b64_dec(const char *s, unsigned char *out, size_t cap);
int dsl_sig_check(const void *msg, size_t n, const char *sig_b64);

static void b64_enc(const unsigned char *b, size_t n, char *out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)b[i] << 16 | (i + 1 < n ? (unsigned)b[i + 1] << 8 : 0) | (i + 2 < n ? b[i + 2] : 0);
        out[k++] = T[v >> 18 & 63]; out[k++] = T[v >> 12 & 63];
        out[k++] = i + 1 < n ? T[v >> 6 & 63] : '='; out[k++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[k] = 0;
}
static unsigned char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    size_t cap = 1 << 16, k = 0; unsigned char *b = malloc(cap);
    for (size_t r; b && (r = fread(b + k, 1, cap - k, f)) > 0;) { k += r; if (k == cap) b = realloc(b, cap *= 2); }
    fclose(f); *n = k; return b;
}
/* the secret from stdin into sk (seed + public half); 0 = ok */
static int secret_in(uint8_t sk[64], uint8_t pk[32]) {
    char line[256]; uint8_t seed[32];
    if (!fgets(line, sizeof line, stdin)) return -1;
    int n = dsl_b64_dec(line, seed, sizeof seed);
    crypto_wipe(line, sizeof line);
    if (n != 32) { crypto_wipe(seed, sizeof seed); return -1; }
    crypto_ed25519_key_pair(sk, pk, seed);                /* wipes seed */
    return 0;
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "";
    uint8_t sk[64], pk[32]; char out[200];
    if (!strcmp(cmd, "keygen") && argc == 2) {
        uint8_t seed[32]; FILE *r = fopen("/dev/urandom", "rb");
        if (!r || fread(seed, 1, 32, r) != 32) { fprintf(stderr, "dsl_sign: no random source\n"); return 1; }
        fclose(r); b64_enc(seed, 32, out); crypto_wipe(seed, sizeof seed);
        puts(out); crypto_wipe(out, sizeof out); return 0;
    }
    if (!strcmp(cmd, "pub") && argc == 2) {
        if (secret_in(sk, pk)) { fprintf(stderr, "dsl_sign: no valid secret on stdin\n"); return 1; }
        crypto_wipe(sk, sizeof sk); b64_enc(pk, 32, out); puts(out); return 0;
    }
    if (!strcmp(cmd, "sign") && argc == 3) {
        size_t n; unsigned char *m = slurp(argv[2], &n); uint8_t sig[64];
        if (!m) { fprintf(stderr, "dsl_sign: cannot read %s\n", argv[2]); return 1; }
        if (secret_in(sk, pk)) { fprintf(stderr, "dsl_sign: no valid secret on stdin\n"); return 1; }
        crypto_ed25519_sign(sig, sk, m, n); crypto_wipe(sk, sizeof sk);
        b64_enc(sig, 64, out); puts(out); free(m); return 0;
    }
    if (!strcmp(cmd, "verify") && argc == 4) {
        size_t n, sn; unsigned char *m = slurp(argv[2], &n), *s = slurp(argv[3], &sn);
        if (!m || !s) { fprintf(stderr, "dsl_sign: cannot read the files\n"); return 1; }
        s = realloc(s, sn + 1); s[sn] = 0;
        int k = dsl_sig_check(m, n, (char *)s);
        if (k < 0) { fprintf(stderr, "dsl_sign: %s: the signature is NOT valid for any trusted key\n", argv[2]); return 1; }
        printf("ok: trusted key %d\n", k); return 0;
    }
    fprintf(stderr, "usage: dsl_sign keygen | pub | sign <file> | verify <file> <sig file>\n");
    return 2;
}
