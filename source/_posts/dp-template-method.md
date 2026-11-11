---
title: 模板方法模式
date: 2026-11-11 23:00:00
updated: 2026-11-11 23:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 模板方法
  - NVI
  - 钩子
  - CRTP
  - FFmpeg
order: 4
description: 视频编辑器要把素材导出成 MP4、GIF、WAV，三个导出函数都是「打开输入 → 解码 → 处理 → 编码 → 写文件 → 清理」，复制粘贴写成三份：58 行里有 12 行三份一字不差，加一个「进度回调和取消」要改 3 个函数、10 处。改成模板方法（NVI：公有的非虚 run() 写流程，受保护的虚函数写步骤）后，同样的需求只改基类，子类一行不动。再实测三个坑：「重写时必须调基类」的写法漏调一次，每个任务漏 2 个文件描述符，批量导出到第 127 个就失败，NVI 写法跑 1000 个都没事；构造和析构函数里调虚函数调到的是基类版本，间接调到纯虚函数时程序直接终止，Clang 和 GCC 的表现还不一样；钩子按采样调用时，虚函数比按缓冲区调用慢 5～13 倍。最后用真正的 FFmpeg 实现三个导出任务，并比较 CRTP、std::function 写法，以及模板方法和策略、工厂方法、建造者的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-template-method]
---

> **本文目标**：理解模板方法模式：基类定好一个算法的骨架，把其中可变的步骤留给子类；会把复制粘贴的几个流程重构成模板方法，用 NVI 惯用法（公有非虚接口 + 受保护的虚函数）来写；分清基本操作和钩子；知道「好莱坞原则」；量出加一个公共需求要改几处、「重写时必须调基类」漏调一次的后果、钩子调用的开销；知道构造函数和析构函数里不能指望虚函数；会用 CRTP 和 `std::function` 写同样的东西；分清模板方法和策略、工厂方法、建造者。
>
> **前置知识**：上一篇《命令模式》；《组合优于继承与 C++ 的四种多态》（脆弱基类、框架留出的扩展点）、《简单工厂与工厂方法》（第 3.3 节提过工厂方法和模板方法的关系）、《策略模式》（第 5 节从策略那边比较过模板方法）；音视频专栏的《转封装与转码》（转码的完整流程）。
>
> **编译环境**：g++/clang++ -std=c++17，完整代码在 projects/dp-lab/21-template-method/

## 1. 问题：三个导出任务，一套流程抄了三遍

### 1.1 复制粘贴版

视频编辑器有个「导出」菜单：导出 MP4（重新编码成 H.264）、导出 GIF（做表情包）、导出 WAV（只要声音）。三个功能的流程是一样的（《转封装与转码》里讲过）：

1. 打开输入文件，找到要处理的那路流；
2. 打开解码器；
3. 打开编码器和输出文件；
4. 循环读包、解码；
5. 处理解码出来的帧；
6. 编码、写进输出文件；
7. 冲刷解码器和编码器里剩下的帧，关闭文件。

导出 MP4 的函数写好以后，导出 GIF 是把它复制一份、改几行，导出 WAV 再复制一份。下面的程序用一个模拟的媒体库代替 FFmpeg（只计数，不做真正的编解码，这样能在浏览器里运行；第 2.7 节有真正调用 FFmpeg 的版本），它像 `bbb-10s.mp4` 一样有 240 个视频包、470 个音频包，视频解码器有 2 帧的延迟，最后要冲刷出来：

```cpp
// 《模板方法模式》：问题版——导出 MP4、GIF、WAV 三个函数，流程一样，整段复制粘贴后各改几行
#include <cstdio>
#include <string>

// ---------- 模拟的媒体库（代替 FFmpeg，只计数、不做真正的编解码） ----------
enum class Media { Video, Audio };
struct Packet { Media type; int index; };
struct Frame { Media type; int index; int w, h; };

int g_open = 0;                                  // 当前打开着的文件个数

struct Demuxer {                                 // 像 bbb-10s.mp4：240 个视频包、470 个音频包，交错存放
    int v = 0, a = 0;
    bool open(const std::string& path) { ++g_open; return !path.empty(); }
    bool read(Packet& p) {
        if (v == 240 && a == 470) return false;
        bool video = a == 470 || (v < 240 && v * 470 <= a * 240);
        p = video ? Packet{Media::Video, v++} : Packet{Media::Audio, a++};
        return true;
    }
    int percent() const { return (v + a) * 100 / 710; }
    void close() { --g_open; }
};

struct Decoder {                                 // 视频解码器有 2 帧延迟（B 帧重排），最后要冲刷出来
    Media type;
    int pending = 0, next = 0;
    explicit Decoder(Media t) : type(t) {}
    bool decode(const Packet&, Frame& f) {
        ++pending;
        return flushOne(type == Media::Video ? 2 : 0, f);
    }
    bool flush(Frame& f) { return flushOne(0, f); }
    bool flushOne(int keep, Frame& f) {
        if (pending <= keep) return false;
        --pending;
        f = {type, next++, 1280, 720};
        return true;
    }
};

struct Encoder {                                 // 按编码器估算每帧的字节数
    std::string codec;
    int frames = 0;
    int encode(const Frame& f) {
        ++frames;
        if (codec == "gif") return f.w * f.h / 3;
        if (codec == "pcm_s16le") return 1024 * 2 * 2;
        return f.w * f.h / 40;                   // h264
    }
};

struct Writer {
    std::string path;
    long bytes = 0;
    bool open(const std::string& p) { path = p; ++g_open; return !p.empty(); }
    void write(int n) { bytes += n; }
    void close() { --g_open; }
    void discard() { bytes = 0; }                 // 删掉写了一半的文件
};

// ---------- 三个导出函数 ----------
bool exportMp4(const std::string& in, const std::string& out) {
    Demuxer dm;
    if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
    Decoder dec(Media::Video);
    Encoder enc{"h264"};
    Writer w;
    if (!w.open(out)) { dm.close(); return false; }
    Packet pkt;
    Frame f;
    while (dm.read(pkt)) {
        if (pkt.type != Media::Video) continue;
        if (dec.decode(pkt, f)) w.write(enc.encode(f));
    }
    while (dec.flush(f)) w.write(enc.encode(f));
    w.close();
    dm.close();
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), enc.frames, w.bytes);
    return true;
}

bool exportGif(const std::string& in, const std::string& out) {
    Demuxer dm;
    if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
    Decoder dec(Media::Video);
    Encoder enc{"gif"};
    Writer w;
    if (!w.open(out)) { dm.close(); return false; }
    Packet pkt;
    Frame f;
    while (dm.read(pkt)) {
        if (pkt.type != Media::Video) continue;
        if (!dec.decode(pkt, f)) continue;
        if (f.index % 2 != 0) continue;          // 24 fps 降到 12 fps
        f.w = 480; f.h = 270;                    // 缩小
        w.write(enc.encode(f));
    }
    while (dec.flush(f)) {
        if (f.index % 2 != 0) continue;
        f.w = 480; f.h = 270;
        w.write(enc.encode(f));
    }
    w.close();
    dm.close();
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), enc.frames, w.bytes);
    return true;
}

bool exportWav(const std::string& in, const std::string& out) {
    Demuxer dm;
    if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
    Decoder dec(Media::Audio);
    Encoder enc{"pcm_s16le"};
    Writer w;
    if (!w.open(out)) { dm.close(); return false; }
    Packet pkt;
    Frame f;
    while (dm.read(pkt)) {
        if (pkt.type != Media::Audio) continue;
        if (dec.decode(pkt, f)) w.write(enc.encode(f));
    }
    while (dec.flush(f)) w.write(enc.encode(f));
    w.close();
    dm.close();
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), enc.frames, w.bytes);
    return true;
}

int main() {
    exportMp4("bbb-10s.mp4", "out.mp4");
    exportGif("bbb-10s.mp4", "out.gif");
    exportWav("bbb-10s.mp4", "out.wav");
    std::printf("打开着的文件：%d\n", g_open);
}
```

```
out.mp4     240 帧   5529600 字节
out.gif     120 帧   5184000 字节
out.wav     470 帧   1925120 字节
打开着的文件：0
```

三个函数并排看，真正不同的地方很少：

<figure class="diagram">
<svg viewBox="0 0 710 266" role="img" aria-label="三个导出任务的 7 个步骤：选流、编码器两步三个任务各不相同，处理这一帧只有 GIF 不同，其余都相同"><g style="font-family:var(--mono)"><text x="255.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">导出 MP4</text><text x="435.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">导出 GIF</text><text x="615.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">导出 WAV</text><text x="0.0" y="52.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">1 打开输入</text><rect x="170" y="34" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="50.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="34" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="435.0" y="50.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="530" y="34" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="50.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="82.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">2 选流、开解码器</text><rect x="170" y="64" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="255.0" y="80.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">视频流</text><rect x="350" y="64" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="435.0" y="80.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">视频流</text><rect x="530" y="64" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="615.0" y="80.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">音频流</text><text x="0.0" y="112.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">3 开编码器、输出文件</text><rect x="170" y="94" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="255.0" y="110.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">h264</text><rect x="350" y="94" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="435.0" y="110.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">gif</text><rect x="530" y="94" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="615.0" y="110.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">pcm_s16le</text><text x="0.0" y="142.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">4 读包、解码</text><rect x="170" y="124" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="124" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="435.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="530" y="124" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="172.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">5 处理这一帧</text><rect x="170" y="154" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="170.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="154" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="435.0" y="170.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">隔帧丢、缩小</text><rect x="530" y="154" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="170.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="202.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">6 编码、写文件</text><rect x="170" y="184" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="200.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="184" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="435.0" y="200.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="530" y="184" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="200.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="232.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">7 冲刷、关闭</text><rect x="170" y="214" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="230.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="214" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="435.0" y="230.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="530" y="214" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="230.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="260.0" text-anchor="start" style="fill:var(--muted);font-size:11px">7 步里只有 3 步不同，不同的部分加起来 7 格；其余 14 格是同一段代码抄了三遍</text></g></svg>
</figure>

### 1.2 重复了多少、加一个需求要改几处

`measure.sh` 把三个函数体取出来逐行比较：三个函数一共 58 行（MP4 17 行、GIF 24 行、WAV 17 行），其中有 **12 行在三个函数里一字不差**，也就是 36 行是同一段代码的三份拷贝。剩下不一样的行，大多也只是把 `Media::Video` 换成了 `Media::Audio`、把 `"h264"` 换成了 `"gif"`。GIF 那个函数还多了一个问题：「隔帧丢、缩小」在读包循环和冲刷循环里各写了一遍。

重复本身还不是最糟的，糟的是**改的时候**。产品提了个新需求：导出要显示进度条，用户可以中途取消；取消时要关掉文件、删掉写了一半的输出。`copy_paste_progress.cpp` 是改完的版本，每个函数都要做同样的三处修改：

```diff
-bool exportMp4(const std::string& in, const std::string& out) {
+bool exportMp4(const std::string& in, const std::string& out, const Progress& progress) {
 ...
     while (dm.read(pkt)) {
+        if (progress && !progress(dm.percent())) {   // 取消：关文件，删掉写了一半的输出
+            w.close(); w.discard(); dm.close();
+            return false;
+        }
 ...
+    if (progress) progress(100);
```

`exportGif`、`exportWav` 再各来一遍。`measure.sh` 用 `diff` 统计（去掉第一行注释和 `main`，相邻的改动算一处）：**10 处改动、新增 20 行、改写旧代码 3 行，分布在 3 个函数里**。其中「取消时的清理」那 4 行被复制了三份，以后清理的步骤再变（比如还要删掉临时的调色板文件），又得改三处；只要有一处漏了，三个导出功能的行为就不一致了。这就是《为什么需要设计模式》里说的**散弹式修改**。

再加一种「导出 WebM」，就是第四份拷贝。

## 2. 模板方法模式

### 2.1 意图

GoF 的定义：

> 定义一个操作中算法的骨架，而将一些步骤延迟到子类中。模板方法使得子类可以不改变一个算法的结构即可重定义该算法的某些特定步骤。

