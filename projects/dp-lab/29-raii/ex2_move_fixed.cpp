// 《RAII 与 Scope Guard》练习 2（修复版）：遵循 Rule of 5，禁用拷贝并实现 noexcept 移动构造与移动赋值
#include <cstdio>
#include <utility>

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

class GoodFrameHandle {
 public:
  explicit GoodFrameHandle(FrameBuf* p = nullptr) noexcept : ptr_(p) {}
  ~GoodFrameHandle() { free_frame_checked(ptr_); }

  GoodFrameHandle(const GoodFrameHandle&) = delete;
  GoodFrameHandle& operator=(const GoodFrameHandle&) = delete;

  GoodFrameHandle(GoodFrameHandle&& o) noexcept
      : ptr_(std::exchange(o.ptr_, nullptr)) {}

  GoodFrameHandle& operator=(GoodFrameHandle&& o) noexcept {
    if (this != &o) {
      free_frame_checked(ptr_);
      ptr_ = std::exchange(o.ptr_, nullptr);
    }
    return *this;
  }

 private:
  FrameBuf* ptr_;
};

int main() {
  FrameBuf* raw1 = alloc_frame(1);
  FrameBuf* raw2 = alloc_frame(2);
  {
    GoodFrameHandle a(raw1);
    GoodFrameHandle b = std::move(a);  // 所有权转移，a.ptr_ 置空
    GoodFrameHandle c(raw2);
    b = std::move(c);                  // 先释放 raw1，再接管 raw2
  }                                    // 释放 raw2；a、c 均为空，无 double-free
  std::printf("double-free 次数=%d\n", g_doubleFree);
  delete raw1;
  delete raw2;
}
