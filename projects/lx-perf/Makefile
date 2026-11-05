# 在 Linux 上编译（Ubuntu 24.04，GCC 13）
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
PROGS   = load syscost sample cache offcpu trace

all: $(addprefix build/,$(PROGS)) build/sample-omit build/sample-nosib build/cache-branchy

# 第 3 篇：同一个程序的另外两种编译方式
# sample-omit：明确地不要帧指针（aarch64 上的 GCC 默认仍会保留帧记录，x86-64 上 -O2 默认不保留）
build/sample-omit: sample.c common.h | build
	$(CC) $(CFLAGS) -g -fomit-frame-pointer -o $@ $<

# sample-nosib：保留帧指针，并且不做尾调用优化
build/sample-nosib: sample.c common.h | build
	$(CC) $(CFLAGS) -g -fno-omit-frame-pointer -fno-optimize-sibling-calls -o $@ $<

build/%: %.c common.h | build
	$(CC) $(CFLAGS) -o $@ $< -lpthread

# 第 4 篇：不让 GCC 把 if 变成条件选择指令（csel），保留真正的分支
build/cache-branchy: cache.c common.h | build
	$(CC) $(CFLAGS) -fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize -o $@ $< -lpthread

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all clean
