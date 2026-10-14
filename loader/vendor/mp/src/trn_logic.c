// trn_logic.c - transitions lane, game-free logic: floor seed derivation.
#define TRN_NO_GAME
#include "trn.h"
#include <string.h>

static uint64_t mix64(uint64_t z) {                 /* splitmix64 finaliser */
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

int32_t trn_floor_seed(int64_t tick, int32_t region, int32_t floor, int32_t cur_seed, int32_t attempt) {
    uint64_t h = mix64((uint64_t)tick);
    h = mix64(h ^ ((uint64_t)(uint32_t)region << 32 | (uint32_t)floor));
    h = mix64(h ^ ((uint64_t)(uint32_t)cur_seed << 32 | (uint32_t)attempt));
    int32_t s = (int32_t)(h & 0x7FFFFFFF);          /* System.Random(int): keep it positive */
    return s ? s : 1;
}

