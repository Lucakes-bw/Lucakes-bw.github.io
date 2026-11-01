---
title: 滤镜：libavfilter
date: 2026-11-01 14:00:00
updated: 2026-11-01 14:00:00
categories:
  - 音视频开发
  - FFmpeg编程
tags:
  - 音视频
  - FFmpeg
  - 滤镜
  - C
order: 6
description: libavfilter 把视频处理写成一张图：入口 buffer 接收解码出的帧，出口 buffersink 交出处理好的帧，中间用一行文字描述，和命令行 -vf 的写法完全一样。用 C 搭起滤镜图，scale=640:360 的结果和命令行逐字节相同；再给测试视频加上自己画的半透明徽标、只让第 2～5 秒变成黑白、把画面左半边镜像到右边、做边缘检测。滤镜图会自动插入格式转换：加水印时多了一个 scale，边缘检测时多了两个；边缘检测没有优化，处理 240 帧用了 3.1 秒，是其他滤镜的 6 倍。忘了告诉入口帧的色彩空间，FFmpeg 会警告「frame properties changed on the fly」。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解滤镜图的结构：滤镜、连接、入口和出口；会用 C 搭建一个视频滤镜图，用 `av_buffersrc_add_frame` 送入帧、`av_buffersink_get_frame` 取出帧；会写滤镜图的文字描述，包括多个输入输出、标签和时间线；知道滤镜图会自动插入格式转换。
>
> **前置知识**：本章前五篇，尤其是《解封装与解码》和《像素格式转换与重采样》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2。

上一篇用 libswscale 做缩放，要自己创建上下文、准备目标内存、调用转换函数。如果还想加水印、裁剪、调色、加淡入淡出……每一种都这样自己写就太累了。FFmpeg 的 libavfilter 提供了几百个现成的**滤镜**，并且可以把它们像管道一样连起来，组成**滤镜图**（filter graph）。命令行中的 `-vf scale=640:360`、`-filter_complex` 用的就是它。

## 1. 滤镜图

### 1.1 结构

一个滤镜有若干个输入端口和输出端口，滤镜之间用连线连起来，帧沿着连线从一个滤镜流向下一个。用 C 程序驱动滤镜图时，还需要两个特殊的滤镜：

- **buffer**：入口，程序把解码出的帧送进来；
- **buffersink**：出口，程序从这里取出处理好的帧。

下面是第 3 节加水印的滤镜图：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="加水印的滤镜图：buffer 入口接收解码出的帧，movie 滤镜读入 logo.png，经过自动插入的 scale 转换格式，两路一起进入 overlay 叠加，结果送到 buffersink 出口">
  <defs>
    <marker id="fg-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="10" y="30" width="110" height="40" rx="6" style="fill:var(--mark)"/>
      <rect x="10" y="110" width="110" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="170" y="110" width="140" height="40" rx="6" style="fill:var(--panel-2);stroke-dasharray:4 3"/>
      <rect x="370" y="60" width="120" height="50" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="550" y="65" width="120" height="40" rx="6" style="fill:var(--mark)"/>
    </g>
    <text x="22" y="47" style="fill:var(--fg)">buffer</text><text x="22" y="62" style="fill:var(--muted);font-size:10px">入口 [in]</text>
    <text x="22" y="127" style="fill:var(--fg)">movie</text><text x="22" y="142" style="fill:var(--muted);font-size:10px">logo.png，rgba</text>
    <text x="180" y="127" style="fill:var(--fg)">scale</text><text x="180" y="142" style="fill:var(--muted);font-size:10px">自动插入：转换格式</text>
    <text x="382" y="82" style="fill:var(--fg)">overlay</text><text x="382" y="98" style="fill:var(--muted);font-size:10px">W-w-24:24</text>
    <text x="562" y="82" style="fill:var(--fg)">buffersink</text><text x="562" y="97" style="fill:var(--muted);font-size:10px">出口 [out]</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#fg-arrow)">
      <path d="M120,50 L340,50 L368,72"/>
      <line x1="120" y1="130" x2="168" y2="130"/>
      <path d="M310,130 L340,130 L368,100"/>
      <line x1="490" y1="85" x2="548" y2="85"/>
    </g>
    <text x="200" y="44" style="fill:var(--muted);font-size:10px">视频帧（主画面）</text>
    <text x="10" y="180" style="fill:var(--muted);font-size:11px">描述：movie=logo.png[wm];[in][wm]overlay=W-w-24:24[out]</text>
  </g>
