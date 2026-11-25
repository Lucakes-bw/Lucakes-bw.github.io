// 《RAII 与 Scope Guard》练习 2（有 bug）：手写 RAII 类只写了析构函数，未禁用拷贝导致 double-free
#include <cstdio>

struct FrameBuf {
  int id;
  bool alive;
};

static int g_doubleFree = 0;

FrameBuf* alloc_frame(int id) {
  return new FrameBuf{id, true};
}

void free_frame_checked(FrameBuf* p) {
  if (!p) return;
  if (!p->alive) {
    ++g_doubleFree;
    std::printf("  [BUG] double-free FrameBuf #%d!\n", p->id);
    return;
  }
  p->alive = false;
  std::printf("  [-] free FrameBuf #%d\n", p->id);
}

class BadFrameHandle {
 public:
  explicit BadFrameHandle(FrameBuf* p) : ptr_(p) {}
  ~BadFrameHandle() { free_frame_checked(ptr_); }
  // 坑：未声明 = delete 的拷贝构造/赋值，编译器自动生成浅拷贝！
 private:
  FrameBuf* ptr_;
};

int main() {
  FrameBuf* raw = alloc_frame(1);
  {
    BadFrameHandle a(raw);
    BadFrameHandle b = a;   // 浅拷贝：a.ptr_ 与 b.ptr_ 指向同一块资源
  }                         // b 先析构释放 raw，a 再析构触发 double-free！
  std::printf("double-free 次数=%d\n", g_doubleFree);
  delete raw;
}
