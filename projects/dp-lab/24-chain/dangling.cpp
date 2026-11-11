// 《责任链模式》：链里存的是裸指针——播放列表关闭时对象删掉了，链还指着它
#include <cstdio>
#include <memory>
#include <string>

class KeyHandler {
 public:
  virtual ~KeyHandler() = default;
  KeyHandler* setNext(KeyHandler* next) { next_ = next; return next; }
  virtual std::string handle(char k) { return next_ ? next_->handle(k) : "没人处理"; }
 private:
  KeyHandler* next_ = nullptr;
};
struct Menu : KeyHandler {};                // 字幕菜单（关着，什么都交给下一个）
struct Playlist : KeyHandler {
  std::string title = "Big Buck Bunny";
  std::string handle(char k) override { return k == 'n' ? "播放列表：下一首 " + title : KeyHandler::handle(k); }
};
struct Player : KeyHandler {
  std::string handle(char k) override { return k == ' ' ? "播放器：暂停" : KeyHandler::handle(k); }
};

int main() {
  Menu menu; Player player;
  auto list = std::make_unique<Playlist>();       // 打开播放列表：new 一个，接进链
  menu.setNext(list.get())->setNext(&player);
  std::printf("%s\n", menu.handle('n').c_str());
  list.reset();                                    // 关闭播放列表：删掉了对象，却忘了把 menu 重新接到 player
  std::printf("%s\n", menu.handle(' ').c_str());  // menu 还指着已经释放的 Playlist
}
