# 在 Linux 上编译（Ubuntu 24.04，GCC 13）
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE

all: build/bench build/crash build/bloomtest

build/bench: bench.c lsm.c lsm.h | build
	$(CC) $(CFLAGS) -o $@ bench.c lsm.c

build/crash: crash.c lsm.c lsm.h | build
	$(CC) $(CFLAGS) -o $@ crash.c lsm.c

build/bloomtest: bloomtest.c | build
	$(CC) $(CFLAGS) -o $@ bloomtest.c -lm

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all clean
