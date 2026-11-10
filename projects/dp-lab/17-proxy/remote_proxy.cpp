// 《代理模式》：远程代理——RemoteDecoder 把每次调用打包成一条消息发给「另一台机器」，调用方看到的还是 Decoder 接口
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

struct Decoder {
    virtual ~Decoder() = default;
    virtual int width() = 0;
    virtual int height() = 0;
    virtual int fps() = 0;
};

// 真实对象：运行在服务端
class HwDecoder final : public Decoder {
public:
    int width() override { return 1280; }
    int height() override { return 720; }
    int fps() override { return 24; }
};

// 服务端：收到一条请求消息，调真实对象，把结果编成回复消息
std::string serve(Decoder& real, const std::string& request) {
    if (request == "width") return std::to_string(real.width());
    if (request == "height") return std::to_string(real.height());
    if (request == "fps") return std::to_string(real.fps());
    if (request == "info")
        return std::to_string(real.width()) + "," + std::to_string(real.height()) + "," + std::to_string(real.fps());
    return "error";
}

// 网络：数一数往返了几次、传了多少字节（真实网络里每次往返要几百微秒到几十毫秒）
struct Channel {
    std::function<std::string(const std::string&)> remote;
    int round_trips = 0, bytes = 0;
    std::string call(const std::string& request) {
        ++round_trips;
        std::string reply = remote(request);
        bytes += static_cast<int>(request.size() + reply.size());
        return reply;
    }
};

// 远程代理：实现同一个接口，每个函数都变成一次网络往返
class RemoteDecoder final : public Decoder {
public:
    explicit RemoteDecoder(Channel& ch) : ch_(ch) {}
    int width() override { return std::atoi(ch_.call("width").c_str()); }
    int height() override { return std::atoi(ch_.call("height").c_str()); }
    int fps() override { return std::atoi(ch_.call("fps").c_str()); }
    // 远程接口通常要「粗粒度」：一次往返把常用的信息都带回来
    void info(int& w, int& h, int& f) {
        std::string r = ch_.call("info");
        std::sscanf(r.c_str(), "%d,%d,%d", &w, &h, &f);
    }

private:
    Channel& ch_;
};

int main() {
    HwDecoder real;
    Channel ch{[&](const std::string& req) { return serve(real, req); }};
    RemoteDecoder dec(ch);

    Decoder& d = dec;                  // 调用方只认识 Decoder
    int w = d.width(), h = d.height(), f = d.fps();
    std::printf("逐个调用：%dx%d@%d，往返 %d 次，%d 字节\n", w, h, f, ch.round_trips, ch.bytes);

    ch.round_trips = ch.bytes = 0;
    dec.info(w, h, f);
    std::printf("一次 info：%dx%d@%d，往返 %d 次，%d 字节\n", w, h, f, ch.round_trips, ch.bytes);
}
