// 《中介者模式》练习 4（有 bug）：两个问题——① 中介者改同事时重入，把刚设好的状态冲掉；② 临时同事析构后留下悬空指针
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Track;
struct MixerMediator {
  std::vector<Track*> tracks;
  Track* soloTrack = nullptr;         // 当前独奏的轨道（最多一个）

  void add(Track* t) { tracks.push_back(t); }
  void onSoloChanged(Track* sender);
  void renderFrame();
};

struct Track {
  std::string name;
  MixerMediator& mixer;
  bool solo = false;
  bool muted = false;

  Track(std::string n, MixerMediator& m) : name(std::move(n)), mixer(m) {
    mixer.add(this);                  // Bug ②：构造时注册进 mixer.tracks，析构时没有移除
  }
  void setSolo(bool s) {
    solo = s;
    mixer.onSoloChanged(this);        // Bug ①：中介者把别的轨道设成 setSolo(false) 时，又会进来通知中介者
  }
};

void MixerMediator::onSoloChanged(Track* sender) {
  if (sender->solo) {
    soloTrack = sender;
    for (Track* t : tracks) {
      if (t != sender) {
        t->setSolo(false);            // 这里调 setSolo(false) 会再次进入 onSoloChanged，进到 else 分支把所有 muted 清掉！
        t->muted = true;
      } else {
        t->muted = false;
      }
    }
  } else {
    soloTrack = nullptr;
    for (Track* t : tracks) t->muted = false;
  }
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
    voice.setSolo(true);              // 期望：只有「人声旁白」发声，其余静音
    mixer.renderFrame();
  }                                   // preview 析构了，但 mixer.tracks[2] 还是悬空指针
  mixer.renderFrame();                // 读已释放的 preview->muted 和 preview->name
}