</svg>
</figure>

### 1.2 文字描述

滤镜图用一行文字描述，规则很简单：

- 一个滤镜写成 `名字=参数`，多个参数用冒号分隔：`scale=640:360`、`overlay=x=10:y=20`；
- 用逗号连起来的滤镜，前一个的输出接到后一个的输入：`crop=640:360:0:0,hflip`；
- 用分号隔开不相连的几条链；
- 方括号中的是**标签**，同名的标签连在一起：`[a]`、`[wm]`。程序中把入口叫 `[in]`、出口叫 `[out]`，没写标签的第一个输入、最后一个输出也会自动连到它们。

这和命令行 `-vf`、`-filter_complex` 的写法完全相同，命令行中试好的描述可以直接拿到程序里用。`ffmpeg -filters` 列出所有的滤镜，`ffmpeg -h filter=overlay` 查看某个滤镜的参数。

## 2. 程序

```c
// ff_filter.c：解码视频，经过一个滤镜图处理，把结果存成 YUV420P
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>

static AVFilterContext *src_ctx, *sink_ctx;
static long frames_out = 0;

// 从滤镜图的出口取出所有处理好的帧，写到文件
static void pull(AVFrame *f, FILE *out)
{
    while (av_buffersink_get_frame(sink_ctx, f) >= 0) {
        if (frames_out == 0) printf("输出：%dx%d\n", f->width, f->height);
        for (int p = 0; p < 3; p++) {
            int w = p ? f->width / 2 : f->width, h = p ? f->height / 2 : f->height;
            for (int y = 0; y < h; y++) fwrite(f->data[p] + (long)y * f->linesize[p], 1, w, out);
        }
        frames_out++;
        av_frame_unref(f);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 4) { fprintf(stderr, "用法: %s 输入 输出.yuv \"滤镜描述\"\n", argv[0]); return 1; }
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    const AVCodec *codec = NULL;
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    AVStream *st = fmt->streams[vs];
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->pkt_timebase = st->time_base;
    avcodec_open2(dec, codec, NULL);

    // 1. 滤镜图的入口 buffer：告诉它进来的帧是什么样的
    AVFilterGraph *graph = avfilter_graph_alloc();
    char args[256];
    snprintf(args, sizeof args, "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1:colorspace=%d:range=%d",
             dec->width, dec->height, dec->pix_fmt, st->time_base.num, st->time_base.den,
             dec->colorspace, dec->color_range);      // 色彩空间和范围也要说明，否则 scale 等滤镜只能去猜
    avfilter_graph_create_filter(&src_ctx, avfilter_get_by_name("buffer"), "in", args, NULL, graph);
    // 2. 出口 buffersink：要求输出 yuv420p
    sink_ctx = avfilter_graph_alloc_filter(graph, avfilter_get_by_name("buffersink"), "out");
    av_opt_set(sink_ctx, "pixel_formats", "yuv420p", AV_OPT_SEARCH_CHILDREN);
    avfilter_init_dict(sink_ctx, NULL);

    // 3. 中间的部分用一行文字描述，标签 [in]、[out] 连到入口和出口
    AVFilterInOut *outputs = avfilter_inout_alloc(), *inputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");  outputs->filter_ctx = src_ctx;  outputs->pad_idx = 0; outputs->next = NULL;
    inputs->name = av_strdup("out");  inputs->filter_ctx = sink_ctx;  inputs->pad_idx = 0;  inputs->next = NULL;
    int ret = avfilter_graph_parse_ptr(graph, argv[3], &inputs, &outputs, NULL);
    if (ret >= 0) ret = avfilter_graph_config(graph, NULL);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) { fprintf(stderr, "滤镜图有误：%s\n", av_err2str(ret)); return 1; }
    printf("滤镜图中共有 %u 个滤镜：", graph->nb_filters);
    for (unsigned i = 0; i < graph->nb_filters; i++) printf("%s ", graph->filters[i]->filter->name);
    printf("\n");

    FILE *out = fopen(argv[2], "wb");
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc(), *filt = av_frame_alloc();
    int64_t t0 = av_gettime_relative();
    int eof = 0;
    while (!eof) {
        if (av_read_frame(fmt, pkt) < 0) { avcodec_send_packet(dec, NULL); eof = 1; }
        else if (pkt->stream_index != vs) { av_packet_unref(pkt); continue; }
        else { avcodec_send_packet(dec, pkt); av_packet_unref(pkt); }
        while (avcodec_receive_frame(dec, frame) >= 0) {
            if (av_buffersrc_add_frame(src_ctx, frame) < 0) break;   // 解码出的帧送进滤镜图（接管 frame 的数据）
            pull(filt, out);
        }
    }
    if (av_buffersrc_add_frame(src_ctx, NULL) >= 0)   // 冲刷滤镜图
        pull(filt, out);
    printf("%ld 帧，用时 %.2f 秒\n", frames_out, (av_gettime_relative() - t0) / 1e6);

    fclose(out);
    av_frame_free(&frame); av_frame_free(&filt); av_packet_free(&pkt);
    avfilter_graph_free(&graph);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return 0;
}
```