大白话：**流程写在基类里，只写一遍；流程中会变的那几步做成虚函数，由子类填。**这里的「模板」是「样板、套路」的意思，和 C++ 的 `template` 没有关系（不过第 4.1 节会看到，用 C++ 模板也能写模板方法）。

几个名词：

- **模板方法**（template method）：写流程的那个函数，本文是 `run()`。它按固定的顺序调用各个步骤，**子类不能改它**。
- **基本操作**（primitive operation）：流程里必须由子类提供的步骤，在 C++ 里是纯虚函数，比如「处理哪路流」「用哪个编码器」。
- **钩子**（hook）：流程里**可以**由子类改、但基类给了默认实现的步骤，通常默认什么都不做，比如「处理一帧」默认原样保留。

### 2.2 用模板方法重构

把三个函数里相同的部分合并成基类 `ExportJob` 的 `run()`，不同的部分变成虚函数：

```cpp
// 《模板方法模式》：重构——基类的 run() 写好流程（不是虚函数），子类只重写不同的那几步
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ---------- 模拟的媒体库（代替 FFmpeg，只计数、不做真正的编解码） ----------
enum class Media { Video, Audio };
struct Packet { Media type; int index; };
struct Frame { Media type; int index; int w, h; };

int g_open = 0;                                  // 当前打开着的文件个数

struct Demuxer {                                 // 像 bbb-10s.mp4：240 个视频包、470 个音频包，交错存放
    int v = 0, a = 0;
    bool open(const std::string& path) { ++g_open; return !path.empty(); }
    bool read(Packet& p) {
        if (v == 240 && a == 470) return false;
        bool video = a == 470 || (v < 240 && v * 470 <= a * 240);
        p = video ? Packet{Media::Video, v++} : Packet{Media::Audio, a++};
        return true;
    }
    int percent() const { return (v + a) * 100 / 710; }
    void close() { --g_open; }
};

struct Decoder {                                 // 视频解码器有 2 帧延迟（B 帧重排），最后要冲刷出来
    Media type;
    int pending = 0, next = 0;
    explicit Decoder(Media t) : type(t) {}
    bool decode(const Packet&, Frame& f) {
        ++pending;
        return flushOne(type == Media::Video ? 2 : 0, f);
    }
    bool flush(Frame& f) { return flushOne(0, f); }
    bool flushOne(int keep, Frame& f) {
        if (pending <= keep) return false;
        --pending;
        f = {type, next++, 1280, 720};
        return true;
    }
};

struct Encoder {                                 // 按编码器估算每帧的字节数
    std::string codec;
    int frames = 0;
    int encode(const Frame& f) {
        ++frames;
        if (codec == "gif") return f.w * f.h / 3;
        if (codec == "pcm_s16le") return 1024 * 2 * 2;
        return f.w * f.h / 40;                   // h264
    }
};

struct Writer {
    std::string path;
    long bytes = 0;
    bool open(const std::string& p) { path = p; ++g_open; return !p.empty(); }
    void write(int n) { bytes += n; }
    void close() { --g_open; }
    void discard() { bytes = 0; }                 // 删掉写了一半的文件
};

// ---------- 模板方法 ----------
class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run(const std::string& in, const std::string& out) {   // 模板方法：公有、非虚，流程只写这一遍
        Demuxer dm;
        if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
        Decoder dec(media());
        Encoder enc{codec()};
        Writer w;
        if (!w.open(out)) { dm.close(); return false; }
        Packet pkt;
        Frame f;
        while (dm.read(pkt)) {
            if (pkt.type != media()) continue;
            if (dec.decode(pkt, f)) encodeOne(f, enc, w);
        }
        while (dec.flush(f)) encodeOne(f, enc, w);
        w.close();
        dm.close();
        onFinished(out, enc.frames, w.bytes);
        return true;
    }

protected:
    virtual Media media() const = 0;                     // 基本操作：子类必须实现
    virtual const char* codec() const = 0;
    virtual bool process(Frame&) { return true; }        // 钩子：默认什么都不做；返回 false 丢掉这一帧
    virtual void onFinished(const std::string& out, int frames, long bytes) {   // 钩子：默认打印一行
        std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), frames, bytes);
    }

private:
    void encodeOne(Frame& f, Encoder& enc, Writer& w) {
        if (process(f)) w.write(enc.encode(f));
    }
};

class Mp4Export : public ExportJob {
protected:
    Media media() const override { return Media::Video; }
    const char* codec() const override { return "h264"; }
};

class GifExport : public ExportJob {
protected:
    Media media() const override { return Media::Video; }
    const char* codec() const override { return "gif"; }
    bool process(Frame& f) override {
        if (f.index % 2 != 0) return false;              // 24 fps 降到 12 fps
        f.w = 480; f.h = 270;                            // 缩小
        return true;
    }
};

class WavExport : public ExportJob {
protected:
    Media media() const override { return Media::Audio; }
    const char* codec() const override { return "pcm_s16le"; }
};

int main() {
    std::vector<std::pair<std::unique_ptr<ExportJob>, std::string>> jobs;
    jobs.emplace_back(std::make_unique<Mp4Export>(), "out.mp4");
    jobs.emplace_back(std::make_unique<GifExport>(), "out.gif");
    jobs.emplace_back(std::make_unique<WavExport>(), "out.wav");
    for (auto& [job, out] : jobs) job->run("bbb-10s.mp4", out);
    std::printf("打开着的文件：%d\n", g_open);
}
```

```
out.mp4     240 帧   5529600 字节
out.gif     120 帧   5184000 字节
out.wav     470 帧   1925120 字节
打开着的文件：0
```

输出和复制粘贴版完全一样。几个要点：

- **`run()` 是公有的、非虚的**。子类没法重写它，流程就不会被改乱；调用者只和 `run()` 打交道。
- **步骤是受保护的虚函数**。外面的代码不能单独调用 `process()`，只有 `run()` 会在合适的时候调它。
- `media()`、`codec()` 是纯虚函数（基本操作），每个子类必须写，漏写了编译不过（`make_unique<一个抽象类>` 报错）。
- `process()`、`onFinished()` 是钩子，有默认实现，`Mp4Export` 和 `WavExport` 不需要处理帧，就不写。
- 「隔帧丢、缩小」现在只写在 `GifExport::process()` 里一次；读包循环和冲刷循环都通过 `encodeOne()` 调到它。

这种「公有非虚函数调用私有或受保护的虚函数」的写法在 C++ 里有个专门的名字：**NVI**（Non-Virtual Interface，非虚接口）惯用法，第 3.1 节细说。

### 2.3 类图和时序图

```uml
label: 模板方法：抽象类 ExportJob 的 run 是公有的非虚函数，调用受保护的虚函数 media、codec、process、onFinished；Mp4Export、GifExport、WavExport 继承它，只重写自己不同的步骤
classes:
  - { id: Job, name: ExportJob, stereo: abstract, x: 250, y: 20, w: 280, ops: ['+ run(in, out) : bool', '*# media() : Media', '*# codec() : const char*', '# process(f) : bool', '# onFinished(out, n, bytes)', '- encodeOne(f, enc, w)'] }
  - { id: Mp4, name: Mp4Export, x: 20, y: 260, w: 210, ops: ['# media()', '# codec()'] }
  - { id: Gif, name: GifExport, x: 285, y: 260, w: 210, ops: ['# media()', '# codec()', '# process(f)'] }
  - { id: Wav, name: WavExport, x: 550, y: 260, w: 210, ops: ['# media()', '# codec()'] }
rels:
  - { from: Mp4, to: Job, type: inherit, mid: 220, tdx: -80 }
  - { from: Gif, to: Job, type: inherit, mid: 220 }
  - { from: Wav, to: Job, type: inherit, mid: 220, tdx: 80 }
notes:
  - { x: 560, y: 20, w: 200, text: ['run() {', '  打开输入（media()）', '  打开编码器（codec()）', '  循环：解码 → process(f)', '        → 编码、写文件', '  冲刷、关闭', '  onFinished(...)', '}'], to: Job }
```

类图里斜体的 `media()`、`codec()` 是纯虚函数；`process()`、`onFinished()` 是普通的虚函数（钩子），只有 `GifExport` 重写了 `process()`。运行时的调用顺序：

```uml
label: 时序图：main 调用 GifExport 对象的 run；run 是基类的代码，它先后调用 media、codec，在循环里对每一帧调用 process，最后调用 onFinished；这些调用都是基类调子类
participants:
  - { id: m, name: 'main', x: 20, w: 80 }
  - { id: b, name: 'ExportJob 部分', x: 200, w: 150 }
  - { id: g, name: 'GifExport 部分', x: 470, w: 150 }
messages:
  - { from: m, to: b, text: 'run(in, out)', n: 1 }
  - { from: b, to: g, text: 'media()', n: 2 }
  - { from: g, to: b, text: 'Video', type: reply }
  - { from: b, to: g, text: 'codec()', n: 3 }
  - { from: g, to: b, text: '"gif"', type: reply }
  - { from: b, to: g, text: 'process(f)', n: 4 }
  - { from: g, to: b, text: 'true / false', type: reply }
  - { from: b, to: b, text: 'encode、write', n: 5 }
  - { from: b, to: b, text: 'onFinished(...)', n: 6, note: '没重写，执行基类的默认版本' }
  - { from: b, to: m, text: 'true', type: reply }
frags:
  - { from: 5, to: 7, label: 'loop [每一帧]' }
```

注意时序图里的两条生命线其实是**同一个对象**的两部分：基类那部分的代码和子类那部分的代码。所有的箭头都是从基类指向子类的。

### 2.4 加进度回调：只改基类

同样的「进度回调和取消」需求，在模板方法版里只需要改 `ExportJob`（`template_method_progress.cpp`）：

```diff
+using Progress = std::function<bool(int percent)>;   // 返回 false 表示用户取消
+
 class ExportJob {
 public:
     virtual ~ExportJob() = default;
+    void setProgress(Progress p) { progress_ = std::move(p); }
     bool run(const std::string& in, const std::string& out) {
 ...
         while (dm.read(pkt)) {
+            if (progress_ && !progress_(dm.percent())) {  // 取消：关文件，删掉写了一半的输出
+                w.close(); w.discard(); dm.close();
+                return false;
+            }
 ...
+        if (progress_) progress_(100);
 ...
 private:
+    Progress progress_;
```

<figure class="diagram">
<svg viewBox="0 0 710 126" role="img" aria-label="加进度回调和取消要改的代码（不算 main）：复制粘贴版 10 处改动、新增 20 行、改了 3 个函数；模板方法版 6 处、新增 9 行、只改了基类 1 个类"><g style="font-family:var(--mono)"><text x="108.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">改动处数</text><rect x="108.0" y="30.0" width="100.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="214.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">10 处</text><rect x="108.0" y="60.0" width="60.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="174.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6 处</text><text x="308.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">新增行数</text><rect x="308.0" y="30.0" width="100.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="414.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">20 行</text><rect x="308.0" y="60.0" width="45.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="359.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">9 行</text><text x="508.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">要改的函数/类</text><rect x="508.0" y="30.0" width="90.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="604.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3 个</text><rect x="508.0" y="60.0" width="30.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="544.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1 个</text><text x="100.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">复制粘贴版</text><text x="100.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">模板方法版</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：diff -U0 比较加需求前后的文件，去掉第一行注释和 main；相邻的 hunk 算一处。Clang 和 GCC 上结果相同</text></g></svg>
</figure>

| 加进度回调和取消（不算 `main`） | 改动几处 | 改写旧代码 | 新增 | 改到的函数 / 类 |
| --- | --- | --- | --- | --- |
| 复制粘贴版 | 10 处 | 3 行 | 20 行 | 3 个函数 |
| 模板方法版 | 6 处 | 0 行 | 9 行 | 只有 `ExportJob` |

模板方法版的 6 处改动全在基类里，三个子类一行没动，取消时的清理代码只有一份。处数没有少很多，是因为基类本身要加 `#include`、类型别名、成员变量和设置函数这些「一次性」的东西；**真正的区别在于改动不随任务的个数增长**：复制粘贴版每多一种导出格式，这个需求就多 3 处改动；模板方法版不管有多少个子类，都只改这一个类。

