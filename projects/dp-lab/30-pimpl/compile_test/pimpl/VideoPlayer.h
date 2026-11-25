// Pimpl 版本：公开头文件只含 <memory> 与 <string>，编译防火墙彻底隔断重型依赖
#pragma once
#include <memory>
#include <string>

class VideoPlayer {
 public:
  explicit VideoPlayer(std::string url);
  ~VideoPlayer();

  VideoPlayer(VideoPlayer&&) noexcept;
  VideoPlayer& operator=(VideoPlayer&&) noexcept;

  void play();
  void seek(int ms);
  int positionMs() const;
  std::string url() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> pImpl_;
};
