// BepInEx host of the Windows start part (bie_win.c).
#pragma once
#include <stddef.h>
#include <wchar.h>
void bie_set_log(void (*f)(const char *fmt, ...));
/* the game copy's BepInEx plugins = the enabled "bepinex" mods of `profile` (NULL / missing = every one); BepInEx is
   installed (download once) when one is enabled. *any = a BepInEx mod runs this start. 0 = ok, -1 = BepInEx off (why). */
int bie_prepare(const char *root, const char *game, const char *profile, int *any, char *why, size_t wn);
int bie_env_keep(const wchar_t *env_line);   /* 0 = a Doorstop process variable: not passed to a child game */
