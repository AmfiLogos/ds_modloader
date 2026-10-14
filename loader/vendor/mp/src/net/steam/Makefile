CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11 -D_DARWIN_C_SOURCE -arch arm64
CORE     = ../dsnet_link.c ../dsnet_server.c ../dsnet_client.c
OBJ      = $(CORE:../%.c=build/%.o) build/dsnet_steam.o
HDR      = ../dsnet.h ../dsnet_internal.h dsnet_steam.h

all: build/libdsnet_steam.a build/steam_check

build/%.o: ../%.c $(HDR)
	@mkdir -p build
	nice -n 19 $(CC) $(CFLAGS) -c $< -o $@

build/dsnet_steam.o: dsnet_steam.c $(HDR)
	@mkdir -p build
	nice -n 19 $(CC) $(CFLAGS) -c $< -o $@

# Steam part only; link it next to ../build/libdsnet.a in the mod dylib.
build/libdsnet_steam.a: build/dsnet_steam.o
	ar rcs $@ $^

build/steam_check: steam_check.c $(OBJ)
	nice -n 19 $(CC) $(CFLAGS) $< $(OBJ) -lpthread -o $@

check: build/steam_check
	nice -n 19 ./build/steam_check

clean:
	rm -rf build

.PHONY: all check clean
