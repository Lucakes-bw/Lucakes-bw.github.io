# tinynet

一个小型的 Reactor 网络库（C++17，Linux），《计算机网络 → 服务器开发》一章的示例项目：

- `EventLoop`、`Channel`、`Poller`：事件循环，对 epoll 的封装（第 1 篇）
- `Buffer`、`TcpConnection`、`TcpServer`、`Acceptor`：缓冲区和连接的生命周期（第 2 篇）
- `TimerQueue`：timerfd + 最小堆的定时器（第 3 篇）
- `EventLoopThreadPool`：one loop per thread，eventfd 跨线程唤醒（第 4 篇）
- `http/`：增量的 HTTP 解析器和 HttpServer（第 5 篇）
- `bench/`：压测的对照组和 wrk 脚本（第 6 篇）

## 构建

只能在 Linux 上构建（epoll、eventfd、timerfd）。macOS 和 Windows 可以用 Docker 或 WSL。

```bash
cmake --preset debug -G Ninja && cmake --build --preset debug && ctest --preset debug
cmake --preset tsan -G Ninja && cmake --build --preset tsan && ctest --preset tsan
cmake --preset release -G Ninja && cmake --build --preset release
./build/release/http_server 8000 0 60      # 端口、IO 线程数、空闲超时秒数
wrk -t1 -c100 -d10s http://127.0.0.1:8000/
```
