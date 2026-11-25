#include "VideoPlayer.h"

int syncSubtitleTrack(const VideoPlayer& p) {
  return p.positionMs() / 1000;
}
