#!/bin/sh
# 《外观模式》：统计调用方的代价——代码行数、用到的 FFmpeg 函数、头文件、链接的库、预处理后的行数、编译时间
# 先运行 ./build.sh；编译时间重复 7 次取中位数
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
if [ "$(uname)" = Darwin ]; then FFCFLAGS="-I${FF:-/opt/homebrew/opt/ffmpeg}/include"
else FFCFLAGS=$(pkg-config --cflags libavformat libavcodec libswscale libavutil); fi

body() {  # 取出函数体（从函数头到第一个顶格的 }），去掉空行和注释行
  awk -v f="$2" 'index($0, f) == 1 {p = 1} p {print} p && /^}/ {exit}' "$1" | grep -v -E '^\s*(//.*)?$'
}
apis() { grep -o -E '\b(av|avformat|avcodec|sws)_[A-Za-z0-9_]+\(' | sort -u | wc -l | tr -d ' '; }
now() {  # 毫秒；GNU date 支持 %N，macOS 用 perl
  case $(date +%N) in *N) perl -MTime::HiRes=time -e 'printf "%.0f", time*1000' ;; *) echo $(($(date +%s%N) / 1000000)) ;; esac
}
median() { sort -n | sed -n 4p; }
ctime() {  # 编译成 .o 的时间（毫秒）
  for i in 1 2 3 4 5 6 7; do
    s=$(now)
    $CXX -std=c++17 -O2 $FFCFLAGS -c "$@" -o /tmp/measure_$$.o
    e=$(now)
    echo $((e - s))
  done | median
}

echo "== 调用方要写的代码"
echo "不用外观  save_thumbnail_raw() $(body raw_thumb.cpp 'int save_thumbnail_raw' | wc -l | tr -d ' ') 行，用到 $(body raw_thumb.cpp 'int save_thumbnail_raw' | apis) 个 FFmpeg 函数"
echo "用外观    main.cpp 里的调用     $(grep -c 'save_thumbnail(' main.cpp) 行，用到 $(apis < main.cpp) 个 FFmpeg 函数"
echo "外观本身  thumbnailer.cpp       $(grep -v -E '^\s*(//.*)?$' thumbnailer.cpp | wc -l | tr -d ' ') 行，用到 $(apis < thumbnailer.cpp) 个 FFmpeg 函数（只写一次）"
echo "== 编译依赖"
for f in raw_thumb.cpp main.cpp; do
  echo "$f：FFmpeg 头文件 $(grep -c -E '#include <lib(av|sw)' $f) 个，预处理后 $($CXX -std=c++17 $FFCFLAGS -E $f | wc -l | tr -d ' ') 行，编译 $(ctime $f) ms"
done
hdr() { printf "$1" | $CXX -x c++ -std=c++17 $FFCFLAGS -E - | wc -l | tr -d ' '; }
echo "  其中 FFmpeg 的 4 个头文件单独预处理 $(hdr '#include <libavcodec/avcodec.h>\n#include <libavformat/avformat.h>\n#include <libavutil/imgutils.h>\n#include <libswscale/swscale.h>\n') 行，<memory> <string> <vector> 单独预处理 $(hdr '#include <memory>\n#include <string>\n#include <vector>\n') 行"
echo "链接：build/raw_thumb 直接依赖的 FFmpeg 库 $(grep -o -- '-lav[a-z]*\|-lsw[a-z]*' build.sh | sort -u | wc -l | tr -d ' ') 个；build/thumb 只依赖 libthumbnailer"
if [ "$(uname)" = Darwin ]; then L="otool -L"; else L="readelf -d"; fi   # 只看直接依赖（ldd 会把间接依赖也列出来）
echo "  $L：build/raw_thumb 直接依赖 $($L build/raw_thumb | grep -c -E 'lib(av|sw)') 个 FFmpeg 库，build/thumb 直接依赖 $($L build/thumb | grep -c -E 'lib(av|sw)') 个"
rm -f /tmp/measure_$$.o
