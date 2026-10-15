// trn_test.c - unit tests for trn_logic.c (floor seed). make -C trn_test test
#define TRN_NO_GAME
#include "../trn.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    /* seed: pure function of its inputs, positive, spread over attempts and floors */
    int32_t a = trn_floor_seed(123456, 1, 2, 777, 0);
    CHECK(a == trn_floor_seed(123456, 1, 2, 777, 0));
    CHECK(a > 0);
    CHECK(a != trn_floor_seed(123456, 1, 2, 777, 1));
    CHECK(a != trn_floor_seed(123456, 1, 3, 777, 0));
    CHECK(a != trn_floor_seed(123457, 1, 2, 777, 0));
    int pos = 1; for (int i = 0; i < 100000; i++) if (trn_floor_seed(i, i % 8, i % 30, i * 7, i % 5) <= 0) pos = 0;
    CHECK(pos);

    printf(fails ? "trn_test: %d FAILED\n" : "trn_test: all passed%.0d\n", fails);
    return fails != 0;
}
