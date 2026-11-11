// 《观察者模式》：推模型把数据随通知一起发出去；拉模型只说「哪方面变了」，观察者自己回来取需要的数据
#include <cstdio>
#include <vector>

// ---------- 推模型：通知里直接带上数据 ----------
class PushObserver {
public:
  virtual ~PushObserver() = default;
  virtual void onProgress(double pos, double dur) = 0;   // 想多推一个「已缓冲到哪」，这里的签名和所有实现都要改
};

// ---------- 拉模型：通知只说哪方面变了，数据由观察者向 Player 要 ----------
class Player;
enum class Changed { Progress, Buffer };
class PullObserver {
public:
  virtual ~PullObserver() = default;
  virtual void onChanged(const Player& p, Changed what) = 0;
};

class Player {
public:
  void attach(PushObserver* o) { push_.push_back(o); }
  void attach(PullObserver* o) { pull_.push_back(o); }
  double position() const { return pos_; }
  double duration() const { return dur_; }
  double buffered() const { return buffered_; }               // 拉模型：加数据只要加一个查询函数

  void tick(double pos) {
    pos_ = pos;
    for (auto* o : push_) o->onProgress(pos_, dur_);
    for (auto* o : pull_) o->onChanged(*this, Changed::Progress);
  }
  void onDownloaded(double until) {
    buffered_ = until;
    for (auto* o : pull_) o->onChanged(*this, Changed::Buffer);
  }

private:
  std::vector<PushObserver*> push_;
  std::vector<PullObserver*> pull_;
  double pos_ = 0, dur_ = 10.0, buffered_ = 0;
};

class PushBar : public PushObserver {
public:
  void onProgress(double pos, double dur) override { std::printf("  [推·进度条] %.1f / %.1f s\n", pos, dur); }
};

class PullBar : public PullObserver {                       // 进度条要两样：播放位置和缓冲位置
public:
  void onChanged(const Player& p, Changed) override {
    std::printf("  [拉·进度条] %.1f / %.1f s，缓冲到 %.1f s\n", p.position(), p.duration(), p.buffered());
  }
};

class PullSubtitle : public PullObserver {                  // 字幕只关心播放位置，缓冲变化直接忽略
public:
  void onChanged(const Player& p, Changed what) override {
    if (what == Changed::Progress) std::printf("  [拉·字幕]   找 %.1f s 的字幕\n", p.position());
  }
};

int main() {
  Player player;
  PushBar pushBar;
  PullBar pullBar;
  PullSubtitle pullSub;
  player.attach(&pushBar);
  player.attach(&pullBar);
  player.attach(&pullSub);
  player.onDownloaded(4.0);
  player.tick(2.0);
}
