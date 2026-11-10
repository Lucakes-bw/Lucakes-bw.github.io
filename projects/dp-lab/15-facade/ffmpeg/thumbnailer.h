// 《外观模式》：Thumbnailer 外观的接口——头文件里没有任何 FFmpeg 的类型
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct Image {
    int width = 0, height = 0;
    double time = 0;                  // 这一帧实际的时间（秒）
    std::vector<uint8_t> rgb;         // RGB24，每行 width * 3 字节
};

class Thumbnailer {
public:
    explicit Thumbnailer(const std::string& video);   // 打开失败抛 std::runtime_error
    ~Thumbnailer();
    Thumbnailer(Thumbnailer&&) noexcept;
    Thumbnailer& operator=(Thumbnailer&&) noexcept;

    double duration() const;                           // 视频长度（秒）
    Image grab(double sec, int width = 320);           // 第 sec 秒的画面，缩放到宽 width
    void save(double sec, const std::string& png, int width = 320);

private:
    struct Impl;                                       // FFmpeg 的东西都在 Impl 里
    std::unique_ptr<Impl> impl_;
};

// 只要一张图时，一个函数就够了
void save_thumbnail(const std::string& video, double sec, const std::string& png, int width = 320);
void save_png(const Image& img, const std::string& png);
