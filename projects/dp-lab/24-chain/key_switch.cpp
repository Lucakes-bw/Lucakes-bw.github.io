// 《责任链模式》：问题版——一个 onKey 按键分支，每个分支里再判断对话框、字幕菜单开没开
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

struct Player {
  bool dialogOpen = false;   // 模态对话框：「是否从上次的位置继续播放？」
  bool menuOpen = false;     // 字幕菜单
  bool playing = false, fullscreen = true;
  int volume = 50, menuSel = 0;
  double pos = 0;

  std::string onKey(Key k) {
    if (dialogOpen) {                       // 模态：对话框开着时，别的层都不响应
      if (k == Key::Enter) { dialogOpen = false; return "对话框：继续播放"; }
      if (k == Key::Esc) { dialogOpen = false; return "对话框：从头播放"; }
      return "对话框：忽略";
    }
    switch (k) {
      case Key::Space:
        playing = !playing;
        return playing ? "播放器：播放" : "播放器：暂停";
      case Key::Esc:
        if (menuOpen) { menuOpen = false; return "字幕菜单：关闭"; }
        fullscreen = false;
        return "播放器：退出全屏";
      case Key::Up:
        if (menuOpen) { --menuSel; return "字幕菜单：上移"; }
        volume += 5;
        return "播放器：音量 +5";
      case Key::Down:
        if (menuOpen) { ++menuSel; return "字幕菜单：下移"; }
        volume -= 5;
        return "播放器：音量 -5";
      case Key::Left: pos -= 5; return "播放器：快退 5 秒";
      case Key::Right: pos += 5; return "播放器：快进 5 秒";
      case Key::Enter:
        if (menuOpen) { menuOpen = false; return "字幕菜单：选中"; }
        return "没人处理";
    }
    return "没人处理";
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
  for (int c = 0; c < 4; ++c)
    for (int k = 0; k < 7; ++k) {
      Player p;
      p.dialogOpen = c & 1; p.menuOpen = c & 2;
      std::string r = p.onKey(Key(k));
      std::string who = r.substr(0, r.find("："));
      std::string want = spec(c & 1, c & 2, false, Key(k));
      ++cells;
      if (who != want) { ++bad; std::printf("  ✗ 对话框%s 菜单%s 按 %s：%s，规格是 %s\n", c & 1 ? "开" : "关", c & 2 ? "开" : "关", kKeys[k], r.c_str(), want.c_str()); }
    }
  std::printf("%d 格里 %d 格和规格不一致\n", cells, bad);
}
