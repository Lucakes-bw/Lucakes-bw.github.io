// 《责任链模式》练习 4：播放列表开着时，按空格不能暂停、按 → 不能快进，问题在哪
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

class KeyHandler {                          // 处理者：保存下一个处理者
 public:
  virtual ~KeyHandler() = default;
  KeyHandler* setNext(KeyHandler* next) { next_ = next; return next; }
  virtual std::string handle(Key k) {      // 默认实现：自己不处理，交给下一个
    return next_ ? next_->handle(k) : "没人处理";
  }
 private:
  KeyHandler* next_ = nullptr;
};

class Dialog : public KeyHandler {          // 模态对话框：开着时吞掉所有按键
 public:
  bool open = false;
  std::string handle(Key k) override {
    if (!open) return KeyHandler::handle(k);
    if (k == Key::Enter) { open = false; return "对话框：继续播放"; }
    if (k == Key::Esc) { open = false; return "对话框：从头播放"; }
    return "对话框：忽略";
  }
};

class SubtitleMenu : public KeyHandler {    // 字幕菜单：只管 ↑ ↓ 回车 Esc
 public:
  bool open = false;
  int sel = 0;
  std::string handle(Key k) override {
    if (open) switch (k) {
      case Key::Up: --sel; return "字幕菜单：上移";
      case Key::Down: ++sel; return "字幕菜单：下移";
      case Key::Enter: open = false; return "字幕菜单：选中";
      case Key::Esc: open = false; return "字幕菜单：关闭";
      default: break;
    }
    return KeyHandler::handle(k);          // 不归我管：交给下一个
  }
};

class Playlist : public KeyHandler {        // 播放列表（新增）：也只管 ↑ ↓ 回车 Esc
 public:
  bool open = false;
  int sel = 0;
  std::string handle(Key k) override {
    if (!open) return KeyHandler::handle(k);
    switch (k) {
      case Key::Up: --sel; return "播放列表：上移";
      case Key::Down: ++sel; return "播放列表：下移";
      case Key::Enter: open = false; return "播放列表：播放选中项";
      case Key::Esc: open = false; return "播放列表：关闭";
      default: return "";                   // 其他键不归播放列表管
    }
  }
};

class PlayerKeys : public KeyHandler {      // 播放器本身：链的最后一环
 public:
  bool playing = false, fullscreen = true;
  int volume = 50;
  double pos = 0;
  std::string handle(Key k) override {
    switch (k) {
      case Key::Space: playing = !playing; return playing ? "播放器：播放" : "播放器：暂停";
      case Key::Esc: fullscreen = false; return "播放器：退出全屏";
      case Key::Up: volume += 5; return "播放器：音量 +5";
      case Key::Down: volume -= 5; return "播放器：音量 -5";
      case Key::Left: pos -= 5; return "播放器：快退 5 秒";
      case Key::Right: pos += 5; return "播放器：快进 5 秒";
      default: return KeyHandler::handle(k);   // 回车：播放器也不管，交给下一个（没有了）
    }
  }
};


int main() {
  Dialog dialog; SubtitleMenu menu; Playlist list; PlayerKeys player;
  dialog.setNext(&menu)->setNext(&list)->setNext(&player);
  const char* names[] = {"空格", "Esc", "↑", "↓", "←", "→", "回车"};
  list.open = true;                         // 打开播放列表，边选边看
  for (Key k : {Key::Down, Key::Space, Key::Right, Key::Esc, Key::Space}) {
    std::string r = dialog.handle(k);
    std::printf("按 %s：%s\n", names[int(k)], r.empty() ? "（没有反应）" : r.c_str());
  }
}