运行结果：

```
  进度 0%
  进度 50%
  进度 100%
out.mp4     240 帧   5529600 字节
  进度 0%
  进度 50%
  进度 100%
out.gif     120 帧   5184000 字节
out.wav 已取消
打开着的文件：0
```

### 2.5 好莱坞原则

模板方法里，调用的方向是反过来的：不是子类调基类的函数来完成任务，而是**基类在合适的时候调子类**。子类只回答问题（「处理哪路流？」「这一帧要不要？」），不知道、也不关心整个流程。这叫**好莱坞原则**：「别打电话给我们，我们会打给你」（Don't call us, we'll call you）。

这也是框架（framework）和库（library）的区别：用库时，是你的代码调库的函数；用框架时，是框架调你的代码，你只填它留出来的空。GoogleTest 调你的 `SetUp()`、`TestBody()`、`TearDown()`，Qt 调你的 `paintEvent()`，都是这样。《组合优于继承与 C++ 的四种多态》说过，继承只在「框架明确设计好的扩展点」上才安全；模板方法就是设计扩展点的方法：哪些步骤可以重写、在什么时候被调用，都由基类公开承诺。

下面的演示一步步走 `run()`：左边是基类的流程，右边是子类，箭头表示基类在调子类。点「导出 MP4」「导出 WAV」看没有重写 `process()` 的子类；「漏调基类」是第 3.2 节的对照写法：

<div class="dsviz" data-viz="dp-template-method"></div>

### 2.6 基本操作还是钩子

一个步骤做成纯虚函数（基本操作）还是有默认实现的虚函数（钩子），是设计模板方法时最主要的决定：

| | 基本操作（纯虚） | 钩子（有默认实现） |
| --- | --- | --- |
| 子类不写会怎样 | 编译错误：子类还是抽象类，创建不了对象 | 用默认实现，悄悄地 |
| 适合 | 每个子类都必须回答的问题：处理哪路流、用哪个编码器 | 大多数子类不关心的扩展点：处理帧、完成通知、清理临时文件 |
| 子类写错签名 | 编译错误 | 只是多了一个无关的函数，钩子没起作用（练习 4） |

钩子越多，子类越省事，但「忘了写」「写错了」越难发现。所以：**每个子类都必须决定的步骤，宁可做成纯虚函数**；钩子的默认实现应该是「什么都不做」或者一个对所有子类都正确的行为；子类重写时一律加 `override`。

钩子还有一种常见的形式：基类问子类一个是 / 否的问题，决定流程里的某一段要不要执行。比如练习 2 里给「导出封面」加的 `done()`：子类说够了，基类就停止读包。

### 2.7 真正的 FFmpeg 版

`ffmpeg/export.cpp` 用真正的 FFmpeg 实现了这三个导出任务。结构和模拟版一样，只是步骤换成了 FFmpeg 的 API：

- 基类 `ExportJob::run()`（109 行）：`avformat_open_input` → `av_find_best_stream`（流的类型问子类）→ 打开解码器 → `avcodec_find_encoder_by_name`（编码器名字问子类）→ `configure()`（编码参数由子类设置）→ 写文件头 → 读包、解码 → `keep()` 钩子 → `convert()` 转成编码器要的格式 → 编码、写包 → 冲刷解码器和编码器 → 写文件尾 → 释放所有资源。
- 中间层 `VideoExport`（26 行）：两个视频任务共用的 `convert()`，用 `sws_scale` 缩放和转像素格式。
- `Mp4Export`（12 行）：libx264，640×360，`yuv420p`。
- `GifExport`（11 行）：320×180，`rgb8`，`keep()` 隔帧丢（24 fps 降到 12 fps）。
- `WavExport`（36 行）：AAC 解码出来的是浮点平面格式，用 `swr_convert` 转成 16 位交错的 PCM。

<details>
<summary>展开 ffmpeg/export.cpp</summary>

<!-- run: off -->
```cpp
// 《模板方法模式》：用真正的 FFmpeg 导出 MP4、GIF、WAV——基类的 run() 是整套流程，子类只配置编码器、转换格式
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

class ExportJob {
public:
    virtual ~ExportJob() = default;

    bool run(const std::string& in, const std::string& out) {   // 模板方法：公有、非虚
        auto t0 = std::chrono::steady_clock::now();
        bool ok = openInput(in) && openOutput(out) && transcode();
        if (ok) bytes_ = avio_tell(oc_->pb);
        releaseConverter();                      // 钩子：子类释放自己的资源
        cleanup();                               // 公共资源永远由基类释放，成功失败都走这里
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-8s 解码 %3d 帧，编码 %3d 帧，%8ld 字节，%7.1f ms%s\n", out.c_str() + out.rfind('/') + 1,
                    decoded_, encoded_, bytes_, ms, ok ? "" : "  失败");
        return ok;
    }

protected:
    // 基本操作：子类必须实现
    virtual AVMediaType mediaType() const = 0;
    virtual const char* encoderName() const = 0;
    virtual void configure(AVCodecContext* enc, const AVCodecContext* dec, AVRational fps) = 0;
    virtual AVFrame* convert(AVFrame* in, const AVCodecContext* enc) = 0;   // 转成编码器要的格式
    // 钩子：有默认实现
    virtual bool keep(int /*index*/) { return true; }                       // 返回 false 丢掉这一帧
    virtual void releaseConverter() {}

private:
    AVFormatContext *ic_ = nullptr, *oc_ = nullptr;
    AVCodecContext *dec_ = nullptr, *enc_ = nullptr;
    AVStream* ost_ = nullptr;
    int stream_ = -1, decoded_ = 0, encoded_ = 0;
    int64_t nextPts_ = 0;
    long bytes_ = 0;

    bool openInput(const std::string& in) {
        if (avformat_open_input(&ic_, in.c_str(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info(ic_, nullptr) < 0) return false;
        const AVCodec* dc = nullptr;
        stream_ = av_find_best_stream(ic_, mediaType(), -1, -1, &dc, 0);
        if (stream_ < 0) return false;
        dec_ = avcodec_alloc_context3(dc);
        avcodec_parameters_to_context(dec_, ic_->streams[stream_]->codecpar);
        dec_->pkt_timebase = ic_->streams[stream_]->time_base;
        return avcodec_open2(dec_, dc, nullptr) >= 0;
    }

    bool openOutput(const std::string& out) {
        if (avformat_alloc_output_context2(&oc_, nullptr, nullptr, out.c_str()) < 0) return false;
        const AVCodec* ec = avcodec_find_encoder_by_name(encoderName());
        if (!ec) { std::fprintf(stderr, "没有编码器 %s\n", encoderName()); return false; }
        enc_ = avcodec_alloc_context3(ec);
        configure(enc_, dec_, av_guess_frame_rate(ic_, ic_->streams[stream_], nullptr));
        if (oc_->oformat->flags & AVFMT_GLOBALHEADER) enc_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(enc_, ec, nullptr) < 0) return false;
        ost_ = avformat_new_stream(oc_, nullptr);
        avcodec_parameters_from_context(ost_->codecpar, enc_);
        ost_->time_base = enc_->time_base;
        if (avio_open(&oc_->pb, out.c_str(), AVIO_FLAG_WRITE) < 0) return false;
        return avformat_write_header(oc_, nullptr) >= 0;
    }

    bool transcode() {
        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        while (av_read_frame(ic_, pkt) >= 0) {
            if (pkt->stream_index == stream_) decode(pkt, frame);
            av_packet_unref(pkt);
        }
        decode(nullptr, frame);                  // 冲刷解码器
        encode(nullptr);                         // 冲刷编码器
        av_frame_free(&frame);
        av_packet_free(&pkt);
        return av_write_trailer(oc_) >= 0;
    }

    void decode(const AVPacket* pkt, AVFrame* frame) {
        avcodec_send_packet(dec_, pkt);
        while (avcodec_receive_frame(dec_, frame) >= 0) {
            if (keep(decoded_++)) {
                AVFrame* f = convert(frame, enc_);
                f->pts = nextPts_;
                nextPts_ += mediaType() == AVMEDIA_TYPE_AUDIO ? f->nb_samples : 1;
                encode(f);
            }
            av_frame_unref(frame);
        }
    }

    void encode(const AVFrame* f) {
        avcodec_send_frame(enc_, f);
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(enc_, pkt) >= 0) {
            ++encoded_;
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc_->time_base, ost_->time_base);
            av_interleaved_write_frame(oc_, pkt);
        }
        av_packet_free(&pkt);
    }

    void cleanup() {
        avcodec_free_context(&enc_);
        avcodec_free_context(&dec_);
        if (oc_ && oc_->pb) avio_closep(&oc_->pb);
        avformat_free_context(oc_);
        oc_ = nullptr;
        avformat_close_input(&ic_);
    }
};

class VideoExport : public ExportJob {           // 中间层：两个视频任务共用的缩放和像素格式转换
protected:
    AVMediaType mediaType() const override { return AVMEDIA_TYPE_VIDEO; }
    AVFrame* convert(AVFrame* in, const AVCodecContext* enc) override {
        sws_ = sws_getCachedContext(sws_, in->width, in->height, static_cast<AVPixelFormat>(in->format),
                                    enc->width, enc->height, enc->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!out_) {
            out_ = av_frame_alloc();
            out_->format = enc->pix_fmt;
            out_->width = enc->width;
            out_->height = enc->height;
            av_frame_get_buffer(out_, 0);
        }
        av_frame_make_writable(out_);            // 编码器可能还引用着上一帧
        sws_scale(sws_, in->data, in->linesize, 0, in->height, out_->data, out_->linesize);
        return out_;
    }
    void releaseConverter() override {
        sws_freeContext(sws_);
        av_frame_free(&out_);
    }

private:
    SwsContext* sws_ = nullptr;
    AVFrame* out_ = nullptr;
};

class Mp4Export : public VideoExport {           // H.264，缩到 640×360
protected:
    const char* encoderName() const override { return "libx264"; }
    void configure(AVCodecContext* enc, const AVCodecContext*, AVRational fps) override {
        enc->width = 640;
        enc->height = 360;
        enc->pix_fmt = AV_PIX_FMT_YUV420P;
        enc->time_base = av_inv_q(fps);
        enc->framerate = fps;
        av_opt_set(enc->priv_data, "preset", "veryfast", 0);
    }
};

class GifExport : public VideoExport {           // 320×180，每 2 帧留 1 帧（24 fps → 12 fps）
protected:
    const char* encoderName() const override { return "gif"; }
    void configure(AVCodecContext* enc, const AVCodecContext*, AVRational fps) override {
        enc->width = 320;
        enc->height = 180;
        enc->pix_fmt = AV_PIX_FMT_RGB8;
        enc->time_base = av_inv_q(av_div_q(fps, AVRational{2, 1}));
    }
    bool keep(int index) override { return index % 2 == 0; }
};

class WavExport : public ExportJob {             // AAC 解码出来的浮点平面格式转成 16 位交错 PCM
protected:
    AVMediaType mediaType() const override { return AVMEDIA_TYPE_AUDIO; }
    const char* encoderName() const override { return "pcm_s16le"; }
    void configure(AVCodecContext* enc, const AVCodecContext* dec, AVRational) override {
        enc->sample_fmt = AV_SAMPLE_FMT_S16;
        enc->sample_rate = dec->sample_rate;
        av_channel_layout_copy(&enc->ch_layout, &dec->ch_layout);
        enc->time_base = AVRational{1, dec->sample_rate};
    }
    AVFrame* convert(AVFrame* in, const AVCodecContext* enc) override {
        if (!swr_) {
            swr_alloc_set_opts2(&swr_, &enc->ch_layout, enc->sample_fmt, enc->sample_rate,
                                &in->ch_layout, static_cast<AVSampleFormat>(in->format), in->sample_rate, 0, nullptr);
            swr_init(swr_);
            out_ = av_frame_alloc();
        }
        av_frame_unref(out_);
        out_->format = enc->sample_fmt;
        out_->sample_rate = enc->sample_rate;
        av_channel_layout_copy(&out_->ch_layout, &enc->ch_layout);
        out_->nb_samples = swr_get_out_samples(swr_, in->nb_samples);
        av_frame_get_buffer(out_, 0);
        out_->nb_samples = swr_convert(swr_, out_->data, out_->nb_samples,
                                       const_cast<const uint8_t**>(in->extended_data), in->nb_samples);
        return out_;
    }
    void releaseConverter() override {
        swr_free(&swr_);
        av_frame_free(&out_);
    }

private:
    SwrContext* swr_ = nullptr;
    AVFrame* out_ = nullptr;
};

int main(int argc, char** argv) {
    std::string in = argc > 1 ? argv[1] : "../../../../source/media/bbb-10s.mp4";
    std::string dir = argc > 2 ? argv[2] : "build";
    av_log_set_level(AV_LOG_ERROR);
    std::vector<std::pair<std::unique_ptr<ExportJob>, std::string>> jobs;
    jobs.emplace_back(std::make_unique<Mp4Export>(), dir + "/out.mp4");
    jobs.emplace_back(std::make_unique<GifExport>(), dir + "/out.gif");
    jobs.emplace_back(std::make_unique<WavExport>(), dir + "/out.wav");
    for (auto& [job, out] : jobs) job->run(in, out);
}
```

