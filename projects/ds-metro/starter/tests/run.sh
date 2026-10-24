#!/usr/bin/env bash
# 地铁换乘查询项目的测试脚本
#
# 用法：tests/run.sh [-v] <里程碑> [源代码目录]
#   tests/run.sh m1           测试 src/ 中的代码，运行里程碑 1 的测试
#   tests/run.sh m3           运行里程碑 1～3 的测试（后面的里程碑包含前面的，防止改坏已经做好的功能）
#   tests/run.sh -v m1        失败时显示完整的差异，而不是只显示前 20 行
#
# 代码会用 -fsanitize=address,undefined 编译，越界、使用已释放的内存、内存泄漏（Linux）都会被当作失败。

set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TIMEOUT=10

verbose=0
if [ "${1:-}" = "-v" ]; then verbose=1; shift; fi
target=${1:-}
case "$target" in
    m[0-9]|m[0-9][0-9]) ;;
    *) echo "用法：tests/run.sh [-v] <里程碑> [源代码目录]，例如 tests/run.sh m1"; exit 2 ;;
esac
last=${target#m}
if [ ! -d "$ROOT/tests/$target" ]; then echo "没有里程碑 $last 的测试：tests/$target 不存在"; exit 2; fi
src=${2:-$ROOT/src}
if [ ! -d "$src" ]; then echo "源代码目录不存在：$src"; exit 2; fi
src=$(cd "$src" && pwd)

if [ -t 1 ]; then GREEN=$'\033[32m'; RED=$'\033[31m'; YELLOW=$'\033[33m'; BOLD=$'\033[1m'; RESET=$'\033[0m'
else GREEN=; RED=; YELLOW=; BOLD=; RESET=; fi

BUILD="$ROOT/build/test"
mkdir -p "$BUILD"
CC=${CC:-cc}
CFLAGS="-std=c11 -Wall -Wextra -g -O1 -fno-omit-frame-pointer"
SAN="-fsanitize=address,undefined"
if ! echo 'int main(void){return 0;}' | "$CC" $SAN -x c - -o "$BUILD/san-check" 2>/dev/null; then
    echo "${YELLOW}注意：编译器不支持 -fsanitize，内存错误将无法检查${RESET}"
    SAN=""
fi
if [ "$(uname)" = "Linux" ]; then export ASAN_OPTIONS=detect_leaks=1; fi   # macOS 不支持泄漏检查
export UBSAN_OPTIONS=print_stacktrace=1

# 除 main.c 以外的源文件，用来和单元测试链接
libsrc=()
for f in "$src"/*.c; do [ "$(basename "$f")" = "main.c" ] || libsrc+=("$f"); done

compile() {     # compile <输出文件> <源文件...>
    local out=$1; shift
    if ! "$CC" $CFLAGS $SAN -I"$src" "$@" -o "$out" 2> "$BUILD/compile.log"; then
        echo "${RED}${BOLD}编译失败：${RESET}"
        cat "$BUILD/compile.log"
        exit 2
    fi
    if [ -s "$BUILD/compile.log" ]; then
        echo "${YELLOW}编译有警告（不影响测试，但最好改掉）：${RESET}"
        cat "$BUILD/compile.log"
    fi
}

# 运行程序，超时就杀掉。结果保存在 rc 中
runWithTimeout() {  # runWithTimeout <输入文件> <输出文件> <错误文件> <命令...>
    local in=$1 out=$2 err=$3; shift 3
    (cd "$ROOT" && exec "$@") < "$in" > "$out" 2> "$err" &
    local pid=$!
    ( sleep "$TIMEOUT"; kill -9 "$pid" ) 2>/dev/null &
    local watcher=$!
    wait "$pid" 2>/dev/null; rc=$?
    { kill "$watcher"; wait "$watcher"; } 2>/dev/null
}

showDiff() {    # showDiff <期望> <实际>
    if [ $verbose = 1 ]; then diff -u -L 期望 -L 实际 "$1" "$2" | sed 's/^/      /'
    else diff -u -L 期望 -L 实际 "$1" "$2" | head -20 | sed 's/^/      /'; fi
}

isSanitizerError() { grep -qE 'ERROR: (Address|Leak)Sanitizer|runtime error:' "$1"; }

passed=0
total=0
report() {      # report <是否通过> <名字>
    total=$((total + 1))
    if [ "$1" = 1 ]; then passed=$((passed + 1)); echo "${GREEN}[通过]${RESET} $2"
    else echo "${RED}[失败]${RESET} $2"; fi
}

compile "$BUILD/metro" "$src"/*.c

for k in $(seq 1 "$last"); do
    dir="$ROOT/tests/m$k"
    [ -d "$dir" ] || continue
    echo "${BOLD}== 里程碑 $k ==${RESET}"

    # 单元测试：tests/mK/*_test.c，每行输出 [通过] 或 [失败]
    for t in "$dir"/*_test.c; do
        [ -e "$t" ] || continue
        exe="$BUILD/$(basename "$t" .c)"
        compile "$exe" "$t" "${libsrc[@]}"
        runWithTimeout /dev/null "$BUILD/unit.out" "$BUILD/unit.err" "$exe"
        while IFS= read -r line; do
            case "$line" in
                "[通过] "*) report 1 "${line#\[通过\] }" ;;
                "[失败] "*) report 0 "${line#\[失败\] }" ;;
                HASH_TEST*|*_TEST" "*) ;;
                *) echo "$line" ;;
            esac
        done < "$BUILD/unit.out"
        if [ $rc -ne 0 ] && ! grep -q '^\[失败\]' "$BUILD/unit.out"; then
            report 0 "$(basename "$t")：程序异常退出（退出码 $rc）"
            sed 's/^/      /' "$BUILD/unit.err" | head -30
        elif isSanitizerError "$BUILD/unit.err"; then
            report 0 "$(basename "$t")：内存错误"
            sed 's/^/      /' "$BUILD/unit.err" | head -30
        fi
    done

    # 命令测试：tests/mK/cases/NN-名字.in 是输入，第一行 "# 描述"
    #   .out 期望的标准输出   .err 期望的标准错误（没有这个文件表示应该为空）
    #   .args 数据文件（默认 data/metro.txt）   .code 期望的退出码（默认 0）
    for input in "$dir"/cases/*.in; do
        [ -e "$input" ] || continue
        base=${input%.in}
        name=$(basename "$base")
        desc=$(head -n 1 "$input" | sed 's/^# *//')
        data=data/metro.txt; [ -f "$base.args" ] && data=$(cat "$base.args")
        want=0; [ -f "$base.code" ] && want=$(cat "$base.code")
        [ -f "$base.out" ] && expOut="$base.out" || expOut=/dev/null
        [ -f "$base.err" ] && expErr="$base.err" || expErr=/dev/null

        runWithTimeout "$input" "$BUILD/out" "$BUILD/err" "$BUILD/metro" "$data"
        title="$name  $desc"
        if isSanitizerError "$BUILD/err"; then
            report 0 "$title"
            echo "      内存错误（输入 tests/m$k/cases/$name.in）："
            sed 's/^/      /' "$BUILD/err" | head -30
        elif [ $rc -eq 137 ]; then
            report 0 "$title"
            echo "      超过 $TIMEOUT 秒没有结束，是不是死循环了？"
        elif [ $rc -ne "$want" ]; then
            report 0 "$title"
            echo "      退出码应该是 $want，实际是 $rc$([ $rc -gt 128 ] && echo "（程序崩溃了）")"
            [ -s "$BUILD/err" ] && sed 's/^/      /' "$BUILD/err" | head -10
        elif ! cmp -s "$expOut" "$BUILD/out"; then
            report 0 "$title"
            echo "      标准输出和期望的不同（- 是期望的，+ 是实际的）："
            showDiff "$expOut" "$BUILD/out"
        elif ! cmp -s "$expErr" "$BUILD/err"; then
            report 0 "$title"
            echo "      错误信息和期望的不同（- 是期望的，+ 是实际的）："
            showDiff "$expErr" "$BUILD/err"
        else
            report 1 "$title"
        fi
    done
done

echo
if [ $passed = $total ]; then
    echo "${GREEN}${BOLD}全部通过：$passed/$total，里程碑 $last 完成！${RESET}"
    exit 0
else
    echo "${RED}${BOLD}通过 $passed/$total${RESET}"
    exit 1
fi
