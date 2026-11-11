// 《状态模式》练习 3：用 std::variant 写分片下载任务：失败后等一会儿重试，最多 3 次，等待时间翻倍，重连后从断点继续
#include <cstdio>
#include <optional>
#include <variant>

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct Connecting { int attempt; long bytes; };   // 第几次尝试（从 1 开始），已经下载了多少
struct Downloading { int attempt; long bytes; };
struct Waiting { int attempt; int delayMs; long bytes; };   // 失败后等多久再连
struct Done { long bytes; };
struct Failed { int attempts; };
using Task = std::variant<Connecting, Downloading, Waiting, Done, Failed>;

struct Connected {};
struct Data { long n; };
struct NetError {};
struct Timer {};
struct Finished {};
using Event = std::variant<Connected, Data, NetError, Timer, Finished>;

constexpr int kMaxAttempts = 3;

Task retryOrFail(int attempt, long bytes) {
  if (attempt >= kMaxAttempts) return Failed{attempt};
  return Waiting{attempt, 500 << (attempt - 1), bytes};   // 500、1000 毫秒
}

std::optional<Task> next(const Task& t, const Event& e) {
  return std::visit(overloaded{
      [](const Connecting& c, const Connected&) -> std::optional<Task> { return Downloading{c.attempt, c.bytes}; },
      [](const Connecting& c, const NetError&) -> std::optional<Task> { return retryOrFail(c.attempt, c.bytes); },
      [](const Downloading& d, const Data& x) -> std::optional<Task> { return Downloading{d.attempt, d.bytes + x.n}; },
      [](const Downloading& d, const NetError&) -> std::optional<Task> { return retryOrFail(d.attempt, d.bytes); },
      [](const Downloading& d, const Finished&) -> std::optional<Task> { return Done{d.bytes}; },
      [](const Waiting& w, const Timer&) -> std::optional<Task> { return Connecting{w.attempt + 1, w.bytes}; },
      [](const auto&, const auto&) -> std::optional<Task> { return std::nullopt; },
  }, t, e);
}

void show(const Task& t) {
  std::visit(overloaded{
      [](const Connecting& c) { std::printf("连接中（第 %d 次，从 %ld 字节处开始）\n", c.attempt, c.bytes); },
      [](const Downloading& d) { std::printf("下载中（第 %d 次，%ld 字节）\n", d.attempt, d.bytes); },
      [](const Waiting& w) { std::printf("等待 %d 毫秒后重试\n", w.delayMs); },
      [](const Done& d) { std::printf("完成，共 %ld 字节\n", d.bytes); },
      [](const Failed& f) { std::printf("失败，试了 %d 次\n", f.attempts); },
  }, t);
}

void run(const char* title, std::initializer_list<Event> events) {
  std::printf("%s\n", title);
  Task t = Connecting{1, 0};
  std::printf("  "); show(t);
  for (const Event& e : events)
    if (auto n = next(t, e)) { t = *n; std::printf("  "); show(t); }
}

int main() {
  run("断了一次，重试成功：", {Connected{}, Data{4096}, NetError{}, Timer{}, Connected{}, Data{8192}, Finished{}});
  run("一直连不上：", {NetError{}, Timer{}, NetError{}, Timer{}, NetError{}, Timer{}});
  std::printf("sizeof(Task) = %zu\n", sizeof(Task));
}