- **入口 buffer** 需要知道进来的帧是什么样的：尺寸、像素格式、时间基、像素宽高比，以及色彩空间和范围。用一个和滤镜描述同样格式的参数字符串传给它。
- **出口 buffersink** 可以限制输出的格式。这里要求 `yuv420p`，如果中间的滤镜输出了别的格式，滤镜图会自动在最后插入转换。
- **`avfilter_graph_parse_ptr`** 解析文字描述，创建中间所有的滤镜，并把 `[in]`、`[out]` 连到入口和出口；**`avfilter_graph_config`** 检查连接，协商每条连线上的像素格式，必要时插入转换滤镜。
- 之后的循环和第 2 篇的解码程序一样，只是解码出的帧不直接写文件，而是用 `av_buffersrc_add_frame` 送进滤镜图，再用 `av_buffersink_get_frame` 取出。滤镜也可能有延迟（比如要看前后几帧的去噪滤镜），所以同样是「送一帧，取到 EAGAIN 为止」，最后送 `NULL` 冲刷。

## 3. 几个例子

### 3.1 缩放

```bash
FF=$(brew --prefix ffmpeg)
clang -O2 ff_filter.c -o ff_filter -I$FF/include -L$FF/lib -lavformat -lavcodec -lavfilter -lavutil
./ff_filter bbb-10s.mp4 f1.yuv "scale=640:360"
ffmpeg -i bbb-10s.mp4 -vf scale=640:360 -f rawvideo -pix_fmt yuv420p cli_f1.yuv
md5 -q f1.yuv cli_f1.yuv
```

```
滤镜图中共有 3 个滤镜：buffer buffersink scale
输出：640x360
240 帧，用时 0.36 秒
7cf038f75ac0231b709cea8f277ff1ee
7cf038f75ac0231b709cea8f277ff1ee
```

和命令行逐字节相同。`scale` 滤镜内部就是上一篇的 libswscale，而且它会根据帧上的色彩信息自动选择 BT.709，不需要自己调用 `sws_setColorspaceDetails`。

### 3.2 加水印

水印用的徽标是我画的：一个 120×120 的 PNG，半透明的白色圆形，中间是本专栏主题色的播放三角形，带有透明通道（RGBA）。`movie` 滤镜可以在滤镜图内部读入一个文件，`overlay` 把第二个输入叠加到第一个输入上，`W`、`H` 是主画面的宽高，`w`、`h` 是叠加物的宽高：

