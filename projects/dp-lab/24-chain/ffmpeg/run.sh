#!/bin/sh
# 《责任链模式》：先走完整的回退链，再只用软解，各跑一次
set -e
cd "$(dirname "$0")"
VIDEO=${1:-../../../../source/media/bbb-10s.mp4}
./build/decoder_chain "$VIDEO"
./build/decoder_chain "$VIDEO" sw
