// dsl_sig.c - the ed25519 check of signed updates (owner 2026-10-18: signed auto-updates, "Build it now").
// Every release that the auto-update or "Get the host's mods" installs carries release-manifest.json (each file's sha256,
// the repo, tag, id and version) and release-manifest.json.sig (base64 of the 64-byte ed25519 signature over the exact
// manifest bytes), made by tools/release_sign/sign_release.sh with a private key that only the owner's Keychain holds.
// dsl_update.c checks the manifest here, then every download against the manifest's sha256.
// Trusted keys: main + spare (rotation: sign with the spare, ship a loader that names a new spare). A build made with
// -DDSL_SIGN_DEV_KEY (tests only) also trusts dev_keys/dev.pub, which is public: sign_release.sh refuses to sign a kit
// whose loader holds it.
// Ed25519 of RFC 8032 (SHA-512, the same as ed25519-dalek in core/), Monocypher 4.0.2 (third_party/monocypher).
#ifndef _WIN32
#pragma GCC visibility push(hidden)
#endif
#include "third_party/monocypher/monocypher-ed25519.c"
#ifndef _WIN32
#pragma GCC visibility pop
#endif
#include <stddef.h>
#include <string.h>

#define DSL_SIG_DEV_PUB "1T/QLlHQ8evd8g7BGFA5kO2x2kBHBXA1zX7AngSSO6U="   /* dev_keys/dev.pub (public) */
static const char *const KEYS[] = {
    "PKXOej/3kgnVvFtQH/NSiONFeA6dkOknbyFme5JyufE=",   /* Keychain ds-modloader-release-signing / ed25519 */
    "LzzGVwZYz2L/fXnTtWqRB7KuYAnsBypIgLZmV9hHwew=",   /* Keychain ds-modloader-release-signing / ed25519-spare */
#ifdef DSL_SIGN_DEV_KEY
    DSL_SIG_DEV_PUB,
#endif
};

/* standard base64 with padding; trailing spaces / new lines allowed. Returns the byte count, -1 = not base64 / too long */
int dsl_b64_dec(const char *s, unsigned char *out, size_t cap) {
    size_t n = 0; unsigned acc = 0; int bits = 0, pad = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s; int v;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { for (const char *t = s; *t; t++) if (!strchr(" \n\r\t", *t)) return -1; break; }
        if (c == '=') { pad++; continue; }
        if (pad) return -1;
        if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52; else if (c == '+') v = 62; else if (c == '/') v = 63; else return -1;
        acc = (acc << 6) | (unsigned)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (n >= cap) return -1; out[n++] = (unsigned char)(acc >> bits); }
    }
    if (pad > 2 || (bits + pad * 6) % 8 || (acc & ((1u << bits) - 1))) return -1;
    return (int)n;
}

/* sig_b64 (the .sig file's text) is an ed25519 signature of msg[0..n) by a trusted key: the key's index; -1 = not */
int dsl_sig_check(const void *msg, size_t n, const char *sig_b64) {
    unsigned char sig[64], pk[32];
    if (!msg || !sig_b64 || dsl_b64_dec(sig_b64, sig, sizeof sig) != 64) return -1;
    for (size_t k = 0; k < sizeof KEYS / sizeof KEYS[0]; k++)
        if (dsl_b64_dec(KEYS[k], pk, sizeof pk) == 32 && !crypto_ed25519_check(sig, pk, msg, n)) return (int)k;
    return -1;
}
/* 1 = this build trusts the public dev key (a test build) */
int dsl_sig_dev(void) {
#ifdef DSL_SIGN_DEV_KEY
    return 1;
#else
    return 0;
#endif
}
