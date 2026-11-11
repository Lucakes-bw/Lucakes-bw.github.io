// 《观察者模式》：重入——回调里又改了 Player 的状态，嵌套的通知先到，外层的旧通知后到，界面最后显示的状态是错的
#include <cstdio>
#include <deque>
#include <vector>

enum class PlayState { Stopped, Playing, Paused };
const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onStateChanged(PlayState s) = 0;
};

class Player {
public:
  explicit Player(bool queued) : queued_(queued) {}
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  PlayState state() const { return state_; }

  void setState(PlayState s) {
    state_ = s;
    if (!queued_) {                                // 直接通知：回调里再 setState 会嵌套通知
      for (auto* o : observers_) o->onStateChanged(s);
      return;
    }
    pending_.push_back(s);                         // 排队通知：正在通知时只入队，由最外层按顺序发
    if (notifying_) return;
    notifying_ = true;
    while (!pending_.empty()) {
      PlayState e = pending_.front();
      pending_.pop_front();
      for (auto* o : observers_) o->onStateChanged(e);
    }
    notifying_ = false;
  }

private:
  bool queued_;
  bool notifying_ = false;
  std::deque<PlayState> pending_;
  std::vector<PlayerObserver*> observers_;
  PlayState state_ = PlayState::Stopped;
};

class TrialGuard : public PlayerObserver {          // 试看结束：一开始播放就暂停，弹出开通会员
public:
  explicit TrialGuard(Player& p) : player_(p) {}
  void onStateChanged(PlayState s) override {
    std::printf("  [试看] 收到「%s」\n", name(s));
    if (s == PlayState::Playing) player_.setState(PlayState::Paused);   // 回调里改状态：重入
  }
private:
  Player& player_;
};

class PlayButton : public PlayerObserver {          // 界面上的播放 / 暂停按钮
public:
  void onStateChanged(PlayState s) override {
    shown_ = s;
    std::printf("  [按钮] 显示「%s」\n", name(s));
  }
  PlayState shown_ = PlayState::Stopped;
};

void run(bool queued) {
  std::printf("%s：\n", queued ? "排队通知" : "直接通知");
  Player player(queued);
  TrialGuard trial(player);
  PlayButton button;
  player.attach(&trial);
  player.attach(&button);
  player.setState(PlayState::Playing);
  std::printf("  结果：播放器是「%s」，按钮显示「%s」%s\n\n", name(player.state()), name(button.shown_),
              player.state() == button.shown_ ? "" : "  ← 不一致");
}

int main() {
  run(false);
  run(true);
}
