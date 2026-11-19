// 《中介者模式》练习 4（修复版）：① syncing_ 守卫防止重入冲掉状态；② ~Track() 在析构时向中介者注销
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Track;
struct MixerMediator {
  std::vector<Track*> tracks;
  Track* soloTrack = nullptr;
  bool syncing_ = false;              // 修复 ①：正在由中介者批量同步时，忽略由此触发的二次通知

  void add(Track* t) { tracks.push_back(t); }
  void remove(Track* t);
  void onSoloChanged(Track* sender);
  void renderFrame();
};

struct Track {
  std::string name;
  MixerMediator& mixer;
  bool solo = false;
  bool muted = false;

  Track(std::string n, MixerMediator& m) : name(std::move(n)), mixer(m) {
    mixer.add(this);
  }
  ~Track() {                          // 修复 ②：析构时把自己从 mixer 里注销
    mixer.remove(this);
  }
  Track(const Track&) = delete;
  Track& operator=(const Track&) = delete;

  void setSolo(bool s) {
    if (solo == s) return;
    solo = s;
    mixer.onSoloChanged(this);
  }
};

void MixerMediator::remove(Track* t) {
  tracks.erase(std::remove(tracks.begin(), tracks.end(), t), tracks.end());
  if (soloTrack == t) {
    soloTrack = nullptr;
    for (Track* other : tracks) other->muted = false;
  }
}

void MixerMediator::onSoloChanged(Track* sender) {
  if (syncing_) return;
  syncing_ = true;
  if (sender->solo) {
    soloTrack = sender;
    for (Track* t : tracks) {
      if (t != sender) {
        t->setSolo(false);
        t->muted = true;
      } else {
        t->muted = false;
      }
    }
  } else {
    soloTrack = nullptr;
    for (Track* t : tracks) t->muted = false;
  }
  syncing_ = false;
}

void MixerMediator::renderFrame() {
  std::printf("当前混音输出：");
  for (Track* t : tracks)
    if (!t->muted) std::printf("[%s] ", t->name.c_str());
  std::printf("(独奏=%s)\n", soloTrack ? soloTrack->name.c_str() : "无");
}

int main() {
  MixerMediator mixer;
  Track bgm("背景音乐", mixer);
  Track voice("人声旁白", mixer);
  {
    auto preview = std::make_unique<Track>("试听音效", mixer);
    voice.setSolo(true);              // 只有「人声旁白」发声，背景音乐和试听音效都静音
    mixer.renderFrame();
    preview->setSolo(true);           // 切到「试听音效」独奏
    mixer.renderFrame();
  }                                   // preview 析构并自动注销，独奏解除，剩下两条轨道恢复发声
  mixer.renderFrame();
}
