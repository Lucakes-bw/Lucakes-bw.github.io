/* 设计模式专栏《类型擦除》：Concept/Model 非侵入式包装进 vector<Drawable>，以及 SBO 小对象内联 vs 大对象落堆（dp-type-erasure） */
(function () {
  'use strict';

  function initState(m) {
    m.mode = 'basic'; // 'basic' | 'sbo'
    m.vec = [];       // vector<Drawable> 里的槽位
    m.heap = [];      // 堆上的对象
    m.backup = [];    // 拷贝出的副本（值语义演示）
    m.allocs = 0;
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];

    if (m.mode === 'basic') {
      texts.push({ id: 't0', x: 20, y: 20, text: 'std::vector<Drawable> scene（连续值数组，对外只见统一类型 Drawable）', cls: 'muted' });
      if (!m.vec.length) {
        boxes.push({ id: 'vempty', x: 20, y: 32, w: 690, h: 52, text: '（空容器）', hl: hl.vempty });
      } else {
        m.vec.forEach(function (item, i) {
          var id = 'v' + i;
          boxes.push({
            id: id, x: 20 + i * 234, y: 32, w: 222, h: 56,
            text: 'scene[' + i + '] : Drawable',
            sub: 'self_ → 0x' + item.addr + ' (8 B)',
            hl: hl[id]
          });
        });
      }

      texts.push({ id: 't1', x: 20, y: 120, text: '堆区：Model<T> 继承内部 Concept（VideoLayer / SubtitleLayer / Watermark 互无继承关系）', cls: 'muted' });
      m.heap.forEach(function (h, i) {
        var id = 'h' + i;
        boxes.push({
          id: id, x: 20 + i * 234, y: 132, w: 222, h: 66,
          text: 'Model<' + h.type + '>',
          sub: 'vptr(Concept) + ' + h.val,
          hl: hl[id]
        });
        if (i < m.vec.length) {
          arrows.push({ from: 'v' + i, to: id, hl: hl['a' + i] || '' });
        }
      });

      if (m.backup.length) {
        texts.push({ id: 't2', x: 20, y: 228, text: 'auto backup = scene;（值语义拷贝：通过 self_->clone() 独立复制堆上 Model<T>）', cls: 'muted' });
        m.backup.forEach(function (b, i) {
          var id = 'b' + i;
          boxes.push({
            id: id, x: 20 + i * 234, y: 240, w: 222, h: 54,
            text: 'backup[' + i + '] : ' + b.type,
            sub: '独立克隆副本 ' + b.val,
            hl: hl[id]
          });
        });
      }
    } else {
      texts.push({ id: 't0', x: 20, y: 20, text: 'std::vector<SboDrawable>（每槽 40 B = 32 B 栈内 buf_ + 8 B vptr_）   堆分配次数：' + m.allocs, cls: 'muted' });
      m.vec.forEach(function (item, i) {
        var id = 's' + i;
        boxes.push({
          id: id, x: 20 + i * 234, y: 34, w: 222, h: 92,
          text: item.type + ' (' + item.bytes + ' B)',
          sub: item.inline
            ? 'buf_[32]: 直接内联存储 | vptr_ → VTable<Small>'
            : 'buf_[0..7]: 存堆指针 0x' + item.addr + ' | vptr_ → VTable<Heap>',
          hl: hl[id]
        });
      });

      texts.push({ id: 't1', x: 20, y: 162, text: '堆区（仅当 sizeof(T) > 32 B 时自动退回堆分配）', cls: 'muted' });
      if (!m.heap.length) {
        boxes.push({ id: 'hempty', x: 20, y: 176, w: 690, h: 62, text: '堆区为空：小对象全部 placement new 在 buf_[32] 内，0 次 heap allocation！', hl: hl.hempty || 'ok' });
      } else {
        m.heap.forEach(function (h, i) {
          var id = 'sh' + i;
          boxes.push({
            id: id, x: 488, y: 176, w: 222, h: 68,
            text: 'Heap: ' + h.type + ' (' + h.bytes + ' B)',
            sub: h.val + '（超出 32 B 阈值）',
            hl: hl[id]
          });
          arrows.push({ from: 's' + h.slot, to: id, hl: hl.sa || 'warn', label: 'new T' });
        });
      }
    }

    if (x.note) texts.push({ id: 'note', x: 20, y: 340, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function basicFlow(m, p, rec) {
    initState(m);
    rec.step(1, '初始状态：VideoLayer、SubtitleLayer、Watermark 是三个毫无继承关系的独立结构体，均提供 draw()。', { hl: { vempty: 'cur' } });

    m.vec.push({ addr: '10A0' });
    m.heap.push({ type: 'VideoLayer', val: '{1920, 1080}' });
    rec.step(2, 'scene.push_back(VideoLayer{1920, 1080})：模板构造函数在堆上创建 Model<VideoLayer>，用 unique_ptr<const Concept> 擦除具体类型。', {
      hl: { v0: 'new', h0: 'new', a0: 'new' }
    });

    m.vec.push({ addr: '10E0' });
    m.heap.push({ type: 'SubtitleLayer', val: '{"Hello"}' });
    rec.step(2, 'scene.push_back(SubtitleLayer{"Hello"})：无需让 SubtitleLayer 继承任何基类，直接装入同一个 vector<Drawable>。', {
      hl: { v1: 'new', h1: 'new', a1: 'new' }
    });

    m.vec.push({ addr: '1140' });
    m.heap.push({ type: 'Watermark', val: '{"Lucky", 0.6}' });
    rec.step(2, 'scene.push_back(Watermark{"Lucky", 0.6})：三个第三方类型全部按值存入同一容器。', {
      hl: { v2: 'new', h2: 'new', a2: 'new' }
    });

    rec.step(4, '遍历渲染：for (const auto& layer : scene) layer.draw() —— 通过 Concept::doDraw() 虚派发到各自的 draw()。', {
      hl: { v0: 'ok', v1: 'ok', v2: 'ok', h0: 'ok', h1: 'ok', h2: 'ok' },
      note: '非侵入式 + 值语义：不改第三方源码，也不用暴露 unique_ptr<IDrawable>'
    });
  }

  function copyFlow(m, p, rec) {
    initState(m);
    m.vec = [{ addr: '10A0' }, { addr: '10E0' }, { addr: '1140' }];
    m.heap = [
      { type: 'VideoLayer', val: '{1920, 1080}' },
      { type: 'SubtitleLayer', val: '{"Hello"}' },
      { type: 'Watermark', val: '{"Lucky", 0.6}' }
    ];
    rec.step(3, '已有 3 层场景 scene，现在执行普通值拷贝：std::vector<Drawable> backup = scene;', {
      hl: { v0: 'cur', v1: 'cur', v2: 'cur' }
    });

    m.backup = [
      { type: 'VideoLayer', val: '{1920, 1080}' },
      { type: 'SubtitleLayer', val: '{"Hello"}' },
      { type: 'Watermark', val: '{"Lucky", 0.6}' }
    ];
    rec.step(3, 'Drawable 拷贝构造调用 other.self_->clone()，每个 Model<T> 自动复制一份底层 T，调用方无需手写 cloneScene()。', {
      hl: { b0: 'new', b1: 'new', b2: 'new' },
      note: '原容器与快照容器完全独立，修改 scene 不会影响 backup'
    });
  }

  function sboSmall(m, p, rec) {
    initState(m);
    m.mode = 'sbo';
    m.vec.push({ type: 'VideoLayer', bytes: 8, inline: true });
    rec.step(6, '放入 VideoLayer（8 B <= 32 B）：编译期 if constexpr 走 SBO 分支，直接 placement new 在 buf_[32] 内，0 次堆分配。', {
      hl: { s0: 'new', hempty: 'ok' }
    });

    m.vec.push({ type: 'Watermark', bytes: 16, inline: true });
    rec.step(6, '放入 Watermark（16 B <= 32 B）：同样直接内联存进 buf_[32]，vptr_ 指向手写静态 VTable 函数指针表。', {
      hl: { s1: 'new', hempty: 'ok' },
      note: '百万次构造+调用实测：SBO 比 Concept/Model 少 100 万次 new/delete，耗时从 19.48 ns 降至 10.44 ns'
    });
  }

  function sboOverflow(m, p, rec) {
    initState(m);
    m.mode = 'sbo';
    m.vec = [
      { type: 'VideoLayer', bytes: 8, inline: true },
      { type: 'Watermark', bytes: 16, inline: true }
    ];
    rec.step(6, '前两个小图层（8 B、16 B）都安静地躺在各自 32 B 栈内缓冲区里，堆分配次数 = 0。', {
      hl: { s0: 'ok', s1: 'ok', hempty: 'ok' }
    });

    m.vec.push({ type: 'LutOverlay', bytes: 64, inline: false, addr: '8F20' });
    m.heap.push({ slot: 2, type: 'LutOverlay', bytes: 64, val: 'float matrix[16]' });
    m.allocs = 1;
    rec.step(7, '放入 LutOverlay（64 B > 32 B）：编译期自动走落堆分支，堆上 new LutOverlay，buf_ 里只存 8 B 指针，调用方零感知！', {
      hl: { s2: 'warn', sh0: 'warn', sa: 'warn' },
      note: '小对象享受零分配与缓存局部性，大对象透明退回堆，兼顾性能与通用性'
    });
  }

  DSViz.register('dp-type-erasure', {
    title: '类型擦除：Concept/Model 非侵入式多态与 SBO 小对象内联布局',
    width: 730,
    height: 360,
    code: [
      'struct VideoLayer { void draw() const; };  // 第三方独立类型，不继承任何基类',
      'template <class T> Drawable(T x) : self_(make_unique<Model<T>>(move(x))) {}',
      'Drawable(const Drawable& o) : self_(o.self_->clone()) {}  // 值语义深拷贝',
      'void draw() const { self_->doDraw(); }  // 对外统一接口，对内虚派发',
      'alignas(max_align_t) byte buf_[32]; const VTable* vptr_;  // SBO 40 B 布局',
      'if constexpr (sizeof(T) <= 32) ::new (buf_) T(move(x));   // 小对象 0 次堆分配',
      'else ::new (buf_) T*(new T(move(x)));                     // 大对象自动落堆'
    ],
    init: function () { var m = {}; initState(m); return m; },
    view: view,
    start: function (m, rec) { basicFlow(m, {}, rec); },
    actions: [
      { label: 'Concept/Model 包装入队', run: basicFlow },
      { label: '值语义深拷贝', run: copyFlow },
      { label: 'SBO 小对象 0 次堆分配', run: sboSmall },
      { label: 'SBO 大对象自动落堆', run: sboOverflow }
    ]
  });
})();