</details>

`sh ffmpeg/build.sh && sh ffmpeg/run.sh` 的结果（时间是 7 次的中位数）：

| 任务 | 解码 / 编码 | 输出 | Clang / macOS（FFmpeg 9.0） | GCC / Ubuntu（FFmpeg 6.1） |
| --- | --- | --- | --- | --- |
| `out.mp4` | 240 / 240 帧 | H.264 640×360 | 368 ms，643 823 字节 | 477 ms，641 202 字节 |
| `out.gif` | 240 / 120 帧 | GIF 320×180 | 365 ms，1 504 852 字节 | 379 ms，1 504 852 字节 |
| `out.wav` | 469 / 469 帧 | PCM 48 kHz 立体声 | 8.8 ms，1 920 078 字节 | 10.2 ms，1 921 102 字节 |

`ffprobe` 核对：MP4 240 帧、9.96 秒，GIF 120 帧、9.93 秒，WAV 10.000 秒。时间几乎全花在 FFmpeg 的编解码上，和用哪个编译器关系不大（Ubuntu 那一列是在只有 2 个 CPU 的虚拟机里跑的，libx264 的多线程吃亏）。两边的 WAV 差 1024 字节，正好是 256 个立体声采样：macOS 上的 FFmpeg 9.0 去掉了 AAC 开头的前导采样，输出正好 480000 个采样（10.000 秒），6.1 多输出了 256 个。

这一版里，基类占了全部代码的一半，三个子类加起来不到 60 行。再加一种「导出 WebM」只要写一个和 `Mp4Export` 差不多长的子类：换成 `libvpx-vp9` 编码器、`.webm` 文件名，FFmpeg 那一大套流程一行都不用碰。

## 3. 深入模板方法

### 3.1 NVI：公有接口不要是虚函数

Herb Sutter 在《Virtuality》一文里给了 C++ 写虚函数的几条准则，核心就是 NVI：

1. **接口最好是非虚的**：公有函数写成非虚函数（模板方法）；
2. **虚函数最好是私有的**：只有子类需要调用基类的版本时，才做成受保护的；
3. **析构函数**：要通过基类指针删除时，写成公有的虚函数；否则写成受保护的非虚函数。

为什么连只有一个步骤的接口也推荐这样写？因为非虚的公有函数是基类**守住的一道门**：参数检查、加锁、计时、日志、进度回调这些「每次调用都要做」的事，以后都可以加在这里，所有子类自动得到（第 2.4 节就是这样）。如果公有函数本身就是虚函数，这些事就只能指望每个子类自己做。

C++ 允许子类重写基类的**私有**虚函数（访问控制管的是「谁能调用」，不管「谁能重写」）。所以 `media()`、`codec()` 写成 `private` 也完全可以，子类照样能 `override`，只是不能调用基类的版本。本文写成 `protected`，是因为钩子的默认实现有时值得复用（子类在自己的 `onFinished()` 里先调一下 `ExportJob::onFinished()` 打印一行）。

### 3.2 漏调基类：「重写时必须调基类」

模板方法还有一种常见的「变体」：步骤本身做成虚函数，子类重写时**必须先调用基类的版本**，因为基类的版本里有公共的工作。很多框架都有这样的约定（Android 的 `onCreate()` 里要先调 `super.onCreate()`），这种写法通常被叫作 **call super 反模式**：约定写在文档里，编译器管不了。

`call_super.cpp` 对比两种写法。GIF 要先生成调色板，存在一个临时文件里；写法一里，`GifExport` 重写了 `cleanup()` 关掉调色板文件，却忘了调 `ExportJob::cleanup()`；写法二（NVI）里，公共的清理由 `run()` 自己做，子类只重写钩子 `onCleanup()`：

```cpp
// 《模板方法模式》：「重写时记得调基类」的写法里，子类漏调一次，批量导出到第 N 个就打不开文件；NVI 写法漏不了
#include <cstdio>
#include <cstdlib>
#include <string>

int g_open = 3;                                  // 打开着的文件描述符：stdin、stdout、stderr 已经占了 3 个
int g_max_open = 256;                            // 模拟 macOS 的默认软限制（launchctl limit maxfiles）；Linux 一般是 1024

struct File {
    bool open(const std::string&) {
        if (g_open >= g_max_open) return false;    // Too many open files
        ++g_open;
        return true;
    }
    void close() { --g_open; }
};

namespace call_super {                           // 写法一：步骤是虚函数，子类重写时「必须先调基类的版本」
class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run(const std::string& in, const std::string& out) {
        if (!prepare(in, out)) return false;
        /* ……读包、解码、编码、写文件…… */
        cleanup();
        return true;
    }

protected:
    virtual bool prepare(const std::string& in, const std::string& out) {   // 重写时必须调 ExportJob::prepare
        if (!input_.open(in)) return false;
        if (!output_.open(out)) { input_.close(); return false; }
        return true;
    }
    virtual void cleanup() {                                                // 重写时必须调 ExportJob::cleanup
        output_.close();
        input_.close();
    }

private:
    File input_, output_;
};

class GifExport : public ExportJob {             // GIF 先生成调色板，存在一个临时文件里
protected:
    bool prepare(const std::string& in, const std::string& out) override {
        if (!ExportJob::prepare(in, out)) return false;
        return palette_.open("palette.tmp");
    }
    void cleanup() override {
        palette_.close();                        // 忘了调 ExportJob::cleanup()：输入和输出都没关
    }

private:
    File palette_;
};
}  // namespace call_super

namespace nvi {                                  // 写法二：基类自己做完公共的步骤，只在中间留出钩子
class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run(const std::string& in, const std::string& out) {
        if (!input_.open(in)) return false;
        if (!output_.open(out)) { input_.close(); return false; }
        bool ok = onPrepare();
        if (ok) { /* ……读包、解码、编码、写文件…… */ }
        onCleanup();                             // 先让子类清理自己的东西
        output_.close();                         // 公共的清理永远由基类来做
        input_.close();
        return ok;
    }

protected:
    virtual bool onPrepare() { return true; }    // 钩子：默认什么都不做
    virtual void onCleanup() {}

private:
    File input_, output_;
};

class GifExport : public ExportJob {
protected:
    bool onPrepare() override { return palette_.open("palette.tmp"); }
    void onCleanup() override { palette_.close(); }   // 不需要、也没法调基类的 cleanup

private:
    File palette_;
};
}  // namespace nvi

template <class Job>
void batch(const char* name, int count) {        // 批量导出 count 个 GIF
    g_open = 3;
    int ok = 0;
    for (int i = 1; i <= count; ++i) {
        Job job;
        if (!job.run("clip" + std::to_string(i) + ".mp4", "clip" + std::to_string(i) + ".gif")) {
            std::printf("%s：第 %d 个任务打不开文件（Too many open files）\n", name, i);
            break;
        }
        ++ok;
    }
    std::printf("%s：成功 %d 个，打开着的文件描述符 %d 个\n", name, ok, g_open);
}

int main(int argc, char** argv) {
    if (argc > 1) g_max_open = std::atoi(argv[1]);
    batch<call_super::GifExport>("调基类的写法", 1000);
    batch<nvi::GifExport>("NVI 写法    ", 1000);
}
```

程序模拟进程的文件描述符上限：macOS 上图形程序的默认软限制是 256（`launchctl limit maxfiles` 可以看到），Linux 一般是 1024（容器里 `ulimit -n` 也是 1024），stdin、stdout、stderr 已经占了 3 个：

```
调基类的写法：第 127 个任务打不开文件（Too many open files）
调基类的写法：成功 126 个，打开着的文件描述符 255 个
NVI 写法    ：成功 1000 个，打开着的文件描述符 3 个
```

<figure class="diagram">
<svg viewBox="0 0 640 230" role="img" aria-label="批量导出 GIF 时打开着的文件描述符：调基类的写法每个任务漏 2 个，从 3 涨到 255，第 127 个任务打不开文件；NVI 写法一直是 3 个"><g style="font-family:var(--mono)"><line x1="56" y1="180" x2="596" y2="180" style="stroke:var(--border-2)"/><line x1="56" y1="20" x2="56" y2="180" style="stroke:var(--border-2)"/><text x="50.0" y="184.0" text-anchor="end" style="fill:var(--muted);font-size:10.5px">0</text><text x="50.0" y="124.7" text-anchor="end" style="fill:var(--muted);font-size:10.5px">100</text><text x="50.0" y="65.5" text-anchor="end" style="fill:var(--muted);font-size:10.5px">200</text><text x="50.0" y="32.3" text-anchor="end" style="fill:var(--muted);font-size:10.5px">256</text><text x="56.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">0</text><text x="191.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">50</text><text x="326.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">100</text><text x="398.9" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">127</text><text x="461.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">150</text><text x="596.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">200</text><line x1="56" y1="28.3" x2="596" y2="28.3" style="stroke:var(--danger);stroke-dasharray:4 3"/><text x="596.0" y="23.3" text-anchor="end" style="fill:var(--danger);font-size:10.5px">上限 256</text><polyline points="56.0,178.2 396.2,28.9" style="fill:none;stroke:var(--warn);stroke-width:2"/><circle cx="398.9" cy="28.9" r="4" style="fill:var(--danger)"/><text x="406.9" y="44.9" text-anchor="start" style="fill:var(--danger);font-size:11px">第 127 个任务：Too many open files</text><text x="218.0" y="95.0" text-anchor="end" style="fill:var(--warn);font-size:11px;font-weight:700">调基类的写法（漏调一次）</text><polyline points="56.0,178.2 596.0,178.2" style="fill:none;stroke:var(--accent);stroke-width:2"/><text x="596.0" y="172.2" text-anchor="end" style="fill:var(--accent);font-size:11px;font-weight:700">NVI 写法：一直是 3 个</text><text x="326.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">批量导出的第几个 GIF（纵轴：打开着的文件描述符）</text></g></svg>
</figure>

每个 GIF 任务漏掉输入和输出 2 个文件描述符，批量导出到第 127 个就失败了；上限改成 1024（`./call_super 1024`），撑到第 511 个。这种 bug 在开发时很难发现：导出一两个文件一切正常，Clang 和 GCC 开着 `-Wall -Wextra` 都没有任何警告，只有批量处理时才出问题，报的还是「打不开文件」，看上去和 GIF 毫无关系。

NVI 写法把「必须做的事」从约定变成了结构：公共的清理写在 `run()` 里，子类根本没有机会跳过它。子类需要插手的地方，由基类专门留出钩子，钩子默认什么都不做，子类重写钩子时也不需要调基类。

### 3.3 构造函数和析构函数里不能用虚函数

