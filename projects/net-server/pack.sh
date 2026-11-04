#!/usr/bin/env bash
# 把 tinynet 打包成 source/downloads/tinynet.zip，供博客文章下载。
# 修改了 tinynet/ 中的任何文件后都要重新运行：projects/net-server/pack.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../../source/downloads/tinynet.zip"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

rsync -a --exclude build --exclude .DS_Store --exclude '* [0-9]*' "$HERE/tinynet/" "$TMP/tinynet/"
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$TMP" && zip -qrX "$OUT" tinynet)
echo "已生成 $(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
