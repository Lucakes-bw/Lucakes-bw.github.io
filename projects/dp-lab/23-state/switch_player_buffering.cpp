// 《状态模式》：到处判断 state_ 的播放器加上「缓冲中」（7 个状态），main 逐格对照规格表检查
#include <cstdio>
#include <string>

enum class State { Idle, Loading, Playing, Paused, Buffering, Ended, Error };
const char* kNames[] = {"空闲", "加载中", "播放", "暂停", "缓冲中", "结束", "出错"};

struct Devices { bool file = false, audio = false, spinner = false; };

class Player {
 public:
  Devices dev;            // 文件、音频设备、转圈图标：进出状态时要打开 / 关闭
  bool verbose = false;

  void open(const std::string& url) {
    if (state_ == State::Idle || state_ == State::Ended || state_ == State::Error) {
      url_ = url;
      dev.file = true;
      to(State::Loading);
    }
  }
  void onLoaded() {
    if (state_ != State::Loading) return;
    dev.audio = true;
    to(State::Playing);
  }
  void onFail() {
    if (state_ == State::Loading || state_ == State::Playing || state_ == State::Paused) {
      dev.audio = false;
      dev.file = false;
      to(State::Error);
    }
  }
  void play() {
    switch (state_) {
      case State::Idle: case State::Loading: case State::Playing: case State::Error:
        break;                                  // 这几个状态下按「播放」没有反应
      default:                                  // 其余的（暂停、结束）：开始播放
        if (state_ == State::Ended) seek0();
        dev.audio = true;
        to(State::Playing);
    }
  }
  void pause() {
    if (state_ == State::Playing) {
      dev.audio = false;
      to(State::Paused);
    } else if (state_ == State::Buffering) {
      dev.spinner = false;
      to(State::Paused);
    }
  }
  void stop() {
    switch (state_) {
      case State::Playing:
        dev.audio = false;
        [[fallthrough]];
      case State::Loading: case State::Paused: case State::Ended:
        dev.file = false;
        to(State::Idle);
        break;
      case State::Buffering:
        dev.spinner = false;
        dev.file = false;
        to(State::Idle);
        break;
      case State::Error:
        to(State::Idle);
        break;
      case State::Idle:
        break;
    }
  }
  void onEof() {
    if (state_ == State::Playing) {
      dev.audio = false;
      to(State::Ended);
    }
  }
  void onStall() {        // 缓冲区读空了
    if (state_ == State::Playing) {
      dev.audio = false;
      dev.spinner = true;
      to(State::Buffering);
    }
  }
  void onBuffered() {     // 缓冲区又攒够了
    if (state_ == State::Buffering) {
      dev.spinner = false;
      dev.audio = true;
      to(State::Playing);
    }
  }

  State state() const { return state_; }
  void force(State s) { state_ = s; }   // 测试用：直接换成某个状态，不执行进入动作

 private:
  void to(State next) {
    if (verbose) std::printf("  %s → %s\n", kNames[int(state_)], kNames[int(next)]);
    state_ = next;
  }
  void seek0() {}
  State state_ = State::Idle;
  std::string url_;
};

// ---- 以下是测试：7 个状态 × 9 个事件，逐格和规格表对照 ----
const char* kEvents[] = {"open", "loaded", "fail", "play", "pause", "stop", "eof", "stall", "buffered"};
void fire(Player& p, int e) {
  switch (e) {
    case 0: p.open("bbb-10s.mp4"); break;
    case 1: p.onLoaded(); break;
    case 2: p.onFail(); break;
    case 3: p.play(); break;
    case 4: p.pause(); break;
    case 5: p.stop(); break;
    case 6: p.onEof(); break;
    case 7: p.onStall(); break;
    case 8: p.onBuffered(); break;
  }
}

int main() {
  Player p;
  p.verbose = true;
  std::printf("演示：打开、播放、卡顿、缓冲中按「播放」\n");
  p.open("bbb-10s.mp4"); p.onLoaded(); p.onStall(); p.play();
  std::printf("  音频设备 %s，转圈图标 %s\n", p.dev.audio ? "开" : "关", p.dev.spinner ? "显示" : "隐藏");

  enum { I, L, P, Z, B, E, X, _ = -1 };   // 规格表：spec[状态][事件] = 下一个状态，_ 表示不变
  const int spec[7][9] = {
      // open loaded fail play pause stop eof stall buffered
      {L, _, _, _, _, _, _, _, _},   // 空闲
      {_, P, X, _, _, I, _, _, _},   // 加载中
      {_, _, X, _, Z, I, E, B, _},   // 播放
      {_, _, X, P, _, I, _, _, _},   // 暂停
      {_, _, X, _, Z, I, _, _, P},   // 缓冲中
      {L, _, _, P, _, I, _, _, _},   // 结束
      {L, _, _, _, _, I, _, _, _},   // 出错
  };
  int bad = 0;
  for (int s = 0; s < 7; ++s)
    for (int e = 0; e < 9; ++e) {
      Player q;
      q.force(State(s));
      q.dev = {s != I && s != X, s == P, s == B};
      fire(q, e);
      int want = spec[s][e] == _ ? s : spec[s][e], got = int(q.state());
      bool devOk = q.dev.audio == (got == P) && q.dev.spinner == (got == B) &&
                   q.dev.file == (got != I && got != X);
      if (got != want || !devOk) {
        ++bad;
        std::printf("  ✗ %s + %s：得到 %s，应该是 %s%s\n", kNames[s], kEvents[e], kNames[got],
                    kNames[want], devOk ? "" : "（设备状态也不对）");
      }
    }
  std::printf("7 个状态 × 9 个事件 = 63 格，和规格不一致的：%d 格\n", bad);
}
