// 普通版本：公开头文件直接暴露所有私有成员，被迫 #include 重型内部依赖
#pragma once
#include "../heavy_deps.h"

class VideoPlayer {
 public:
  explicit VideoPlayer(std::string url);
  ~VideoPlayer();

  void play();
  void seek(int ms);
  int positionMs() const;
  std::string url() const;

 private:
  DecoderContext decoder_;
  RenderPipeline renderer_;
  NetworkSession net_;
  int positionMs_ = 0;
  bool playing_ = false;
#ifdef ADD_PRIVATE_FIELD
  int retryCount_ = 3; // 模拟迭代：新增一个私有成员
#endif
};
