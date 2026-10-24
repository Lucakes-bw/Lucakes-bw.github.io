#!/usr/bin/env bash
# 把起始代码打包成 source/downloads/ds-metro.zip，供博客文章下载。
# 修改了 starter/ 中的任何文件后都要重新运行：projects/ds-metro/pack.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../../source/downloads/ds-metro.zip"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cp -R "$HERE/starter" "$TMP/ds-metro"
rm -rf "$TMP/ds-metro/build"
find "$TMP/ds-metro" -name .DS_Store -delete
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$TMP" && zip -qrX "$OUT" ds-metro)
echo "已生成 $(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
