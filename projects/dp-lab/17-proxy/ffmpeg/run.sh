#!/bin/sh
# 《代理模式》：分别以「全部解码」和「虚代理」两种方式运行（各自一个进程，峰值内存互不影响）
set -e
cd "$(dirname "$0")"
VIDEO=${1:-../../../../source/media/bbb-10s.mp4}
./build/lazy_strip "$VIDEO" eager
./build/lazy_strip "$VIDEO" lazy
./build/probe_cache "$VIDEO"
