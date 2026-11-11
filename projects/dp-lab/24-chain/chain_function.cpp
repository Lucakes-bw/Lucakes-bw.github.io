// 《责任链模式》：用 vector<std::function> 当链——面板打开时压进去、关上时弹出来，从最上面一层往下问
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };
using Handler = std::function<std::string(Key)>;   // 返回空串表示「不归我管」

class KeyRouter {
 public:
  void push(std::string name, Handler h) { layers_.push_back({std::move(name), std::move(h)}); }
  void remove(const std::string& name) {     // 只做标记：处理函数可能正在执行，这时不能把它从 vector 里删掉
    for (Layer& l : layers_) if (l.name == name) l.alive = false;
  }
  std::string dispatch(Key k) {
    std::string r = "没人处理";
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)   // 后打开的在上面，先问它
      if (it->alive) if (std::string s = it->handle(k); !s.empty()) { r = s; break; }
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [](const Layer& l) { return !l.alive; }), layers_.end());
    return r;
  }
 private:
  struct Layer { std::string name; Handler handle; bool alive = true; };
  std::vector<Layer> layers_;
};

int main() {
  KeyRouter router;
  int volume = 50, sel = 3;
  router.push("播放器", [&](Key k) -> std::string {
    if (k == Key::Up) { volume += 5; return "播放器：音量 " + std::to_string(volume); }
    if (k == Key::Space) return "播放器：暂停";
    return "";
  });
  std::printf("%s\n", router.dispatch(Key::Up).c_str());

  router.push("字幕菜单", [&](Key k) -> std::string {   // 打开字幕菜单：压一层
    if (k == Key::Up) return "字幕菜单：选中第 " + std::to_string(--sel) + " 项";
    if (k == Key::Esc) { router.remove("字幕菜单"); return "字幕菜单：关闭"; }
    return "";
  });
  std::printf("%s\n", router.dispatch(Key::Up).c_str());
  std::printf("%s\n", router.dispatch(Key::Space).c_str());   // 菜单不管空格，落到播放器
  std::printf("%s\n", router.dispatch(Key::Esc).c_str());     // 菜单在处理函数里把自己移除
  std::printf("%s\n", router.dispatch(Key::Up).c_str());      // 又回到调音量
  std::printf("%s\n", router.dispatch(Key::Enter).c_str());
}
