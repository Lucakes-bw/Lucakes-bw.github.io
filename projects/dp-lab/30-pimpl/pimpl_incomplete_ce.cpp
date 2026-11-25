// 《Pimpl 惯用法》：故意编译失败演示——在 Impl 仍是不完整类型时省略或 = default 析构函数
// 预期编译报错：invalid application of 'sizeof' to an incomplete type 'BadPlayer::Impl'
#include <memory>

class BadPlayer {
 public:
  BadPlayer();
  // 坑：没声明析构函数（或在此处写 ~BadPlayer() = default;）
  // 编译器会在调用方看到 BadPlayer 销毁点时隐式生成 inline ~BadPlayer()，
  // 进而实例化 std::default_delete<Impl>::operator() -> static_assert(sizeof(Impl) > 0) 失败！
 private:
  struct Impl;
  std::unique_ptr<Impl> pImpl_;
};

int main() {
  BadPlayer p;                                     // 离开作用域触发 ~BadPlayer() -> 编译失败！
}
