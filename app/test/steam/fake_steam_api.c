// Stands in for libsteam_api.dylib in steam_check.sh. FAKE_RUN / FAKE_INIT = 1 or 0.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int on(const char *k) { const char *v = getenv(k); return v && *v == '1'; }
uint8_t SteamAPI_IsSteamRunning(void) { return on("FAKE_RUN"); }
int SteamAPI_InitFlat(char *err) { if (on("FAKE_INIT")) return 0; strcpy(err, "fake: no user"); return 1; }
void SteamAPI_Shutdown(void) {}
