/* 设计模式专栏《CRTP 奇异递归模板》：对比「虚函数运行时 vptr→vtable 跳转」vs「CRTP 编译期 static_cast 内联展开」及 Counted<T> 独立静态计数器布局（dp-crtp） */
(function () {
  'use strict';

  function initState(m) {
    m.mode = 'virtual'; // 'virtual' | 'crtp' | 'counted'
    m.stepTag = '';
    m.videoAlive = 0;
    m.audioAlive = 0;
    m.vObjs = [];
    m.aObjs = [];
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];

    if (m.mode === 'virtual') {
      texts.push({ id: 'th', x: 20, y: 20, text: '运行时多态：每个采样点都要经 vptr → vtable → 间接跳转（无法内联/向量化）', cls: 'muted' });

      boxes.push({
        id: 'caller', x: 20, y: 40, w: 205, h: 74,
        text: 'VirtualFilter::processBlock()',
        sub: 'for (i = 0; i < n; ++i)\n  y = this->applySample(s);',
        hl: hl.caller
      });

      boxes.push({
        id: 'obj', x: 265, y: 40, w: 195, h: 74,
        text: 'GainFilter 对象（16 B）',
        sub: '[0..7] vptr  |  [8..11] gain_=1.25',
        hl: hl.obj
      });

      boxes.push({
        id: 'vtbl', x: 500, y: 40, w: 210, h: 74,
        text: 'GainFilter vtable（只读段）',
        sub: '[0] ~GainFilter\n[1] &GainFilter::applySample',
        hl: hl.vtbl
      });

      boxes.push({
        id: 'impl', x: 265, y: 162, w: 445, h: 68,
        text: 'GainFilter::applySample(float x)  [独立函数体]',
        sub: 'return x * this->gain_;  // 每次循环跨调用边界，阻断 SIMD 向量化',
        hl: hl.impl
      });

      boxes.push({
        id: 'cost', x: 20, y: 162, w: 205, h: 68,
        text: '1000 万采样点实测',
        sub: '10.19 ms（1.02 ns/sample）\nsizeof = 16 B（含 8B vptr）',
        hl: hl.cost
      });
    } else if (m.mode === 'crtp') {
      texts.push({ id: 'th', x: 20, y: 20, text: 'CRTP 静态多态：编译期实例化 FilterBase<GainFilter>，直接内联并触发 SIMD 向量化', cls: 'muted' });

      boxes.push({
        id: 'caller', x: 20, y: 40, w: 225, h: 74,
        text: 'FilterBase<GainFilter>',
        sub: 'static_cast<const GainFilter*>(this)\n  ->applySample(pcm[i]);',
        hl: hl.caller
      });

      boxes.push({
        id: 'obj', x: 280, y: 40, w: 190, h: 74,
        text: 'CrtpGain 对象（4 B）',
        sub: '[0..3] gain_=1.25（零 vptr！）',
        hl: hl.obj
      });

      boxes.push({
        id: 'inline', x: 505, y: 40, w: 205, h: 74,
        text: '编译期直接展开（Inline）',
        sub: '无 vtable，无间接跳转\n直接替换成 pcm[i] * gain_',
        hl: hl.inline
      });

      boxes.push({
        id: 'simd', x: 280, y: 162, w: 430, h: 68,
        text: '编译器自动生成 NEON / AVX 向量指令',
        sub: '单条指令并行算 4 个 float：fmul v0.4s, v0.4s, v1.4s + clamp',
        hl: hl.simd
      });

      boxes.push({
        id: 'cost', x: 20, y: 162, w: 225, h: 68,
        text: '1000 万采样点实测',
        sub: '1.00 ms（0.10 ns/sample，快 10.2×）\nsizeof = 4 B（体积降至 25%）',
        hl: hl.cost
      });
    } else {
      texts.push({ id: 'th', x: 20, y: 20, text: 'Mixin 内存布局：Counted<VideoClip> 与 Counted<AudioClip> 是两个互不相干的基类特化', cls: 'muted' });

      boxes.push({
        id: 'cv', x: 20, y: 42, w: 325, h: 66,
        text: 'Counted<VideoClip>  [静态数据区]',
        sub: 'inline static int alive_ = ' + m.videoAlive + '  （独立地址）',
        hl: hl.cv
      });

      boxes.push({
        id: 'ca', x: 385, y: 42, w: 325, h: 66,
        text: 'Counted<AudioClip>  [静态数据区]',
        sub: 'inline static int alive_ = ' + m.audioAlive + '  （独立地址）',
        hl: hl.ca
      });

      texts.push({ id: 'tv', x: 20, y: 132, text: 'VideoClip 实例（EBO 空基类优化，0 额外字节）', cls: 'muted' });
      texts.push({ id: 'ta', x: 385, y: 132, text: 'AudioClip 实例（独立计数，互不干扰）', cls: 'muted' });

      if (!m.vObjs.length) {
        boxes.push({ id: 'vempty', x: 20, y: 146, w: 325, h: 36, text: '（当前无存活实例）', hl: hl.vempty });
      }
      m.vObjs.forEach(function (name, i) {
        boxes.push({
          id: 'v' + i, x: 20, y: 146 + i * 42, w: 325, h: 34,
          text: name + '  (构造 ++VideoClip::alive_)',
          hl: hl['v' + i]
        });
      });

      if (!m.aObjs.length) {
        boxes.push({ id: 'aempty', x: 385, y: 146, w: 325, h: 36, text: '（当前无存活实例）', hl: hl.aempty });
      }
      m.aObjs.forEach(function (name, i) {
        boxes.push({
          id: 'a' + i, x: 385, y: 146 + i * 42, w: 325, h: 34,
          text: name + '  (构造 ++AudioClip::alive_)',
          hl: hl['a' + i]
        });
      });
    }

    if (x.arrows) x.arrows.forEach(function (a) { arrows.push(a); });
    if (x.note) texts.push({ id: 'note', x: 20, y: 276, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function virtualDispatch(m, p, rec) {
    initState(m);
    m.mode = 'virtual';
    rec.step(1, '热循环进入 VirtualFilter::processBlock()：准备处理第 i 个采样点，调用虚函数 this->applySample(s)。', {
      hl: { caller: 'cur' }
    });
    rec.step(1, '运行时第 1 步：通过 this 指针读取对象头部的 8 字节 vptr（导致 GainFilter 体积膨胀到 16 B）。', {
      hl: { caller: 'cmp', obj: 'cur' },
      arrows: [{ from: 'caller', to: 'obj', hl: 'cur', label: '读 vptr' }]
    });
    rec.step(2, '运行时第 2 步：按 vptr 查 GainFilter 的 vtable 槽位 [1]，取出函数地址 &GainFilter::applySample。', {
      hl: { obj: 'cmp', vtbl: 'cur' },
      arrows: [{ from: 'obj', to: 'vtbl', hl: 'cur', label: '查 vtable' }]
    });
    rec.step(2, '运行时第 3 步：间接跳转执行 applySample(s)。因为编译器不知道运行期具体类型，无法内联，也无法向量化。', {
      hl: { vtbl: 'cmp', impl: 'warn', cost: 'warn' },
      arrows: [{ from: 'vtbl', to: 'impl', hl: 'warn', label: '间接跳转' }],
      note: '代价不在单次跳转本身，而在虚调用边界阻断了内联与 SIMD 自动向量化（实测 10.19 ms）'
    });
  }

  function crtpInline(m, p, rec) {
    initState(m);
    m.mode = 'crtp';
    rec.step(4, '编译期实例化 FilterBase<CrtpGain>：Derived 模板参数在编译时就确定为 CrtpGain。', {
      hl: { caller: 'cur', obj: 'ok' }
    });
    rec.step(5, 'static_cast<const CrtpGain*>(this) 是零成本编译期指针类型转换，对象不含 vptr，sizeof(CrtpGain) 仅 4 B。', {
      hl: { caller: 'cmp', obj: 'ok', inline: 'new' },
      arrows: [{ from: 'caller', to: 'obj', hl: 'ok', label: '零成本转型' }, { from: 'obj', to: 'inline', hl: 'new', label: '编译期绑定' }]
    });
    rec.step(5, '编译器直接把 CrtpGain::applySample(s) 内联展开成 s * gain_，彻底消除函数调用边界。', {
      hl: { inline: 'ok', simd: 'new' },
      arrows: [{ from: 'inline', to: 'simd', hl: 'ok', label: '触发向量化' }]
    });
    rec.step(6, '循环体变得完全透明，编译器自动生成 SIMD 向量指令并行处理 4 个 float：1000 万采样点耗时从 10.19 ms 降至 1.00 ms（快 10.2×）。', {
      hl: { simd: 'ok', cost: 'ok' },
      note: '静态多态的核心红利：消除 vptr 内存 + 解锁内联与自动向量化'
    });
  }

  function countedLayout(m, p, rec) {
    initState(m);
    m.mode = 'counted';
    rec.step(7, '模板类 Counted<T> 按不同 T 实例化出互不相干的基类：Counted<VideoClip> 和 Counted<AudioClip> 各自拥有独立的 static alive_。', {
      hl: { cv: 'cur', ca: 'cur' }
    });

    m.vObjs = ['v1 (VideoClip)', 'v2 (VideoClip)'];
    m.videoAlive = 2;
    rec.step(8, '构造两个 VideoClip：调用 Counted<VideoClip> 构造，VideoClip::alive_ 变为 2；AudioClip::alive_ 保持 0。', {
      hl: { cv: 'new', v0: 'new', v1: 'new' },
      arrows: [{ from: 'v0', to: 'cv', hl: 'new', label: '++alive_' }]
    });

    m.aObjs = ['a1 (AudioClip)'];
    m.audioAlive = 1;
    rec.step(8, '构造一个 AudioClip：调用 Counted<AudioClip> 构造，只递增 AudioClip 自己的计数器。', {
      hl: { ca: 'new', a0: 'new' },
      arrows: [{ from: 'a0', to: 'ca', hl: 'new', label: '++alive_' }]
    });

    m.vObjs.push('v1Copy = v1.clone()');
    m.videoAlive = 3;
    rec.step(9, '通过 Cloneable<VideoClip> 自动生成的 clone() 深拷贝一份 v1Copy：触发拷贝构造，VideoClip::alive_ 升到 3。', {
      hl: { cv: 'ok', v2: 'ok' }
    });

    m.vObjs.pop();
    m.videoAlive = 2;
    rec.step(9, '销毁 v1Copy：调用 ~Counted<VideoClip>()，VideoClip::alive_ 精确回落到 2，AudioClip 仍为 1。', {
      hl: { cv: 'cmp', ca: 'ok' },
      note: '若直接继承非模板基类 Counted，所有子类会混用一个 static 计数器；CRTP 让每个子类零样板独享一份状态'
    });
  }

  DSViz.register('dp-crtp', {
    title: 'CRTP：编译期静态绑定 vs 虚函数查表，以及 Mixin 独立静态布局',
    width: 730,
    height: 295,
    code: [
      'virtual float applySample(float x) const = 0;           // 虚函数：每采样点查 vptr → vtable',
      '  // 间接调用阻断内联与 SIMD 向量化（1000 万采样点：10.19 ms，sizeof=16 B）',
      '',
      'template <class Derived> class FilterBase {             // CRTP：子类把自己传给基类模板',
      '  float y = static_cast<const Derived*>(this)->applySample(pcm[i]);',
      '};  // 编译期完全内联 + 自动向量化（1000 万采样点：1.00 ms，提速 10.2×，sizeof=4 B）',
      'template <class Derived> class Counted { inline static int alive_ = 0; };',
      'class VideoClip : public Cloneable<VideoClip>, public Counted<VideoClip> {};',
      'auto copy = v1.clone();  // 自动深拷贝 + 独立计数，零手写样板'
    ],
    init: function () { var m = {}; initState(m); return m; },
    view: view,
    start: function (m, rec) { crtpInline(m, {}, rec); },
    actions: [
      { label: 'CRTP 编译期展开', run: crtpInline },
      { label: '虚函数运行时查表', run: virtualDispatch },
      { label: 'Counted<T> 独立布局', run: countedLayout }
    ]
  });
})();
