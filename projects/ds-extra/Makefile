# 《数据结构》专栏中红黑树、跳表、Trie、LRU 缓存四篇文章的示例程序
#   make        编译全部，程序放在 build/ 中
#   make run    编译并依次运行，输出文章中的所有数据
#   make asan   用 AddressSanitizer 和 UBSan 编译运行
CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Wextra
PROGS = rbtree skiplist trie lru

all: $(addprefix build/,$(PROGS))

build/%: %.c common.h
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ -lm

run: all
	@for p in $(PROGS); do echo "######## $$p"; ./build/$$p; done

asan:
	@mkdir -p build
	@for p in $(PROGS); do $(CC) -std=c11 -g -fsanitize=address,undefined $$p.c -o build/$$p-asan -lm && ./build/$$p-asan > /dev/null && echo "$$p：ASan 通过"; done

clean:
	rm -rf build

.PHONY: all run asan clean
