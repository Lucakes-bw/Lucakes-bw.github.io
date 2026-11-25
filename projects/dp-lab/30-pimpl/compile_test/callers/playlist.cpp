#include "VideoPlayer.h"

int switchNextItem(VideoPlayer& p) {
  p.seek(0);
  p.play();
  return p.positionMs();
}
