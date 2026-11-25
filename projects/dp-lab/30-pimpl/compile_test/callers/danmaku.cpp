#include "VideoPlayer.h"

int fetchDanmakuBucket(const VideoPlayer& p) {
  return p.positionMs() / 5000;
}
