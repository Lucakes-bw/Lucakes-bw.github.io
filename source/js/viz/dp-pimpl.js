/* 设计模式专栏《Pimpl 惯用法》：对比普通头文件改私有成员触发级联重编 + ABI 破坏 vs Pimpl 编译防火墙隔离依赖、ABI 恒为 8 B（dp-pimpl） */
(function () {
  'use strict';

  var CALLERS = ['ui_bar.cpp', 'playlist.cpp', 'subtitle.cpp', 'danmaku.cpp', 'analytics.cpp'];

  function initState(m) {
    m.mode = 'raw';           // 'raw' | 'pimpl' | 'mem'
    m.headerText = '';
    m.headerSub = '';
    m.implText = '';
    m.implSub = '';
    m.sizeofVal = '280 B';
    m.rebuilt = {};           // map of id -> status ('idle' | 'dirty' | 'cached')
    m.memKind = 'classic';    // 'classic' | 'fast'
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];

    if (m.mode === 'mem') {
      texts.push({ id: 'mt', x: 20, y: 20, text: m.memKind === 'classic' ? '经典 Pimpl：栈上 8 B 指针 + 堆上 Impl（1 次 new/delete）' : 'Fast Pimpl：栈上 64 B 对齐缓冲区 + placement new（0 次堆分配）', cls: 'muted' });
      if (m.memKind === 'classic') {
        boxes.push({ id: 'stack', x: 30, y: 55, w: 220, h: 96, text: '栈上 VideoPlayer（8 B）', sub: 'unique_ptr<Impl> pImpl_', hl: hl.stack });
        boxes.push({ id: 'heap', x: 380, y: 45, w: 310, h: 118, text: '堆上 VideoPlayer::Impl（280+ B）', sub: 'DecoderContext / RenderPipeline / Net / pos', hl: hl.heap });
        arrows.push({ from: 'stack', to: 'heap', hl: hl.arrow || 'cur', label: 'pImpl_->pos' });
        boxes.push({ id: 'cost', x: 30, y: 196, w: 660, h: 64, text: '实测（Clang -O2）：构造+析构 15.50 ns/op（含堆分配） · sizeof 恒定 8 B', sub: 'ABI 100% 稳定，增删 Impl 字段零影响调用方；适合中/长生命周期重型对象', hl: hl.cost });
      } else {
        boxes.push({ id: 'stack', x: 30, y: 55, w: 660, h: 108, text: '栈上 FastPlayer（alignas(max_align_t) std::byte storage_[64]）', sub: '::new (storage_) Impl(...) 原地构造，无堆分配、无指针跳转', hl: hl.stack });
        boxes.push({ id: 'cost', x: 30, y: 196, w: 660, h: 64, text: '实测（Clang -O2）：构造+析构 2.04 ns/op（比堆 Pimpl 快 7.6×）', sub: '代价：sizeof 固定为 64 B，Impl 超过上限触发 static_assert（需改缓冲区并重编）', hl: hl.cost });
      }
      if (x.note) texts.push({ id: 'note', x: 20, y: 344, text: x.note, cls: 'muted' });
      return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
    }

    var isPimpl = m.mode === 'pimpl';
    texts.push({
      id: 't1', x: 20, y: 20,
      text: isPimpl ? 'Pimpl 架构：VideoPlayer.h 是编译防火墙，重型依赖全关在 .cpp' : '普通架构：VideoPlayer.h 直接暴露私有成员与重型依赖',
      cls: 'muted'
    });

    boxes.push({
      id: 'hdr', x: 20, y: 34, w: 320, h: 74,
      text: m.headerText, sub: m.headerSub, hl: hl.hdr
    });

    if (isPimpl) {
      boxes.push({
        id: 'wall', x: 352, y: 34, w: 28, h: 74,
        text: '防', sub: '火', hl: hl.wall || 'ok'
      });
    }

    boxes.push({
      id: 'impl', x: isPimpl ? 392 : 390, y: 34, w: 318, h: 74,
      text: m.implText, sub: m.implSub, hl: hl.impl
    });

    arrows.push({ from: 'impl', to: 'hdr', hl: hl.ih || 'cur', label: isPimpl ? '隔离' : '包含' });

    texts.push({
      id: 't2', x: 20, y: 138,
      text: '5 个业务调用方模块（均 #include "VideoPlayer.h"）  |  sizeof(VideoPlayer) = ' + m.sizeofVal,
      cls: 'muted'
    });

    CALLERS.forEach(function (name, i) {
      var col = i < 3 ? i : i - 3;
      var row = i < 3 ? 0 : 1;
      var bx = 20 + col * 234;
      var by = 152 + row * 72;
      var st = m.rebuilt['c' + i] || 'idle';
      var sub = st === 'dirty' ? '⚠ 必须重新编译！' : (st === 'cached' ? '✓ .o 直接复用（免重编）' : '#include "VideoPlayer.h"');
      var hStyle = hl['c' + i] || (st === 'dirty' ? 'warn' : (st === 'cached' ? 'ok' : undefined));
      boxes.push({ id: 'c' + i, x: bx, y: by, w: 220, h: 56, text: name, sub: sub, hl: hStyle });
    });

    boxes.push({
      id: 'abi', x: 488, y: 224, w: 220, h: 56,
      text: 'ABI 布局：' + m.sizeofVal,
      sub: isPimpl ? '恒定 8 B，二进制兼容' : (m.sizeofVal === '288 B' ? '⚠ 大小改变，ABI 破坏！' : '暴露全部私有字段'),
      hl: hl.abi
    });

    if (x.note) texts.push({ id: 'note', x: 20, y: 344, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function rawRebuild(m, p, rec) {
    initState(m);
    m.mode = 'raw';
    m.headerText = 'VideoPlayer.h（#include heavy_deps.h）';
    m.headerSub = 'private: Decoder, Render, Net, pos, playing';
    m.implText = 'VideoPlayer.cpp';
    m.implSub = '实现成员函数（共 7 个 .cpp）';
    m.sizeofVal = '280 B';
    rec.step(1, '普通头文件：VideoPlayer.h 直接包含解码/渲染/网络重型头文件，5 个业务模块被迫传递展开所有重型依赖。', {
      hl: { hdr: 'cur', abi: 'cur' }
    });

    m.headerSub = 'private: ..., int retryCount_ = 3; // 新增！';
    m.sizeofVal = '288 B';
    rec.step(2, '需求迭代：仅在 private 区新增一个字段 int retryCount_ = 3。虽然公有接口没变，但 VideoPlayer.h 文件被改动了！', {
      hl: { hdr: 'warn', abi: 'warn' }
    });

    CALLERS.forEach(function (_, i) { m.rebuilt['c' + i] = 'dirty'; });
    rec.step(3, '级联重编 + ABI 破坏：7/7 个 .cpp 全部重编（实测 1598.4 ms）；且 sizeof 从 280 B 变成 288 B，旧动态库调用方会直接踩内存崩溃！', {
      hl: { hdr: 'warn', impl: 'warn', abi: 'warn' },
      note: '哪怕只改一个 private 字段，所有 #include 该头文件的模块都要跟着重编'
    });
  }

  function pimplRebuild(m, p, rec) {
    initState(m);
    m.mode = 'pimpl';
    m.headerText = 'VideoPlayer.h（仅 <memory>/<string>）';
    m.headerSub = 'struct Impl; unique_ptr<Impl> pImpl_;';
    m.implText = 'VideoPlayer.cpp（#include heavy_deps.h）';
    m.implSub = 'struct Impl { Decoder, Render, Net, pos };';
    m.sizeofVal = '8 B';
    rec.step(4, 'Pimpl 编译防火墙：VideoPlayer.h 只前向声明 struct Impl; 并持有一个 8 B 的 unique_ptr，5 个调用方零重型依赖。', {
      hl: { hdr: 'ok', wall: 'ok', abi: 'ok' }
    });

    m.implSub = 'struct Impl { ..., int retryCount = 3; }; // 仅改 .cpp';
    rec.step(5, '需求迭代：在 VideoPlayer.cpp 的 Impl 里新增 int retryCount = 3。公开头文件 VideoPlayer.h 一字节未动！', {
      hl: { hdr: 'ok', wall: 'ok', impl: 'new', abi: 'ok' }
    });

    CALLERS.forEach(function (_, i) { m.rebuilt['c' + i] = 'cached'; });
    rec.step(6, '增量构建只重编 1/7 个文件（VideoPlayer.cpp，实测 451.7 ms，快 3.5×）；5 个业务模块 .o 全部复用，sizeof 恒定 8 B，ABI 100% 兼容！', {
      hl: { hdr: 'ok', wall: 'ok', impl: 'new', abi: 'ok' },
      note: '直接替换新版 libplayer.so / .dylib，业务程序无需重新编译即可运行'
    });
  }

  function memCompare(m, p, rec) {
    initState(m);
    m.mode = 'mem';
    m.memKind = 'classic';
    rec.step(7, '经典 Pimpl：对象本身在栈上仅占 8 B（一个 unique_ptr），真实状态 Impl 分配在堆上——多一次 new/delete 和一次指针跳转。', {
      hl: { stack: 'cur', heap: 'cmp', cost: 'cur' }
    });

    m.memKind = 'fast';
    rec.step(8, 'Fast Pimpl：在头文件中预留 alignas(max_align_t) std::byte storage_[64]，在 .cpp 用 placement new 原地构造，彻底消除堆分配！', {
      hl: { stack: 'ok', cost: 'ok' },
      note: '热路径构造+析构从 15.50 ns 降至 2.04 ns（快 7.6×），但缓冲区大小写死在 ABI 中'
    });
  }

  DSViz.register('dp-pimpl', {
    title: 'Pimpl 惯用法：编译防火墙与 ABI 稳定对比',
    width: 730,
    height: 360,
    code: [
      '// 普通头文件：#include "heavy_deps.h"（暴露 Decoder/Render/Net 私有成员）',
      '  int retryCount_ = 3;  // 改 1 个 private 字段 → 头文件时间戳变更、sizeof 280→288 B',
      '  // 触发 7/7 个 .cpp 级联重编（1598.4 ms），旧调用方 ABI 失效',
      '// Pimpl 头文件：struct Impl; std::unique_ptr<Impl> pImpl_;（仅 <memory>/<string>）',
      'struct VideoPlayer::Impl { ..., int retryCount = 3; };  // 仅修改 VideoPlayer.cpp',
      '  // 增量构建仅重编 1/7 文件（451.7 ms，快 3.5×），sizeof 恒为 8 B，ABI 稳定',
      'VideoPlayer::VideoPlayer(string u) : pImpl_(make_unique<Impl>(move(u))) {} // 堆 Pimpl',
      '::new (storage_) Impl(move(u));  // Fast Pimpl：栈上 placement new，零堆分配'
    ],
    init: function () { var m = {}; initState(m); return m; },
    view: view,
    start: function (m, rec) { pimplRebuild(m, {}, rec); },
    actions: [
      { label: 'Pimpl：仅重编 1 个文件', run: pimplRebuild },
      { label: '普通头文件：级联重编 + 破 ABI', run: rawRebuild },
      { label: '经典 Pimpl vs Fast Pimpl', run: memCompare }
    ]
  });
})();
