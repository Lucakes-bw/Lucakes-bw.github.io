// 《责任链模式》：加「播放列表」——写一个新的处理者，再把它接进链里
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
    if (open) switch (k) {
      case Key::Up: --sel; return "播放列表：上移";
      case Key::Down: ++sel; return "播放列表：下移";
      case Key::Enter: open = false; return "播放列表：播放选中项";
      case Key::Esc: open = false; return "播放列表：关闭";
      default: break;
    }
    return KeyHandler::handle(k);
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

// ---- 以下是测试：各层开关的每种组合 × 7 个键，看是哪一层处理的，和规格逐格对照
const char* kKeys[] = {"空格", "Esc", "↑", "↓", "←", "→", "回车"};

std::string spec(bool dlg, bool menu, bool list, Key k) {   // 规格：从上往下，第一个开着、又关心这个键的层处理
  bool nav = k == Key::Up || k == Key::Down || k == Key::Enter || k == Key::Esc;
  if (dlg) return "对话框";
  if (menu && nav) return "字幕菜单";
  if (list && nav) return "播放列表";
  return k == Key::Enter ? "没人处理" : "播放器";
}

int main() {
  int cells = 0, bad = 0;
  for (int c = 0; c < 8; ++c)
    for (int k = 0; k < 7; ++k) {
      Dialog dialog; SubtitleMenu menu; Playlist list; PlayerKeys player;
      dialog.setNext(&menu)->setNext(&list)->setNext(&player);   // 播放列表插在字幕菜单和播放器之间
      dialog.open = c & 1; menu.open = c & 2; list.open = c & 4;
      std::string r = dialog.handle(Key(k));
      std::string who = r.substr(0, r.find("："));
      std::string want = spec(c & 1, c & 2, c & 4, Key(k));
      ++cells;
      if (who != want) { ++bad; std::printf("  ✗ 组合 %d 按 %s：%s，规格是 %s\n", c, kKeys[k], r.c_str(), want.c_str()); }
    }
  std::printf("%d 格里 %d 格和规格不一致\n", cells, bad);
}
