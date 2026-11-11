// 《责任链模式》：中间件写法——每一环拿到 next，可以在调用 next 之前和之后各做一些事，也可以不调用
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

using Next = std::function<std::string(int)>;                    // 「交给后面」
using Middleware = std::function<std::string(int, const Next&)>;

// 把中间件一层套一层地包起来：mws[0] 在最外层，final 在最里面
Next build(const std::vector<Middleware>& mws, Next final) {
  for (auto it = mws.rbegin(); it != mws.rend(); ++it)
    final = [mw = *it, next = std::move(final)](int frame) { return mw(frame, next); };
  return final;
}

int main() {
  int decoded = 0;
  std::map<int, std::string> cache;
  std::vector<Middleware> mws = {
    [](int f, const Next& next) {                                // 1. 日志：前后各打一行
      std::printf("→ 日志：请求第 %d 帧\n", f);
      std::string r = next(f);
      std::printf("← 日志：第 %d 帧返回 %s\n", f, r.c_str());
      return r;
    },
    [&](int f, const Next& next) {                               // 2. 缓存：命中就不往后传
      if (auto it = cache.find(f); it != cache.end()) { std::printf("  → 缓存：命中\n"); return it->second; }
      std::printf("  → 缓存：没命中\n");
      return cache[f] = next(f);
    },
    [](int f, const Next& next) {                                // 3. 参数检查：越界直接拒绝
      if (f < 0 || f >= 240) return std::string("越界");
      return next(f);
    },
  };
  Next pipeline = build(mws, [&](int f) {                         // 最里面：真正解码
    ++decoded;
    std::printf("    解码第 %d 帧\n", f);
    return "帧" + std::to_string(f);
  });
  pipeline(3);
  pipeline(3);
  pipeline(300);
  std::printf("解码器一共被调用 %d 次\n", decoded);
}