既然流程写在基类里，有人会想：干脆在基类的构造函数里就把流程跑了，或者在析构函数里调子类的清理。这不行。C++ 规定：**在构造函数和析构函数执行期间，对象的动态类型就是正在构造（析构）的那个类**。基类的构造函数运行时，子类部分还没构造；基类的析构函数运行时，子类部分已经析构了。这时调虚函数，调到的是基类自己的版本：

```cpp
// 《模板方法模式》：在构造函数和析构函数里调虚函数——调到的是基类自己的版本，不是子类的
#include <cstdio>
#include <string>

class ExportJob {
public:
    ExportJob() { std::printf("创建任务：%s\n", name()); }               // 这时子类部分还没构造
    virtual ~ExportJob() {
        std::printf("销毁任务：%s\n", name());                           // 这时子类部分已经析构了
        removeTemp();
    }
    virtual const char* name() const { return "（未命名任务）"; }

protected:
    virtual void removeTemp() { std::printf("  ExportJob::removeTemp：没有临时文件\n"); }
};

class GifExport : public ExportJob {
public:
    GifExport() : palette_("palette.tmp") { std::printf("  GifExport 构造完，调色板 %s\n", palette_.c_str()); }
    ~GifExport() override { std::printf("  GifExport 析构\n"); }
    const char* name() const override { return "导出 GIF"; }

protected:
    void removeTemp() override { std::printf("  删除 %s\n", palette_.c_str()); }

private:
    std::string palette_;
};

int main() {
    GifExport job;
    std::printf("main 里调用 job.name()：%s\n", job.name());
}
```

```
创建任务：（未命名任务）
  GifExport 构造完，调色板 palette.tmp
main 里调用 job.name()：导出 GIF
  GifExport 析构
销毁任务：（未命名任务）
  ExportJob::removeTemp：没有临时文件
```

构造完成以后 `name()` 才返回「导出 GIF」；析构函数里的 `removeTemp()` 调到的是基类的版本，调色板临时文件没有删。Clang 和 GCC 的输出一样，`-Wall -Wextra` 下都**没有警告**。Clang 的静态分析器能发现：`clang++ --analyze -Xanalyzer -analyzer-checker=optin.cplusplus.VirtualCall` 报告「Call to virtual method 'ExportJob::name' during construction bypasses virtual dispatch」，构造函数里 1 处、析构函数里 2 处。

如果调到的是**纯虚函数**，就更糟了。`pure_call.cpp` 里，构造函数调一个普通的成员函数 `log()`，`log()` 再调纯虚函数 `name()`：

<!-- run: off -->
```cpp
// 《模板方法模式》：构造函数经过一个普通函数间接调到纯虚函数，编译器没有警告，运行时程序直接终止
#include <cstdio>

class ExportJob {
public:
    ExportJob() { log("创建"); }                 // 看上去只调了一个普通成员函数
    virtual ~ExportJob() = default;

protected:
    virtual const char* name() const = 0;

private:
    void log(const char* what) { std::printf("%s任务：%s\n", what, name()); }   // 这里调到了纯虚函数
};

class GifExport : public ExportJob {
protected:
    const char* name() const override { return "导出 GIF"; }
};

int main() {
    std::printf("开始\n");
    std::fflush(stdout);
    GifExport job;
    std::printf("这一行不会执行\n");
}
```

编译没有警告，运行时程序直接终止（退出码 134，即 SIGABRT）：

| | Clang 21 / macOS（libc++） | GCC 13 / Ubuntu（libstdc++） |
| --- | --- | --- |
| `pure_call` 的输出 | `libc++abi: Pure virtual function called!` | `pure virtual method called` `terminate called without an active exception` |
| 构造函数里**直接**调纯虚函数（`pure_direct_ce.cpp`） | 警告 `-Wcall-to-pure-virtual-from-ctor-dtor`（默认开启）；能链接，运行时同样终止 | 警告 `pure virtual ... called from constructor`（默认开启）；**链接失败**：`undefined reference to 'ExportJob::name() const'` |

直接调用时两个编译器都会警告，但后果不同：GCC 知道构造期间的动态类型就是 `ExportJob`，直接生成对 `ExportJob::name()` 的调用，而这个函数没有函数体，于是链接失败；Clang 仍然通过虚表调用，虚表里这一项是运行时库的 `__cxa_pure_virtual`，它打印消息后终止程序。间接调用时，两个编译器都看不出来。（`clang++ --analyze` 的 `cplusplus.PureVirtualCall` 检查能发现间接的这一处。）

怎么办：

- **构造函数只负责把对象建好，流程放到普通的成员函数里**，等对象完整构造后再调用，本文的 `run()` 就是这样。
- 基类构造时需要的、由子类决定的信息，**作为参数传给基类的构造函数**：`GifExport() : ExportJob("导出 GIF") {}`。
- 一定要「构造完马上做点什么」，就用工厂函数：`auto job = std::make_unique<GifExport>(); job->init(); return job;`，把构造函数设为受保护的，让工厂成为唯一的创建途径。
- 析构函数里的清理，让每个类在**自己的**析构函数里清理自己的东西：调色板文件由 `~GifExport()` 删。

### 3.4 标准库和框架里的模板方法

**`std::streambuf`** 是标准库里最典型的 NVI。`ostream` 写字符时调用 `streambuf` 的公有非虚函数 `sputc()`，它是内联的：缓冲区还有空间时直接把字符放进去；放不下时才调用受保护的虚函数 `overflow()`。自定义输出目标（写到网络、写到日志、统计字节数）只需要重写 `overflow()` 和 `sync()`：

```cpp
// 《模板方法模式》：标准库里的 NVI——ostream 调 streambuf 的公有非虚函数 sputc，缓冲区满了才调受保护的虚函数 overflow
#include <cstdio>
#include <ostream>
#include <streambuf>

// 把字幕文本写到 stdout，顺便统计字节数和行数；只重写两个受保护的虚函数 overflow 和 sync
class CountingBuf : public std::streambuf {
public:
    long bytes = 0, lines = 0, calls = 0;
    CountingBuf(int size, bool echo) : size_(size), echo_(echo) { setp(buf_, buf_ + size_); }   // size = 0：没有缓冲区
    ~CountingBuf() override { sync(); }

protected:
    int_type overflow(int_type ch) override {    // 基类的 sputc() 发现缓冲区放不下时才调用它
        ++calls;
        drain();
        if (!traits_type::eq_int_type(ch, traits_type::eof())) emit(static_cast<char>(ch));
        return traits_type::not_eof(ch);
    }
    int sync() override { drain(); return 0; }   // flush 时调用

private:
    char buf_[64];
    int size_;
    bool echo_;
    void emit(char c) { ++bytes; if (c == '\n') ++lines; if (echo_) std::putchar(c); }
    void drain() {                               // 把缓冲区里攒下的字符写出去
        for (char* p = pbase(); p < pptr(); ++p) emit(*p);
        setp(buf_, buf_ + size_);
    }
};

void writeSubs(std::ostream& out) {              // 只用 ostream 的公有接口
    out << "1\n00:00:01,000 --> 00:00:03,500\n大雄兔醒了。\n\n";
    out << "2\n00:00:04,000 --> 00:00:06,000\n" << "第 " << 2 << " 条字幕\n";
    out.flush();
}

int main() {
    for (int size : {0, 16, 64}) {
        CountingBuf buf(size, size == 0);            // 只在第一次把字幕打印出来
        std::ostream out(&buf);
        writeSubs(out);
        std::printf("-- 缓冲区 %2d 字节：写了 %ld 字节、%ld 行，overflow 被调用 %ld 次\n",
                    size, buf.bytes, buf.lines, buf.calls);
    }
}
```

```
1
00:00:01,000 --> 00:00:03,500
大雄兔醒了。

2
00:00:04,000 --> 00:00:06,000
第 2 条字幕
-- 缓冲区  0 字节：写了 100 字节、7 行，overflow 被调用 100 次
-- 缓冲区 16 字节：写了 100 字节、7 行，overflow 被调用 5 次
-- 缓冲区 64 字节：写了 100 字节、7 行，overflow 被调用 1 次
```

没有缓冲区时每个字符都要走一次虚函数；有 16 字节的缓冲区，100 个字符只调了 5 次 `overflow()`。缓冲的逻辑写在基类的非虚函数里，子类不用管，这正是 NVI 的好处。（Clang 和 GCC 的输出相同。）`std::locale` 的各个 facet 也是这样：公有的 `num_put::put()` 调受保护的虚函数 `do_put()`，标准库里所有以 `do_` 开头的虚函数都是 NVI 的步骤。

**GoogleTest** 的 `Test::Run()` 是模板方法：调 `SetUp()`，成功后调 `TestBody()`，最后不论成败都调 `TearDown()`。练习 3 会写一个这样的测试夹具。

**FFmpeg** 是 C 写的，没有虚函数，但结构一样。`libavcodec/decode.c` 里的 `avcodec_send_packet()`、`avcodec_receive_frame()` 是公共流程：检查参数、管理内部的包队列、处理冲刷、时间戳、裁剪前导采样；每个解码器在自己的 `FFCodec` 结构里填几个函数指针（`init`、`decode` 或 `receive_frame`、`flush`、`close`），由公共代码在合适的时候调用。新写一个解码器只需要实现这几个回调，FFmpeg 的 API 规则（比如 `EAGAIN` 和 `EOF` 的约定）由公共代码保证。

### 3.5 什么时候不用模板方法

- **几个可变的步骤要自由组合**：GIF 的「隔帧丢 + 缩小」和 MP4 的编码器想组合成「小尺寸 MP4」，用继承要再写一个子类，处理方式 × 编码格式的组合越来越多，就成了《桥接模式》开头的类爆炸。这时把步骤做成对象（策略）更合适，见第 4.2 节。
- **要在运行时换步骤**：继承关系在编译时就定了，一个 `GifExport` 对象不能中途变成 `Mp4Export`。
- **流程本身还在变**：模板方法固定的是流程；如果每个新需求都要改流程的顺序，基类会改个不停，子类也跟着受影响。
- **继承已经很深**：模板方法依赖继承，基类的改动会影响所有子类（脆弱基类问题）。基类的流程越长，子类越依赖它的细节；一般基类 + 一层子类（最多加一个中间层，像 FFmpeg 版的 `VideoExport`）就够了。

## 4. 现代 C++ 写法

### 4.1 CRTP：编译期的模板方法

如果用哪个子类在编译时就确定了，可以不用虚函数：基类做成模板，把子类作为模板参数传进来，`run()` 里用 `static_cast<Derived&>(*this)` 调子类的步骤。这就是 CRTP（奇异递归模板模式，《原型模式》第 3.1 节用它自动生成过 `clone()`），第 5 章会专门讲：

```cpp
// 《模板方法模式》：编译期的模板方法——CRTP：基类模板通过 static_cast 调子类的步骤，没有虚函数
#include <cstdio>

struct Frame { int index; int w, h; };

template <class Derived>
class ExportJob {
public:
    void run(int frames) {                       // 模板方法：流程写在基类模板里
        Derived& self = static_cast<Derived&>(*this);
        int kept = 0;
        long bytes = 0;
        for (int i = 0; i < frames; ++i) {
            Frame f{i, 1280, 720};
            if (!self.process(f)) continue;      // 子类写了 process 就调子类的，没写就调下面的默认版本
            bytes += self.encode(f);             // 子类必须写 encode，否则编译不过
            ++kept;
        }
        std::printf("%-4s %4d 帧 %9ld 字节\n", self.name(), kept, bytes);
    }

protected:
    bool process(Frame&) { return true; }        // 钩子的默认实现
};

class Mp4Export : public ExportJob<Mp4Export> {
    friend class ExportJob<Mp4Export>;           // 步骤是私有的，只让基类调用
    const char* name() const { return "MP4"; }
    int encode(const Frame& f) const { return f.w * f.h / 40; }
};

class GifExport : public ExportJob<GifExport> {
    friend class ExportJob<GifExport>;
    const char* name() const { return "GIF"; }
    bool process(Frame& f) {                     // 「重写」钩子：其实是隐藏了基类的同名函数
        if (f.index % 2 != 0) return false;
        f.w = 480; f.h = 270;
        return true;
    }
    int encode(const Frame& f) const { return f.w * f.h / 3; }
};

struct VirtualJob {                              // 对照：有虚函数的类，对象里多一个虚表指针
    virtual ~VirtualJob() = default;
    virtual int encode(const Frame& f) const = 0;
};

int main() {
    Mp4Export mp4;
    GifExport gif;
    mp4.run(240);
    gif.run(240);
    std::printf("sizeof(Mp4Export) = %zu，sizeof(VirtualJob) = %zu\n", sizeof(Mp4Export), sizeof(VirtualJob));
    // Mp4Export 和 GifExport 没有共同的基类：ExportJob<Mp4Export> 和 ExportJob<GifExport> 是两个不相关的类型，
    // 不能放进同一个 vector<ExportJob*> 里
}
```

