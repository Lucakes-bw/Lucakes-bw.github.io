CC ?= gcc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -pthread
BIN = build/kvd build/cli build/hb

all: $(BIN)

build/%: %.c net.c net.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< net.c

clean:
	rm -rf build

.PHONY: all clean