```bash
./ff_filter bbb-10s.mp4 wm.yuv "movie=logo.png[wm];[in][wm]overlay=W-w-24:24[out]"
```

```
滤镜图中共有 5 个滤镜：buffer buffersink movie overlay scale
输出：1280x720
240 帧，用时 0.50 秒
```

<img src="/img/av/ff-filter-wm.jpg" alt="右上角叠加了半透明的圆形徽标，中间是紫色的播放三角形" width="640">

描述中只写了 `movie` 和 `overlay`，滤镜图里却有一个 `scale`：徽标是 RGBA，主画面是 YUV420P，`overlay` 要求两路输入格式一致，`avfilter_graph_config` 在协商格式时**自动插入**了一个 `scale` 来转换徽标的格式。透明通道也保留了下来，徽标的白色圆形是半透明的，能隐约看到下面的草地。

### 3.3 时间线：只在一段时间内生效

很多滤镜支持 `enable` 选项，用一个表达式决定哪些帧要处理，`t` 是当前帧的时间（秒）。让画面只在第 2～5 秒变成黑白（`eq` 滤镜调整饱和度）：

```bash
./ff_filter bbb-10s.mp4 gray.yuv "eq=saturation=0:enable='between(t,2,5)'"
```

<figure>
<img src="/img/av/ff-filter-gray.jpg" alt="左边是第 1.67 秒的画面，彩色；右边是第 2.5 秒的画面，黑白" width="640">
<figcaption>左：第 40 帧（1.67 秒），彩色；右：第 60 帧（2.5 秒），黑白</figcaption>
</figure>

这个表达式要用到帧的时间戳，所以入口 buffer 的 `time_base` 必须设对，帧的 `pts` 也要正确。

### 3.4 多路分支：镜像

`split` 把一路输入复制成两路，两路分别处理后再用 `hstack` 并排拼起来。左半边保持原样，右半边是左半边的镜像：

```bash
./ff_filter bbb-10s.mp4 mirror.yuv "split[a][b];[a]crop=iw/2:ih:0:0[l];[b]crop=iw/2:ih:0:0,hflip[r];[l][r]hstack"
```

```
滤镜图中共有 7 个滤镜：buffer buffersink split crop crop hflip hstack
```

<img src="/img/av/ff-filter-mirror.jpg" alt="画面左右对称：兔子和蝴蝶都被镜像了一份" width="640">

`iw`、`ih` 是输入的宽高。`crop` 和 `hflip` 都不复制像素数据，只是调整数据指针和 linesize（`hflip` 用负的 linesize 实现左右翻转），所以这条链很快。

### 3.5 边缘检测

```bash
./ff_filter bbb-10s.mp4 edge.yuv "edgedetect=low=0.1:high=0.3"
```

```
滤镜图中共有 5 个滤镜：buffer buffersink edgedetect scale scale
输出：1280x720
240 帧，用时 3.12 秒
```

<img src="/img/av/ff-filter-edge.jpg" alt="边缘检测的结果：黑色背景上白色的轮廓线，能认出兔子、蝴蝶和花" width="640">

这次自动插入了两个 `scale`：`edgedetect` 只处理灰度图，输入要先从 YUV420P 转成灰度，它的输出再转回出口要求的 YUV420P。

### 3.6 速度

| 滤镜图 | 240 帧用时 |
|---|---|
| `null`（什么也不做，只有解码） | 0.29 秒 |
| `scale=640:360` | 0.36 秒 |
| 加水印 | 0.50 秒 |
| 2～5 秒黑白 | 0.53 秒 |
| 镜像 | 0.56 秒 |
| 边缘检测 | **3.12 秒** |

（用时包括解码和写文件，`null` 滤镜原样输出，作为基准。）

