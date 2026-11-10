// 《适配器》：播放器需要的接口——C++ 风格：返回 optional，资源自动释放
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct Packet {
    int stream;              // 流的编号
    bool video;              // 是不是视频流
    double time;             // 时间戳，单位是秒
    bool keyframe;
    std::vector<uint8_t> data;
};

class MediaSource {
public:
    virtual ~MediaSource() = default;
    virtual std::string describe() const = 0;
    virtual std::optional<Packet> next() = 0;     // 没有更多数据时返回空
};
