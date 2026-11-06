# 在 Linux 上编译（Ubuntu 24.04，GCC 13）
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
PROGS   = nscost pid1 escape touch1 cgcpu forkbomb pingpong minictr

all: $(addprefix build/,$(PROGS))

build/%: %.c common.h | build
	$(CC) $(CFLAGS) -o $@ $< -lpthread

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all clean
