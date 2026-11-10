/* 设计模式专栏《桥接模式》：继承版的类数是 M × N，桥接版是 M + N；加一种视图、加一种后端、运行时换后端（dp-bridge） */
(function () {
  'use strict';
  var ROW_Y = [58, 98, 138, 178];
  var GX = 66, GW = 74, GSTEP = 78;               // 左边：继承版的表格
  var VX = 400, VW = 104, IX = 540, IW = 86, RX = 664, RW = 70;   // 右边：桥接版
  var VIEW_IDS = { '主窗口': 'main', '缩略图': 'thumb', '画中画': 'pip', '多画面': 'multi' };
  var BACK_IDS = { '软件': 'soft', 'SDL': 'sdl', 'Metal': 'metal', 'Vulkan': 'vk' };

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'tl', x: 10, y: 22, text: '继承：每种组合写一个类' });
    texts.push({ id: 'tr', x: VX, y: 22, text: '桥接：两边各自扩展，用指针连起来' });
    m.backends.forEach(function (b, j) {
      texts.push({ id: 'ch' + j, x: GX + j * GSTEP + GW / 2, y: 48, text: b, cls: 'muted', anchor: 'middle' });
    });
    m.views.forEach(function (v, i) {
      texts.push({ id: 'rl' + i, x: 10, y: ROW_Y[i] + 20, text: v, cls: 'muted' });
      m.backends.forEach(function (b, j) {
        var id = 'g-' + VIEW_IDS[v] + '-' + BACK_IDS[b];
        boxes.push({ id: id, x: GX + j * GSTEP, y: ROW_Y[i], w: GW, h: 30, text: v.charAt(0) + '·' + b, hl: hl[id] || (m.fresh[id] ? 'new' : null) });
      });
      var vid = 'v-' + VIEW_IDS[v];
      boxes.push({ id: vid, x: VX, y: ROW_Y[i], w: VW, h: 30, text: v, hl: hl[vid] || (m.fresh[vid] ? 'new' : null) });
      arrows.push({ from: vid, to: 'iface', hl: hl.bridge ? 'cur' : null });
    });
    boxes.push({ id: 'iface', x: IX, y: 113, w: IW, h: 34, text: 'Renderer', hl: hl.iface });
    m.backends.forEach(function (b, j) {
      var rid = 'r-' + BACK_IDS[b];
      boxes.push({ id: rid, x: RX, y: ROW_Y[j], w: RW, h: 30, text: b, hl: hl[rid] || (m.fresh[rid] ? 'new' : null) });
      arrows.push({ from: 'iface', to: rid, hl: hl.bridge ? 'cur' : null });
    });
    if (x.pip) arrows.push({ from: 'v-pip', to: 'r-' + BACK_IDS[x.pip], hl: 'cur', label: 'pip.r_', curve: x.pipCurve || 34 });
    var M = m.views.length, N = m.backends.length;
    texts.push({ id: 'cl', x: 10, y: 240, text: '具体类 ' + M * N + ' 个 = ' + M + ' × ' + N + '（另有 ' + M + ' 个视图基类、' + N + ' 份后端代码）', cls: hl.count ? null : 'muted' });
    texts.push({ id: 'cr', x: VX, y: 240, text: '类 ' + (M + N) + ' 个 = ' + M + ' + ' + N + '（另有 2 个接口）', cls: hl.count ? null : 'muted' });
    if (x.note) texts.push({ id: 'note', x: 10, y: 266, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function fresh() { return { views: ['主窗口', '缩略图', '画中画'], backends: ['软件', 'SDL', 'Metal'], fresh: {} }; }

  function intro(m, rec) {
    rec.step(1, '3 种视图（主窗口、缩略图、画中画）× 3 种渲染后端（软件、SDL、Metal）。左边用继承：后端是类型的一部分，每种组合都要一个类，比如「画中画·Metal」就是 PictureInPictureMetal。',
      { hl: { 'g-pip-metal': 'cur', count: true } });
    rec.step(2, '右边用桥接：视图只管排版，通过 unique_ptr<Renderer> 这座「桥」使用后端的基本操作。两边各自是一个继承体系，类的数量是相加而不是相乘。',
      { hl: { bridge: true, iface: 'cur', count: true } });
  }

  function addView(m, rec) {
    if (m.views.indexOf('多画面') >= 0) { rec.step(5, '「多画面」已经加过了。'); return; }
    m.fresh = {};
    m.views.push('多画面');
    m.backends.forEach(function (b) { m.fresh['g-multi-' + BACK_IDS[b]] = true; });
    m.fresh['v-multi'] = true;
    rec.step(5, '加一种视图「多画面」（2 × 2 四个画面）。继承版要加 1 个视图基类和 ' + m.backends.length + ' 个具体类，每个后端一个；桥接版只加 1 个类。实测（3 × 3 起步）：继承版新增 45 行，桥接版 10 行。',
      { hl: { count: true }, note: '实测：inherit_v1 → v2 新增 45 行；bridge_v1 → v2 新增 10 行（不算空行和 main）' });
  }

  function addBackend(m, rec) {
    if (m.backends.indexOf('Vulkan') >= 0) { rec.step(6, 'Vulkan 已经加过了。'); return; }
    m.fresh = {};
    m.backends.push('Vulkan');
    m.views.forEach(function (v) { m.fresh['g-' + VIEW_IDS[v] + '-vk'] = true; });
    m.fresh['r-vk'] = true;
    rec.step(6, '加一种后端 Vulkan。继承版每种视图都要多一个类（' + m.views.length + ' 个），makeView 里也要多 ' + m.views.length + ' 个分支；桥接版只加一个 VulkanRenderer，视图一行都不用改。',
      { hl: { count: true }, note: '实测：inherit_v2 → v3 新增 58 行；bridge_v2 → v3 新增 12 行' });
  }

  function switchBackend(m, rec) {
    m.fresh = {};
    rec.step(7, '画中画正在用 Metal 渲染，用户把它拖到了右上角。现在它被拖到外接显示器上，那里 Metal 不可用，要换成软件渲染。',
      { pip: 'Metal', hl: { 'v-pip': 'cur', 'g-pip-metal': 'cur' } });
    rec.step(7, '继承版：后端是类型的一部分，只能销毁 PictureInPictureMetal、新建一个 PictureInPictureSoft。新对象的位置是默认值，实测画中画跳回了左下角（除非手动把每个状态都拷过去）。',
      { pip: 'Metal', hl: { 'v-pip': 'cur', 'g-pip-metal': 'warn', 'g-pip-soft': 'new' }, note: '继承版：新对象，拖动的位置丢了' });
    rec.step(7, '桥接版：同一个视图对象，setRenderer 换掉 r_ 指向的后端。位置、播放进度这些状态都在视图里，原样保留，实测画中画还在右上角。',
      { pip: '软件', pipCurve: -70, hl: { 'v-pip': 'ok', 'r-soft': 'cur', 'r-metal': 'dim' }, note: '桥接版：同一个对象，只换了后端' });
  }

  DSViz.register('dp-bridge', {
    title: '继承 M × N 与桥接 M + N：加视图、加后端、运行时换后端',
    width: 740,
    height: 276,
    code: [
      'class VideoView {                              // 抽象部分：视图',
      '    std::unique_ptr<Renderer> r_;              // 桥',
      'public:',
      '    void setRenderer(std::unique_ptr<Renderer> r) { r_ = std::move(r); }',
      'class MultiView : public VideoView { ... };      // 加一种视图：1 个类',
      'class VulkanRenderer : public Renderer { ... };  // 加一种后端：1 个类',
      'pip.setRenderer(makeRenderer("软件"));          // 运行时换后端，对象不变'
    ],
    init: fresh,
    view: view,
    start: function (m, rec) { intro(m, rec); addView(m, rec); addBackend(m, rec); switchBackend(m, rec); },
    actions: [
      { label: '加一种视图', run: function (m, p, rec) { addView(m, rec); } },
      { label: '加一种后端', run: function (m, p, rec) { addBackend(m, rec); } },
      { label: '运行时换后端', run: function (m, p, rec) { switchBackend(m, rec); } },
      { label: '重置', run: function (m, p, rec) { var f = fresh(); m.views = f.views; m.backends = f.backends; m.fresh = {}; intro(m, rec); } }
    ]
  });
})();