大部分滤镜在解码时间之外只多花零点几秒，边缘检测却多了 2.8 秒，是其他滤镜的好几倍。`scale`、`overlay` 这些常用的滤镜有 SIMD 优化，而 `edgedetect` 是逐像素计算梯度的普通 C 代码，没有优化。用滤镜做实时处理之前，最好像这样先量一量。

## 4. 色彩信息

第一版程序的入口参数中没有写 `colorspace` 和 `range`，运行时 FFmpeg 给出了警告：

```
[in @ 0x74930046c0] Changing video frame properties on the fly is not supported by all filters.
[in @ 0x74930046c0] filter context - w: 1280 h: 720 fmt: 0 csp: unknown range: unknown alpha: unspecified,
                    incoming frame - w: 1280 h: 720 fmt: 0 csp: bt709 range: tv alpha: unspecified pts_time: 0
```

入口以为进来的帧是「色彩空间未知、范围未知」，实际送进来的帧却是 BT.709、有限范围（tv），它认为帧的属性在中途变了。有些滤镜能处理这种变化，有些不能；即使能处理，滤镜图在配置阶段做出的决定（比如自动插入的转换用什么系数）也是按「未知」做的。把这两项写进入口参数，警告就消失了。

这和上一篇 libswscale 默认按 BT.601 处理是同一类问题：**色彩信息要一路传下去**，解码器 → 滤镜 → 编码器 → 容器，每一个环节都要知道。漏掉一处，颜色就可能偏。

## 5. 小结

- 滤镜图：入口 buffer、出口 buffersink，中间的滤镜用一行文字描述，写法和命令行的 `-vf`、`-filter_complex` 相同。
- 用 `avfilter_graph_parse_ptr` 解析描述、`avfilter_graph_config` 配置；`av_buffersrc_add_frame` 送入帧，`av_buffersink_get_frame` 取出帧，最后送 `NULL` 冲刷。
- 描述中，逗号连接滤镜，分号分隔链，方括号是标签；`split` 分出多路，`overlay`、`hstack` 合并多路。
- 滤镜图会协商格式，必要时自动插入 `scale` 等转换滤镜。
- `enable` 时间线选项让滤镜只在一段时间内生效。
- 入口要写清帧的尺寸、格式、时间基、色彩空间和范围。

第 3 章到这里结束。我们已经能用 FFmpeg 的库完成解封装、解码、编码、封装、缩放、重采样和滤镜处理，命令行 `ffmpeg` 能做的事情，基本都能自己用代码做了。下一章把解码出来的画面和声音真正地播放出来：写一个播放器。

## 练习

1. 把水印放到左下角，并且只在前 3 秒显示。滤镜描述怎么写？处理后有多少帧带有水印？
2. 第 3.4 节的镜像中，如果把 `split` 去掉，直接写 `[in]crop=iw/2:ih:0:0[l];[in]crop=iw/2:ih:0:0,hflip[r];[l][r]hstack`，会怎样？

<details>
<summary>点击查看答案</summary>

1. 描述：

   ```
   movie=logo.png[wm];[in][wm]overlay=24:H-h-24:enable='lt(t,3)'[out]
   ```

   `24:H-h-24` 是左边距 24、下边距 24 的位置；`lt(t,3)` 表示时间小于 3 秒。逐帧和原始的 `bbb.yuv` 比较，有 72 帧不同，正好是第 0～71 帧：24 帧/秒 × 3 秒 = 72 帧。

2. 配置失败：

   ```
   Input pad "default" with type video of the filter instance "Parsed_crop_1" of crop not connected to any source
   滤镜图有误：Invalid argument
   ```

   一个输出只能连到一个输入：入口 `[in]` 被第一个 `crop` 用掉了，第二个 `crop` 的输入就没有连到任何地方。滤镜图中的一条连线只能有一个起点和一个终点，一路帧要给两个滤镜用，就必须先用 `split`（视频）或 `asplit`（音频）复制成两路。`split` 并不复制像素数据，两路输出引用的是同一块内存（引用计数），只有当后面的滤镜需要修改数据时才会复制。

</details>