```
MP4   240 帧   5529600 字节
GIF   120 帧   5184000 字节
sizeof(Mp4Export) = 1，sizeof(VirtualJob) = 8
```

和虚函数版比：

- **没有虚函数调用**，步骤可以内联（第 4.3 节量一下差多少）；对象里也没有虚表指针。
- **子类漏写了必须的步骤，编译时就报错**。`crtp_missing_ce.cpp` 把 `encode` 写成了 `encodeAudio`，Clang 报 `no member named 'encode' in 'WavExport'`，GCC 报 `'class WavExport' has no member named 'encode'`。不过错误出现在基类模板的实例化里，而不是子类的定义处；C++20 可以用 concept 把要求写在模板参数上。
- 「钩子」靠名字查找实现：子类定义了 `process` 就隐藏了基类的同名函数。签名写错了不会报错，只是调到基类的默认版本，和虚函数版不加 `override` 时一样。
- **`ExportJob<Mp4Export>` 和 `ExportJob<GifExport>` 是两个毫不相干的类型**，没有共同的基类，不能放进同一个 `vector` 里批量运行。要两者兼得，可以让 CRTP 基类再继承一个有虚函数 `run()` 的接口：外层一次虚调用，里面的步骤全部内联。

### 4.2 std::function：用组合代替继承

另一个方向是不用继承：流程还是固定地写在一个类里，可变的步骤是 `std::function` 成员，创建对象时传进来。这其实就是《策略模式》：

```cpp
// 《模板方法模式》：用组合代替继承——流程还是固定的，可变的步骤换成 std::function，可以在运行时拼装
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct Frame { int index; int w, h; };

using Step = std::function<bool(Frame&)>;        // 处理一帧；返回 false 丢掉这一帧
using Encode = std::function<int(const Frame&)>; // 编码一帧，返回字节数

class ExportPipeline {
public:
    ExportPipeline(std::string name, std::vector<Step> steps, Encode encode)
        : name_(std::move(name)), steps_(std::move(steps)), encode_(std::move(encode)) {}

    void run(int frames) const {                 // 流程和模板方法版一样，只写一遍
        int kept = 0;
        long bytes = 0;
        Frame out{};
        for (int i = 0; i < frames; ++i) {
            Frame f{i, 1280, 720};
            bool keep = true;
            for (const Step& s : steps_)
                if (!(keep = s(f))) break;
            if (!keep) continue;
            bytes += encode_(f);
            ++kept;
            out = f;
        }
        std::printf("%4d 帧 %4d×%-4d %9ld 字节  %s\n", kept, out.w, out.h, bytes, name_.c_str());
    }

private:
    std::string name_;
    std::vector<Step> steps_;
    Encode encode_;
};

Step everyNth(int n) { return [n](Frame& f) { return f.index % n == 0; }; }
Step scaleTo(int w, int h) { return [w, h](Frame& f) { f.w = w; f.h = h; return true; }; }
Encode h264() { return [](const Frame& f) { return f.w * f.h / 40; }; }
Encode gif() { return [](const Frame& f) { return f.w * f.h / 3; }; }

int main() {
    std::vector<ExportPipeline> jobs = {
        {"MP4",            {},                                 h264()},
        {"GIF",            {everyNth(2), scaleTo(480, 270)},   gif()},
        {"MP4 小尺寸",     {scaleTo(640, 360)},                h264()},   // 新组合：不用新写一个子类
        {"GIF 预览 6fps",  {everyNth(4), scaleTo(320, 180)},   gif()},
    };
    for (const auto& j : jobs) j.run(240);
}
```

```
 240 帧 1280×720    5529600 字节  MP4
 120 帧  480×270    5184000 字节  GIF
 240 帧  640×360    1382400 字节  MP4 小尺寸
  60 帧  320×180    1152000 字节  GIF 预览 6fps
```

「MP4 小尺寸」和「GIF 预览 6fps」是两个新组合，没有写新的类，只是把已有的步骤拼了一下。用继承的话，每个组合都是一个子类。

代价：

- 步骤之间不能共享状态。模板方法的子类可以有成员变量（`VideoExport` 的 `SwsContext`），几个步骤共用；`std::function` 里的状态只能藏在 lambda 的捕获里，一个步骤一份。
- 步骤拿不到基类的受保护成员，基类要把它们需要的东西作为参数传进去。
- 每次调用多一次间接调用，`std::function` 还可能为捕获的数据分配堆内存（《策略模式》第 4.3 节量过）。

### 4.3 钩子的调用开销：看粒度

钩子被调用的次数取决于它的**粒度**。`bench_hooks.cpp` 给 10 秒的立体声音频调音量（96 万个 `float` 采样，每个乘一个增益），比较四种写法：钩子每个采样调一次（虚函数、`std::function`、CRTP），以及虚函数钩子每个缓冲区（一个音频包，2048 个采样）调一次：

<details>
<summary>展开 bench_hooks.cpp</summary>

<!-- run: off -->
```cpp
// 《模板方法模式》：钩子的调用开销——给 10 秒音频调音量，钩子每个采样调一次 vs 每个缓冲区调一次（请用 -O2）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

constexpr int kBlock = 1024 * 2;                 // 一个音频包：1024 个采样 × 2 声道
constexpr int kBlocks = 470;                     // bbb-10s.mp4 有 470 个音频包

// 1. 虚函数钩子，每个采样调一次
struct SampleJob {
    virtual ~SampleJob() = default;
    void run(std::vector<float>& pcm) {          // 模板方法
        for (float& s : pcm) s = processSample(s);
    }
    virtual float processSample(float s) = 0;
};
struct SampleGain : SampleJob {
    float g;
    explicit SampleGain(float g_) : g(g_) {}
    float processSample(float s) override { return s * g; }
};

// 2. 虚函数钩子，每个缓冲区调一次
struct BlockJob {
    virtual ~BlockJob() = default;
    void run(std::vector<float>& pcm) {
        for (size_t i = 0; i < pcm.size(); i += kBlock) processBlock(pcm.data() + i, kBlock);
    }
    virtual void processBlock(float* p, int n) = 0;
};
struct BlockGain : BlockJob {
    float g;
    explicit BlockGain(float g_) : g(g_) {}
    void processBlock(float* p, int n) override { for (int i = 0; i < n; ++i) p[i] *= g; }
};

// 3. CRTP，每个采样调一次（编译期绑定，能内联）
template <class D>
struct CrtpJob {
    void run(std::vector<float>& pcm) {
        for (float& s : pcm) s = static_cast<D*>(this)->processSample(s);
    }
};
struct CrtpGain : CrtpJob<CrtpGain> {
    float g;
    explicit CrtpGain(float g_) : g(g_) {}
    float processSample(float s) const { return s * g; }
};

// 4. std::function，每个采样调一次
struct FuncJob {
    std::function<float(float)> process;
    void run(std::vector<float>& pcm) {
        for (float& s : pcm) s = process(s);
    }
};

template <class F>
double median_ms(F&& f) {
    std::vector<double> t;
    for (int r = 0; r < 7; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 20; ++k) f();        // 一次计时处理 20 遍，取每遍的时间
        auto t1 = std::chrono::steady_clock::now();
        t.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / 20);
    }
    std::sort(t.begin(), t.end());
    return t[3];
}

int main(int argc, char**) {
    std::vector<float> pcm(static_cast<size_t>(kBlock) * kBlocks);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<float>(i % 200) / 100.0f - 1.0f;
    float g = argc > 5 ? 1.0f : 0.999f;          // 让编译器看不出增益是多少

    std::unique_ptr<SampleJob> a = std::make_unique<SampleGain>(g);   // 通过基类指针调用，防止编译器去虚化
    std::unique_ptr<BlockJob> b = std::make_unique<BlockGain>(g);
    CrtpGain c(g);
    FuncJob d{[g](float s) { return s * g; }};

    double ta = median_ms([&] { a->run(pcm); });
    double tb = median_ms([&] { b->run(pcm); });
    double tc = median_ms([&] { c.run(pcm); });
    double td = median_ms([&] { d.run(pcm); });
    std::printf("10 秒立体声 %zu 个采样，每遍的毫秒数（7 次取中位数）：\n", pcm.size());
    std::printf("  虚函数钩子，每个采样一次    %.3f ms\n", ta);
    std::printf("  虚函数钩子，每个缓冲区一次  %.3f ms\n", tb);
    std::printf("  CRTP，每个采样一次          %.3f ms\n", tc);
    std::printf("  std::function，每个采样一次 %.3f ms\n", td);
    std::printf("（校验 %.3f）\n", pcm[12345]);
}
```

</details>

<figure class="diagram">
<svg viewBox="0 0 740 186" role="img" aria-label="给 10 秒立体声音频调音量，每遍毫秒数。Clang：虚函数每采样 0.607、std::function 0.542、虚函数每缓冲区 0.046、CRTP 0.046；GCC：0.295、0.520、0.057、0.057"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang 21 / macOS</text><rect x="198.0" y="30.0" width="156.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="360.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.607 ms</text><rect x="198.0" y="60.0" width="139.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="343.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.542 ms</text><rect x="198.0" y="90.0" width="11.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="215.8" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.046 ms</text><rect x="198.0" y="120.0" width="11.8" height="20.0" rx="2" style="fill:var(--link)"/><text x="215.8" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.046 ms</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="468.0" y="30.0" width="75.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="549.9" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.295 ms</text><rect x="468.0" y="60.0" width="133.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="607.7" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.520 ms</text><rect x="468.0" y="90.0" width="14.7" height="20.0" rx="2" style="fill:var(--accent)"/><text x="488.7" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.057 ms</text><rect x="468.0" y="120.0" width="14.7" height="20.0" rx="2" style="fill:var(--link)"/><text x="488.7" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.057 ms</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数钩子，每个采样</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::function，每个采样</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数钩子，每个缓冲区</text><text x="190.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">CRTP，每个采样</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_hooks：96 万个 float 采样乘一个增益，-O2，每次计时跑 20 遍取平均，7 次取中位数；越短越快</text></g></svg>
</figure>

| 每遍毫秒数（96 万个采样） | Clang 21 / macOS | GCC 13 / Ubuntu |
| --- | --- | --- |
| 虚函数钩子，每个采样一次 | 0.607 | 0.295 |
| `std::function`，每个采样一次 | 0.542 | 0.520 |
| 虚函数钩子，每个缓冲区一次 | 0.046 | 0.057 |
| CRTP，每个采样一次 | 0.046 | 0.057 |

- 每个缓冲区调一次虚函数，和 CRTP 一样快：虚函数只调了 470 次，循环在子类里，编译器可以把它向量化（一次乘 4 个或 8 个 `float`）。
- 每个采样调一次虚函数，Clang 下慢 13 倍，GCC 下慢 5 倍。慢的原因不只是间接调用本身，更是调用挡住了循环的向量化。
- GCC 的虚函数每采样只慢 5 倍，是因为 `-O2` 默认打开了**推测性去虚化**（`-fdevirtualize-speculatively`）：编译器猜对象多半是 `SampleGain`，生成「比较虚表里的函数指针，相等就走内联的代码」的分支。加上 `-fno-devirtualize-speculatively` 再测，就变成了 0.41～0.61 ms，和 Clang 差不多。
- `std::function` 两边都慢，它在调用处什么都猜不到。

