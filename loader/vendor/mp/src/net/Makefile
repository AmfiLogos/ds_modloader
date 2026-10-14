CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11 -D_DARWIN_C_SOURCE
SRC      = dsnet_link.c dsnet_server.c dsnet_client.c
OBJ      = $(SRC:%.c=build/%.o)

all: build/libdsnet.a build/test_dsnet

build/%.o: %.c dsnet.h dsnet_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/libdsnet.a: $(OBJ)
	ar rcs $@ $^

build/test_dsnet: test/test_dsnet.c build/libdsnet.a
	$(CC) $(CFLAGS) $< build/libdsnet.a -lpthread -o $@

test: build/test_dsnet
	nice -n 19 ./build/test_dsnet

clean:
	rm -rf build

.PHONY: all test clean
