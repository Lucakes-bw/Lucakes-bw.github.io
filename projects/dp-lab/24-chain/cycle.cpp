// 《责任链模式》：链接成了环，没人处理的请求会一直转下去；接链时检查一下就能拦住
#include <cstdio>
#include <cstring>

struct Handler {
  const char* name;
  Handler* next = nullptr;
  bool handle(int key) {
    if (key == 'q') { std::printf("%s 处理了 q\n", name); return true; }
    return next ? next->handle(key) : false;
  }
  bool link(Handler* n) {                   // 接链前沿着 n 往后走一遍，走回自己就说明会成环
    for (Handler* h = n; h; h = h->next)
      if (h == this) { std::printf("拒绝：%s → %s 会让链成环\n", name, n->name); return false; }
    next = n;
    return true;
  }
};

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);   // 不缓冲：程序崩溃前的输出也能看到
  bool check = !(argc > 1 && std::strcmp(argv[1], "nocheck") == 0);
  Handler menu{"字幕菜单"}, list{"播放列表"}, player{"播放器"};
  menu.link(&list);
  list.link(&player);
  if (check) player.link(&menu);            // 手误：本想接「截图层」，接成了 menu
  else player.next = &menu;                 // 不检查，直接接上
  std::printf("按 q：%s\n", menu.handle('q') ? "有人处理" : "没人处理");
  std::printf("按 x：%s\n", menu.handle('x') ? "有人处理" : "没人处理");   // 没人处理 x：成环时在环里一直转
}
