#include "../ds_modcheck.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    char t[2048]; dsmc_counts c;
    const char *names = "hell-difficulty\tHell difficulty\ncheaper-recruits\tCheaper recruits\n";
    /* same set, any order -> 0, empty text */
    CHECK(dsmc_guest_text("hell-difficulty@1.0.0:aa:bb\nmultiplayer@1.0.0:cc", "multiplayer@1.0.0:cc\nhell-difficulty@1.0.0:aa:bb\n", 0, names, t, sizeof t, &c) == 0);
    CHECK(t[0] == 0);
    /* missing + extra + version + files */
    int n = dsmc_guest_text("hell-difficulty@1.0.0:aa\nmultiplayer@1.1.0:cc\nbig-maps@2.0.0:dd",
                            "multiplayer@1.0.0:cc\ncheaper-recruits@1.0.0:ee\nbig-maps@2.0.0:zz", 0, names, t, sizeof t, &c);
    printf("%s\n---\n", t);
    CHECK(n == 4 && c.missing == 1 && c.extra == 1 && c.version == 1 && c.files == 1);
    CHECK(strstr(t, "Missing (the host uses them): Hell difficulty 1.0.0"));
    CHECK(strstr(t, "Extra (the host does not use them): Cheaper recruits 1.0.0"));
    CHECK(strstr(t, "Other version: multiplayer (host 1.1.0, you 1.0.0)"));
    CHECK(strstr(t, "different files (reinstall): big-maps 2.0.0"));
    /* vanilla: every gameplay mod of the guest is named, host lines ignored */
    n = dsmc_guest_text("ignored@1:x", "hell-difficulty@1.0.0:aa\ncheaper-recruits@1.0.0", 1, names, t, sizeof t, &c);
    printf("%s\n---\n", t);
    CHECK(n == 2 && c.extra == 2 && strstr(t, "allows no gameplay mods") && strstr(t, "Hell difficulty 1.0.0, Cheaper recruits 1.0.0"));
    CHECK(dsmc_guest_text("", "", 1, NULL, t, sizeof t, &c) == 0);
    /* host side */
    n = dsmc_host_text("The owner", "hell-difficulty@1.0.0:aa", "cheaper-recruits@1.0.0", 0, names, t, sizeof t);
    printf("%s\n---\n", t);
    CHECK(n == 2 && !strcmp(t, "The owner cannot join: missing Hell difficulty 1.0.0; extra Cheaper recruits 1.0.0"));
    n = dsmc_host_text("The owner", "a@2", "a@1", 0, NULL, t, sizeof t);
    CHECK(n == 1 && !strcmp(t, "The owner cannot join: other version a (host 2, player 1)"));
    /* tiny buffer: no overflow, terminated */
    char s[16]; dsmc_guest_text("a@1", "", 0, NULL, s, sizeof s, NULL); CHECK(strlen(s) == 15);
    /* no names table -> ids */
    dsmc_guest_text("hell-difficulty@1.0.0", "", 0, NULL, t, sizeof t, NULL); CHECK(strstr(t, "hell-difficulty 1.0.0"));
    /* mod settings: same mods, one gameplay setting differs -> named with both values, no mod named as missing / extra */
    n = dsmc_guest_text("zetu@1.0.0:aa\nzetu/gold_mult@2\nzetu/hard@on", "zetu/hard@on\nzetu/gold_mult@3\nzetu@1.0.0:aa",
                        0, "zetu\tZET-U\n", t, sizeof t, &c);
    printf("%s\n---\n", t);
    CHECK(n == 1 && c.settings == 1 && c.missing == 0 && c.extra == 0 && c.version == 0 && c.files == 0);
    CHECK(strstr(t, "ZET-U: gold_mult (host 2, you 3)") && strstr(t, "mod settings") && !strstr(t, "Missing") && !strstr(t, "Extra"));
    /* the mod in another version: only the version line, its settings are not listed again */
    n = dsmc_guest_text("zetu@1.1.0:aa\nzetu/gold_mult@2", "zetu@1.0.0:aa\nzetu/gold_mult@3", 0, NULL, t, sizeof t, &c);
    CHECK(n == 1 && c.version == 1 && c.settings == 0 && !strstr(t, "gold_mult"));
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
