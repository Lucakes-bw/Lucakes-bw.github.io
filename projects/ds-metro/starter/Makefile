CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -g
SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h)

# make        编译出 build/metro
# make run    编译并用示例数据运行
# make test   运行当前里程碑的测试（make test M=m2 指定里程碑）
M ?= m1

build/metro: $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@

run: build/metro
	./build/metro data/metro.txt

test:
	@tests/run.sh $(M)

clean:
	rm -rf build

.PHONY: run test clean
