#!/usr/bin/env bash
# 把 wordfreq 打包成 source/downloads/wordfreq.zip，供博客文章下载。
# 修改了 wordfreq/ 中的任何文件后都要重新运行：projects/cpp-eng/pack.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../../source/downloads/wordfreq.zip"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

rsync -a --exclude build --exclude .DS_Store --exclude '* [0-9]*' "$HERE/wordfreq/" "$TMP/wordfreq/"
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$TMP" && zip -qrX "$OUT" wordfreq)
echo "已生成 $(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
