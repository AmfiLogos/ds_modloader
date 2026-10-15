/* In-game wrong-modset message (launcher lane, 2026-10-15).
 * The host and the guest each have a list of GAMEPLAY mod lines, the same lines the launcher hashes
 * (core/src/join.rs modset_lines, loader dsload_modset.json): `<id>@<version>[:<file sha256s>[:<patch hash>]]`,
 * one per line ('\n'), any order. Client mods are never in the list (never required from others).
 * Pure C, no game code: the multiplayer mod calls it when the HELLO modset hashes differ. */
#ifndef DS_MODCHECK_H
#define DS_MODCHECK_H
#include <stddef.h>

enum { DSMC_MAX_MODS = 64 };

typedef struct {
    int missing, extra, version, files;   /* counts per kind */
} dsmc_counts;

/* Builds the player text for the GUEST (the one who cannot join) into out (always 0-terminated).
 * vanilla = 1 when the host's rule is "Vanilla check" (no gameplay mod allowed; host_lines is then empty).
 * names (optional, may be NULL): "id\tDisplay name\n..." to show names instead of ids.
 * Returns the number of differences (0 = same set: out is ""). */
int dsmc_guest_text(const char *host_lines, const char *guest_lines, int vanilla, const char *names,
                    char *out, size_t n, dsmc_counts *counts);

/* The same differences seen from the host (one short line for the host's chat / log), e.g.
 * "The owner cannot join: missing Hell difficulty 1.0.0; extra Cheaper recruits 1.0.0". */
int dsmc_host_text(const char *player, const char *host_lines, const char *guest_lines, int vanilla, const char *names,
                   char *out, size_t n);
#endif