结论和《策略模式》第 4.2 节一样：**粒度比写法重要**。模板方法的步骤一般是「打开」「处理一帧」「完成」这种粗粒度的，每次调用多出来的几纳秒可以忽略；如果钩子要在最内层的循环里按元素调用，就把它改成按块处理（`processBlock(float*, n)`），或者用 CRTP 在编译期绑定。

### 4.4 怎么选

| 写法 | 什么时候选 | 代价 |
| --- | --- | --- |
| 虚函数 + NVI | 默认选择：子类的种类在运行时决定，要放进同一个容器里 | 每个步骤一次虚调用；继承带来的耦合 |
| CRTP | 类型编译期就定了，步骤在热循环里 | 没有共同基类；错误信息在模板里；代码膨胀 |
| `std::function` 成员 | 步骤要自由组合、运行时替换 | 步骤之间不能共享状态；拿不到受保护成员 |
| 复制粘贴 | 只有两份、而且流程以后会各走各的 | 改公共部分时要改每一份 |

最后一行不是玩笑：两份流程刚好相同、但它们是**碰巧**相同（以后会朝不同方向变），硬抽一个基类出来，以后每次变化都要在基类里加钩子、加开关，比复制还糟。抽象的时机，通常是出现第三份的时候。

## 5. 模板方法和相似的模式

《策略模式》第 5 节从策略的角度对比过一次，这里从模板方法这边看，再加上两个创建型模式：

| 模式 | 用什么机制 | 固定的是什么 | 可变的是什么 |
| --- | --- | --- | --- |
| **模板方法** | 继承：基类调子类重写的步骤 | 流程（步骤的顺序） | 其中几个步骤 |
| 策略 | 组合：上下文持有一个算法对象 | 上下文的其他部分 | 整个算法，可以运行时替换 |
| 工厂方法 | 继承：基类调子类重写的创建函数 | 使用产品的流程 | 创建哪种产品 |
| 建造者 | 组合：指挥者按顺序调建造者的各步 | 建造的顺序（指挥者） | 每一步怎么做、产出什么表示 |

- **模板方法和策略**：一个用继承换「步骤」，一个用组合换「算法」。模板方法的粒度更细：基类控制流程，子类只填几个空；策略把整个算法交出去。第 4.2 节的 `std::function` 版说明两者可以互相转换：把模板方法的每个步骤都做成策略，基类就变成了一个持有若干策略的上下文。选哪个看第 3.5 节：需要运行时换、需要自由组合时选策略。
- **模板方法和工厂方法**：工厂方法就是一种特殊的步骤，负责「创建一个对象」。《简单工厂与工厂方法》里的 `play()` 是模板方法，其中的 `createDecoder()` 是工厂方法。GoF 原书也说，工厂方法通常在模板方法里被调用。
- **模板方法和建造者**：GoF 原版建造者里，指挥者的 `construct()` 也是按固定顺序调一串步骤（《建造者模式》第 3 节），看上去很像模板方法。区别在于：建造者的步骤在**另一个对象**上（指挥者持有建造者），目的是**造出一个产品**；模板方法的步骤在**同一个对象**的子类里，目的是**完成一件事**。把建造者的指挥者换成基类、建造者的方法变成基类的虚函数，就成了模板方法。

## 6. 小结

- 模板方法模式把一个算法的流程写在基类里，可变的步骤留给子类；几个任务流程相同、只有几步不同时，用它消除复制粘贴（本文三个导出函数 58 行里有 12 行三份一字不差）。
- 在 C++ 里用 NVI 惯用法写：公有的模板方法是非虚函数，步骤是私有或受保护的虚函数。公共的需求只改基类（进度回调：复制粘贴版改 3 个函数 10 处，模板方法版只改基类）。
- 必须由子类回答的步骤做成纯虚函数（基本操作），可选的扩展点做成有默认实现的钩子；重写时一律加 `override`。
- 好莱坞原则：基类调子类，子类不调基类的流程。避免「重写时必须调基类」的约定：漏调一次，每个任务漏 2 个文件描述符，批量导出第 127 个就失败；把公共的工作留在模板方法里，只给子类开钩子。
- 构造函数和析构函数里调的虚函数不会分派到子类；间接调到纯虚函数时程序直接终止，Clang 和 GCC 只能发现直接调用的那种。流程不要放在构造函数里。
- 编译期可以用 CRTP（没有虚调用，但没有共同基类），要自由组合步骤时用 `std::function`（其实就是策略）。钩子的开销看粒度：按采样调用虚函数比按缓冲区慢 5～13 倍。

## 7. 练习

1. **辨析**：下面四个设计分别是模板方法、策略、工厂方法还是建造者？说出理由。
   - (a) 编码器基类的 `encode(frame)` 是非虚函数：检查帧的尺寸 → 调虚函数 `encodeImpl(frame)` → 统计码率；`H264Encoder`、`Vp9Encoder` 只实现 `encodeImpl()`。
   - (b) 封装器基类的 `writeHeader()` 里调虚函数 `makeIndexWriter()`，`Mp4Muxer` 返回 `MoovWriter`，`MkvMuxer` 返回 `CuesWriter`，后面的写入都通过这个对象。
   - (c) 播放器持有一个 `std::function<void(Frame&)> postProcess`，用户在设置里勾选「降噪」或「锐化」时换成不同的 lambda。
   - (d) `ThumbnailDirector::make(ThumbBuilder& b)` 依次调 `b.setSize()`、`b.addFrames()`、`b.addTimecode()`，`JpegSheetBuilder` 和 `HtmlSheetBuilder` 分别产出一张拼图和一个网页。
2. **动手**：给 `template_method.cpp` 加一个「导出封面」任务 `CoverExport`：只把第一帧编码成 JPEG（编码器 `mjpeg`，每帧 `w * h / 8` 字节）。要求读到第一帧就停止读包，不要把 710 个包都读完。基类要怎么改？其他三个子类要不要改？
3. **设计**：写一个最小的测试夹具（不用异常）：`TestCase::run()` 固定是 `setUp()` → 测试体 → `tearDown()`，`setUp()` 失败时跳过测试体，但**不管成败 `tearDown()` 都要执行**；`PlayerTest` 在 `setUp()` 里打开一个播放器、在 `tearDown()` 里关闭。写两三个测试（其中一个故意失败、一个 `setUp()` 失败），最后检查没有播放器泄漏。哪些函数是基本操作，哪些是钩子？
4. **找问题**：同事给 `GifExport::process()` 加了个 `const`（「这个函数又不改成员」），结果导出的 GIF 变成了 240 帧、7372 万字节（原尺寸，没有隔帧）。为什么？怎么让这类错误在编译时就暴露？如果 `process()` 是纯虚函数，会怎样？

   ```cpp
   // 《模板方法模式》：练习 4——找问题：GIF 导出出来是 240 帧原尺寸，GifExport 的钩子没起作用
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   // ---------- 模拟的媒体库（代替 FFmpeg，只计数、不做真正的编解码） ----------
   enum class Media { Video, Audio };
   struct Packet { Media type; int index; };
   struct Frame { Media type; int index; int w, h; };

   int g_open = 0;                                  // 当前打开着的文件个数

   struct Demuxer {                                 // 像 bbb-10s.mp4：240 个视频包、470 个音频包，交错存放
       int v = 0, a = 0;
       bool open(const std::string& path) { ++g_open; return !path.empty(); }
       bool read(Packet& p) {
           if (v == 240 && a == 470) return false;
           bool video = a == 470 || (v < 240 && v * 470 <= a * 240);
           p = video ? Packet{Media::Video, v++} : Packet{Media::Audio, a++};
           return true;
       }
       int percent() const { return (v + a) * 100 / 710; }
       void close() { --g_open; }
   };

   struct Decoder {                                 // 视频解码器有 2 帧延迟（B 帧重排），最后要冲刷出来
       Media type;
       int pending = 0, next = 0;
       explicit Decoder(Media t) : type(t) {}
       bool decode(const Packet&, Frame& f) {
           ++pending;
           return flushOne(type == Media::Video ? 2 : 0, f);
       }
       bool flush(Frame& f) { return flushOne(0, f); }
       bool flushOne(int keep, Frame& f) {
           if (pending <= keep) return false;
           --pending;
           f = {type, next++, 1280, 720};
           return true;
       }
   };

   struct Encoder {                                 // 按编码器估算每帧的字节数
       std::string codec;
       int frames = 0;
       int encode(const Frame& f) {
           ++frames;
           if (codec == "gif") return f.w * f.h / 3;
           if (codec == "pcm_s16le") return 1024 * 2 * 2;
           return f.w * f.h / 40;                   // h264
       }
   };

   struct Writer {
       std::string path;
       long bytes = 0;
       bool open(const std::string& p) { path = p; ++g_open; return !p.empty(); }
       void write(int n) { bytes += n; }
       void close() { --g_open; }
       void discard() { bytes = 0; }                 // 删掉写了一半的文件
   };

   // ---------- 模板方法 ----------
   class ExportJob {
   public:
       virtual ~ExportJob() = default;
       bool run(const std::string& in, const std::string& out) {   // 模板方法：公有、非虚，流程只写这一遍
           Demuxer dm;
           if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
           Decoder dec(media());
           Encoder enc{codec()};
           Writer w;
           if (!w.open(out)) { dm.close(); return false; }
           Packet pkt;
           Frame f;
           while (dm.read(pkt)) {
               if (pkt.type != media()) continue;
               if (dec.decode(pkt, f)) encodeOne(f, enc, w);
           }
           while (dec.flush(f)) encodeOne(f, enc, w);
           w.close();
           dm.close();
           onFinished(out, enc.frames, w.bytes);
           return true;
       }

   protected:
       virtual Media media() const = 0;                     // 基本操作：子类必须实现
       virtual const char* codec() const = 0;
       virtual bool process(Frame&) { return true; }        // 钩子：默认什么都不做；返回 false 丢掉这一帧
       virtual void onFinished(const std::string& out, int frames, long bytes) {   // 钩子：默认打印一行
           std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), frames, bytes);
       }

   private:
       void encodeOne(Frame& f, Encoder& enc, Writer& w) {
           if (process(f)) w.write(enc.encode(f));
       }
   };

   class Mp4Export : public ExportJob {
   protected:
       Media media() const override { return Media::Video; }
       const char* codec() const override { return "h264"; }
   };

   class GifExport : public ExportJob {
   protected:
       Media media() const override { return Media::Video; }
       const char* codec() const override { return "gif"; }
       bool process(Frame& f) const {               // 这一步不改成员，顺手加了个 const
           if (f.index % 2 != 0) return false;              // 24 fps 降到 12 fps
           f.w = 480; f.h = 270;                            // 缩小
           return true;
       }
   };

   class WavExport : public ExportJob {
   protected:
       Media media() const override { return Media::Audio; }
       const char* codec() const override { return "pcm_s16le"; }
   };

   int main() {
       std::vector<std::pair<std::unique_ptr<ExportJob>, std::string>> jobs;
       jobs.emplace_back(std::make_unique<Mp4Export>(), "out.mp4");
       jobs.emplace_back(std::make_unique<GifExport>(), "out.gif");
       jobs.emplace_back(std::make_unique<WavExport>(), "out.wav");
       for (auto& [job, out] : jobs) job->run("bbb-10s.mp4", out);
       std::printf("打开着的文件：%d\n", g_open);
   }
   ```

   ```
   out.mp4     240 帧   5529600 字节
   out.gif     240 帧  73728000 字节
   out.wav     470 帧   1925120 字节
   打开着的文件：0
   ```

<details>
<summary>点击查看答案</summary>

1. 
   - (a) **模板方法**：`encode()` 是非虚的流程（检查 → 编码 → 统计），子类只实现其中一步。这就是 NVI。
   - (b) **工厂方法**：子类决定创建哪种 `IndexWriter`，基类只通过接口使用它。它也是 `writeHeader()` 这个模板方法里的一步，这正是工厂方法最常见的样子。
   - (c) **策略**：后处理算法是一个对象（lambda），由播放器持有，运行时随用户的设置替换；没有继承。
   - (d) **建造者**：指挥者按固定顺序调建造者的各个步骤，不同的建造者产出不同的**表示**（JPEG 拼图、网页）。步骤在另一个对象上，目的是造出一个产品。

