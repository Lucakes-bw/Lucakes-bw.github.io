# 在装有 libhiredis-dev（和 libmysqlclient-dev）的 Linux 上编译
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
PROGS   = atomic vsmysql pipeline block memory evict expire persist repl cache

all: $(addprefix build/,$(PROGS))

build/%: %.c common.h | build
	$(CC) $(CFLAGS) -o $@ $< -lhiredis -lpthread -lm $(if $(filter vsmysql cache,$*),-lmysqlclient)

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all clean
