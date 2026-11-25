// 模拟播放器内部依赖的重型第三方头文件（解码器、GPU 渲染管线、网络流栈）
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct DecoderContext {
  std::string codec = "H.265/HEVC";
  std::deque<std::vector<unsigned char>> frameQueue;
  std::unordered_map<std::string, std::string> avOptions;
};

struct RenderPipeline {
  std::string backend = "Metal";
  std::map<int, std::string> shaderCache;
  int width = 1920;
  int height = 1080;
};

struct NetworkSession {
  std::string url;
  std::regex urlPattern{"^(rtsp|https?)://.*"};
  std::chrono::milliseconds timeout{3000};
  std::atomic<bool> connected{false};
};