2. 基类加一个钩子 `done(int framesEncoded)`，默认返回 `false`（一直读到文件末尾），读包循环和冲刷循环在每一轮开始时问一下；`CoverExport` 重写它，编了一帧就返回 `true`。`Mp4Export`、`GifExport`、`WavExport` 都不用改：

   ```cpp
   // 《模板方法模式》：练习 2——加「导出封面」：只要第一帧，基类加一个钩子 done()，拿到就停止读包
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   // ---------- 模拟的媒体库（代替 FFmpeg，只计数、不做真正的编解码） ----------
   enum class Media { Video, Audio };
   struct Packet { Media type; int index; };
   struct Frame { Media type; int index; int w, h; };

   int g_open = 0;                                  // 当前打开着的文件个数

   struct Demuxer {                                 // 像 bbb-10s.mp4：240 个视频包、470 个音频包，交错存放
       int v = 0, a = 0;
       bool open(const std::string& path) { ++g_open; return !path.empty(); }
       bool read(Packet& p) {
           if (v == 240 && a == 470) return false;
           bool video = a == 470 || (v < 240 && v * 470 <= a * 240);
           p = video ? Packet{Media::Video, v++} : Packet{Media::Audio, a++};
           return true;
       }
       int percent() const { return (v + a) * 100 / 710; }
       void close() { --g_open; }
   };

   struct Decoder {                                 // 视频解码器有 2 帧延迟（B 帧重排），最后要冲刷出来
       Media type;
       int pending = 0, next = 0;
       explicit Decoder(Media t) : type(t) {}
       bool decode(const Packet&, Frame& f) {
           ++pending;
           return flushOne(type == Media::Video ? 2 : 0, f);
       }
       bool flush(Frame& f) { return flushOne(0, f); }
       bool flushOne(int keep, Frame& f) {
           if (pending <= keep) return false;
           --pending;
           f = {type, next++, 1280, 720};
           return true;
       }
   };

   struct Encoder {                                 // 按编码器估算每帧的字节数
       std::string codec;
       int frames = 0;
       int encode(const Frame& f) {
           ++frames;
           if (codec == "gif") return f.w * f.h / 3;
           if (codec == "pcm_s16le") return 1024 * 2 * 2;
           if (codec == "mjpeg") return f.w * f.h / 8;
           return f.w * f.h / 40;                   // h264
       }
   };

   struct Writer {
       std::string path;
       long bytes = 0;
       bool open(const std::string& p) { path = p; ++g_open; return !p.empty(); }
       void write(int n) { bytes += n; }
       void close() { --g_open; }
       void discard() { bytes = 0; }                 // 删掉写了一半的文件
   };

   // ---------- 模板方法 ----------
   class ExportJob {
   public:
       virtual ~ExportJob() = default;
       bool run(const std::string& in, const std::string& out) {   // 模板方法：公有、非虚，流程只写这一遍
           Demuxer dm;
           if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
           Decoder dec(media());
           Encoder enc{codec()};
           Writer w;
           if (!w.open(out)) { dm.close(); return false; }
           Packet pkt;
           Frame f;
           while (!done(enc.frames) && dm.read(pkt)) {   // 新增：问一下子类够了没有
               if (pkt.type != media()) continue;
               if (dec.decode(pkt, f)) encodeOne(f, enc, w);
           }
           while (!done(enc.frames) && dec.flush(f)) encodeOne(f, enc, w);
           w.close();
           dm.close();
           std::printf("读了 %3d 个包  ", dm.v + dm.a);
           onFinished(out, enc.frames, w.bytes);
           return true;
       }

   protected:
       virtual Media media() const = 0;                     // 基本操作：子类必须实现
       virtual const char* codec() const = 0;
       virtual bool process(Frame&) { return true; }        // 钩子：默认什么都不做；返回 false 丢掉这一帧
       virtual bool done(int /*framesEncoded*/) const { return false; }   // 新钩子：默认一直读到文件末尾
       virtual void onFinished(const std::string& out, int frames, long bytes) {   // 钩子：默认打印一行
           std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), frames, bytes);
       }

   private:
       void encodeOne(Frame& f, Encoder& enc, Writer& w) {
           if (process(f)) w.write(enc.encode(f));
       }
   };

   class Mp4Export : public ExportJob {
   protected:
       Media media() const override { return Media::Video; }
       const char* codec() const override { return "h264"; }
   };

   class GifExport : public ExportJob {
   protected:
       Media media() const override { return Media::Video; }
       const char* codec() const override { return "gif"; }
       bool process(Frame& f) override {
           if (f.index % 2 != 0) return false;              // 24 fps 降到 12 fps
           f.w = 480; f.h = 270;                            // 缩小
           return true;
       }
   };

   class WavExport : public ExportJob {
   protected:
       Media media() const override { return Media::Audio; }
       const char* codec() const override { return "pcm_s16le"; }
   };

   class CoverExport : public ExportJob {             // 新子类：第一帧存成 JPEG
   protected:
       Media media() const override { return Media::Video; }
       const char* codec() const override { return "mjpeg"; }
       bool done(int framesEncoded) const override { return framesEncoded >= 1; }
   };

   int main() {
       std::vector<std::pair<std::unique_ptr<ExportJob>, std::string>> jobs;
       jobs.emplace_back(std::make_unique<Mp4Export>(), "out.mp4");
       jobs.emplace_back(std::make_unique<GifExport>(), "out.gif");
       jobs.emplace_back(std::make_unique<WavExport>(), "out.wav");
       jobs.emplace_back(std::make_unique<CoverExport>(), "cover.jpg");
       for (auto& [job, out] : jobs) job->run("bbb-10s.mp4", out);
       std::printf("打开着的文件：%d\n", g_open);
   }
   ```

   ```
   读了 710 个包  out.mp4     240 帧   5529600 字节
   读了 710 个包  out.gif     120 帧   5184000 字节
   读了 710 个包  out.wav     470 帧   1925120 字节
   读了   7 个包  cover.jpg     1 帧    115200 字节
   打开着的文件：0
   ```

   封面只读了 7 个包：视频解码器有 2 帧延迟，要读进 3 个视频包才出第一帧，中间交错着 4 个音频包。如果没有这个钩子，`CoverExport` 只能在 `process()` 里丢掉后面的帧，710 个包照样全部读完、239 帧白白解码。这个练习也说明了模板方法的局限：**流程里没有预留的扩展点，子类是加不进去的**，只能改基类。好在改基类只加了一个有默认实现的钩子，已有的子类不受影响。

3. `run()` 是模板方法；测试体 `body()` 是基本操作（纯虚，每个测试必须写）；`setUp()`、`tearDown()` 是钩子（默认什么都不做，夹具类重写它们）。`check()` 不用异常，记下失败接着往下跑，所以 `tearDown()` 一定能执行到：

   ```cpp
   // 《模板方法模式》：练习 3——测试夹具：run() 固定是 setUp → 测试体 → tearDown，测试失败了 tearDown 也一定执行
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   int g_open = 0;                                  // 打开着的播放器个数，用来检查 tearDown 有没有漏

   struct Player {                                  // 被测的对象（模拟）
       double pos = 0;
       bool open(const std::string& path) { ++g_open; return path.size() > 4; }
       void close() { --g_open; }
       void seek(double t) { pos = t < 10 ? t : 10; }
   };

   class TestCase {
   public:
       explicit TestCase(std::string name) : name_(std::move(name)) {}
       virtual ~TestCase() = default;

       bool run() {                                 // 模板方法：流程固定，子类改不了
           failures_ = 0;
           if (setUp()) body();
           else check(false, "setUp 失败，跳过测试体");
           tearDown();                              // 不管 setUp、测试体成没成功都执行（和 GoogleTest 一样）
           std::printf("[%s] %s\n", failures_ == 0 ? "  OK  " : " FAIL ", name_.c_str());
           return failures_ == 0;
       }

   protected:
       virtual bool setUp() { return true; }        // 钩子
       virtual void tearDown() {}                   // 钩子
       virtual void body() = 0;                     // 基本操作：每个测试必须写

       void check(bool ok, const char* what) {      // 不用异常：记下失败，接着往下跑
           if (!ok) { ++failures_; std::printf("         检查失败：%s\n", what); }
       }

   private:
       std::string name_;
       int failures_ = 0;
   };

   class PlayerTest : public TestCase {             // 夹具：每个测试前打开文件，测试后关掉
   public:
       using TestCase::TestCase;

   protected:
       Player player;
       bool setUp() override { return player.open("bbb-10s.mp4"); }
       void tearDown() override { player.close(); }
   };

   class SeekInside : public PlayerTest {
   public:
       SeekInside() : PlayerTest("seek 到 5 秒") {}
   protected:
       void body() override { player.seek(5); check(player.pos == 5, "pos == 5"); }
   };

   class SeekPastEnd : public PlayerTest {
   public:
       SeekPastEnd() : PlayerTest("seek 超过结尾") {}
   protected:
       void body() override { player.seek(99); check(player.pos == 99, "pos == 99"); }   // 故意写错的期望值
   };

   class OpenBadFile : public PlayerTest {
   public:
       OpenBadFile() : PlayerTest("打开不存在的文件") {}
   protected:
       bool setUp() override { return player.open("x"); }   // 重写夹具：打开失败
       void body() override { check(player.pos == 0, "pos == 0"); }
   };

   int main() {
       std::vector<std::unique_ptr<TestCase>> tests;
       tests.push_back(std::make_unique<SeekInside>());
       tests.push_back(std::make_unique<SeekPastEnd>());
       tests.push_back(std::make_unique<OpenBadFile>());
       int passed = 0;
       for (auto& t : tests) passed += t->run();
       std::printf("通过 %d / %zu，打开着的播放器：%d\n", passed, tests.size(), g_open);
   }
   ```

   ```
   [  OK  ] seek 到 5 秒
            检查失败：pos == 99
   [ FAIL ] seek 超过结尾
            检查失败：setUp 失败，跳过测试体
   [ FAIL ] 打开不存在的文件
   通过 1 / 3，打开着的播放器：0
   ```

   「打开不存在的文件」这个测试里，`setUp()` 失败了，`tearDown()` 仍然执行，所以播放器没有泄漏。这和 GoogleTest 的规则一样：`SetUp()` 失败时跳过测试体，但仍然调 `TearDown()`。所以 `tearDown()` 要能处理「`setUp()` 只做了一半」的情况。如果测试会抛异常，`run()` 里要用 `try` / `catch` 或 RAII 保证 `tearDown()` 执行。

4. 基类的钩子是 `bool process(Frame&)`，子类写的是 `bool process(Frame&) const`，多了个 `const` 就是**另一个函数**，没有重写基类的钩子，只是把它隐藏了。`run()` 通过虚函数调到的还是基类的默认版本（原样保留），所以 GIF 没有隔帧、没有缩小：240 帧 × 1280 × 720 / 3 = 73728000 字节。

   开着 `-Wall` 时两个编译器其实都有提示：Clang 报 `'GifExport::process' hides overloaded virtual function [-Woverloaded-virtual]`，GCC 报 `'virtual bool ExportJob::process(Frame&)' was hidden [-Woverloaded-virtual=]`。但这只是警告，很容易被淹没。根治的办法是**重写时一律加 `override`**：`ex4_override_ce.cpp` 写成 `bool process(Frame& f) const override`，Clang 报 `non-virtual member function marked 'override' hides virtual member function`，GCC 报 `marked 'override', but does not override`，都是编译错误。正确的写法是去掉 `const`，和基类的签名一致（就是 `template_method.cpp` 里的样子）。

   如果 `process()` 是纯虚函数，`GifExport` 没有实现它，就还是抽象类，`std::make_unique<GifExport>()` 直接编译失败，问题马上暴露。这就是第 2.6 节说的取舍：钩子省事，但写错了不会报错；每个子类都必须回答的步骤，宁可做成纯虚函数。

</details>
