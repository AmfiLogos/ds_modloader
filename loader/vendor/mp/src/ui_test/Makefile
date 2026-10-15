# ui_test: pure logic of the multiplayer UI (vote tally, claim rules, popup routing). make test
CC = clang
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I..
test_ui: test_ui.c ../ui_model.c ../ui_model.h
	nice -n 19 $(CC) $(CFLAGS) -o $@ test_ui.c ../ui_model.c
test: test_ui
	nice -n 19 ./test_ui
clean:
	rm -f test_ui
.PHONY: test clean
