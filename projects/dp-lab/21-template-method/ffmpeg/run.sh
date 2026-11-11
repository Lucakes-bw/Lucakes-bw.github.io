#!/bin/sh
# 《模板方法模式》：把 bbb-10s.mp4 导出成 MP4、GIF、WAV（输出在 build/ 下），再用 ffprobe 核对结果
set -e
cd "$(dirname "$0")"
VIDEO=${1:-../../../../source/media/bbb-10s.mp4}
./build/export "$VIDEO" build
if command -v ffprobe >/dev/null; then
  for f in build/out.mp4 build/out.gif build/out.wav; do
    printf '%s: ' "$f"
    ffprobe -v error -show_entries stream=codec_name,width,height,sample_rate,nb_frames:format=duration -of compact=p=0:nk=1 "$f" | tr '\n' ' '
    echo
  done
fi
