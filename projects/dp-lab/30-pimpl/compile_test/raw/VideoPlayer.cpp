#include "VideoPlayer.h"

VideoPlayer::VideoPlayer(std::string url) {
  net_.url = std::move(url);
}
VideoPlayer::~VideoPlayer() = default;
void VideoPlayer::play() { playing_ = true; }
void VideoPlayer::seek(int ms) { positionMs_ = ms < 0 ? 0 : ms; }
int VideoPlayer::positionMs() const { return positionMs_; }
std::string VideoPlayer::url() const { return net_.url; }
