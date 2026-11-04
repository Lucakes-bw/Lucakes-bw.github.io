# 《数据结构 → 算法设计》一章的示例程序
#   make          编译全部，程序放在 build/ 中
#   make run      编译并依次运行（会输出文章中的所有数据，N 皇后和 LIS 要运行几十秒）
CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Wextra
PROGS = backtracking dp_basics knapsack sequence greedy two_pointers kmp

all: $(addprefix build/,$(PROGS))

build/%: %.c common.h
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@

run: all
	@for p in $(PROGS); do echo "######## $$p"; ./build/$$p; done

clean:
	rm -rf build

.PHONY: all run clean
