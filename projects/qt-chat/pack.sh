#!/bin/sh
# 把服务器和客户端的源码打包成 source/downloads/qt-chat.zip
set -e
cd "$(dirname "$0")"
OUT=../../source/downloads/qt-chat.zip
rm -f "$OUT"
zip -qr "$OUT" README.md server client -x "*/build/*" "*.DS_Store"
unzip -l "$OUT" | tail -1
