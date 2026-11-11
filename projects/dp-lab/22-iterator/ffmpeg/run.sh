#!/bin/sh
# 《迭代器模式》：遍历 bbb-10s.mp4 的包，再用 ffprobe 核对每个流的包数
set -e
cd "$(dirname "$0")"
VIDEO=${1:-../../../../source/media/bbb-10s.mp4}
./build/packets "$VIDEO"
if command -v ffprobe >/dev/null; then
  echo "ffprobe 核对："
  ffprobe -v error -count_packets -show_entries stream=index,codec_type,nb_read_packets -of compact=p=0 "$VIDEO"
fi
