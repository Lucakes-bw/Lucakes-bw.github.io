/* 设计模式专栏《策略》：Player 持有一个缩放策略，运行时可以换；每种策略在源图上读几个像素（dp-strategy） */
(function () {
  'use strict';

  var G = [[40, 60, 90, 120], [50, 80, 160, 200], [60, 120, 210, 230], [70, 140, 220, 240]];
  var SX = 1.3, SY = 1.6;                        // 采样点在这块 4×4 源像素里的位置
  var ALGOS = [
    { id: 'N', name: 'Nearest', taps: 1 },
    { id: 'B', name: 'Bilinear', taps: 4 },
    { id: 'C', name: 'Bicubic', taps: 16 }
  ];

  function cubic(t) {
    t = Math.abs(t);
    if (t < 1) return (1.5 * t - 2.5) * t * t + 1;
    if (t < 2) return ((-0.5 * t + 2.5) * t - 4) * t + 2;
    return 0;
  }
  // 返回 { cells: 读到的像素 [[i, j], ...], v: 采样结果 }
  function sample(k) {
    var x0 = Math.floor(SX), y0 = Math.floor(SY), fx = SX - x0, fy = SY - y0, cells = [], v = 0, i, j;
    if (k === 0) {
      i = Math.floor(SX + 0.5); j = Math.floor(SY + 0.5);
      return { cells: [[i, j]], v: G[j][i] };
    }
    if (k === 1) {
      for (j = 0; j < 2; j++) for (i = 0; i < 2; i++) cells.push([x0 + i, y0 + j]);
      v = (G[y0][x0] * (1 - fx) + G[y0][x0 + 1] * fx) * (1 - fy) + (G[y0 + 1][x0] * (1 - fx) + G[y0 + 1][x0 + 1] * fx) * fy;
      return { cells: cells, v: Math.round(v) };
    }
    for (j = -1; j <= 2; j++) for (i = -1; i <= 2; i++) {
      cells.push([x0 + i, y0 + j]);
      v += G[y0 + j][x0 + i] * cubic(fx - i) * cubic(fy - j);
    }
    return { cells: cells, v: Math.round(v) };
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [], i, j;
    texts.push({ id: 'tc', x: 20, y: 22, text: '上下文', cls: 'muted' });
    texts.push({ id: 'ts', x: 250, y: 22, text: '策略（都实现 Scaler 接口）', cls: 'muted' });
    texts.push({ id: 'tg', x: 500, y: 22, text: '源图上的 4×4 像素，× 是采样点', cls: 'muted' });
    boxes.push({ id: 'P', x: 20, y: 102, w: 170, h: 48, text: 'Player', sub: 'scaler_ → ' + ALGOS[m.cur].name, hl: hl.P });
    ALGOS.forEach(function (a, k) {
      boxes.push({ id: a.id, x: 250, y: 36 + k * 72, w: 190, h: 48, text: a.name, sub: '每个输出像素读 ' + a.taps + ' 个',
        hl: k === m.cur ? (hl.S || 'cur') : (hl[a.id] || 'dim') });
    });
    arrows.push({ from: 'P', to: ALGOS[m.cur].id, hl: hl.a || 'cur' });
    var r = m.show ? sample(m.cur) : { cells: [] }, read = {};
    r.cells.forEach(function (c) { read[c[0] + ',' + c[1]] = true; });
    for (j = 0; j < 4; j++) for (i = 0; i < 4; i++) {
      boxes.push({ id: 'g' + i + j, x: 500 + i * 54, y: 36 + j * 46, w: 50, h: 42, text: String(G[j][i]),
        hl: read[i + ',' + j] ? 'cmp' : undefined });
    }
    texts.push({ id: 'cross', x: 500 + SX * 54 + 25, y: 36 + SY * 46 + 26, text: '×', anchor: 'middle' });
    if (m.show) texts.push({ id: 'res', x: 500, y: 238, text: '结果 = ' + r.v + '（读了 ' + r.cells.length + ' 个像素）' });
    if (m.frame) texts.push({ id: 'fr', x: 20, y: 270, text: m.frame });
    if (x.note) texts.push({ id: 'note', x: 20, y: 296, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m) { m.cur = 1; m.show = false; m.frame = ''; }

  function swap(m, p, rec) {
    reset(m);
    rec.step(1, 'Player 创建时拿到一个 Bilinear 策略，它只通过 Scaler 接口使用这个对象，不知道具体是哪个类。', { hl: { P: 'new' } });
    m.cur = 0; m.show = true;
    rec.step(2, '生成缩略图墙时换成 Nearest：每个输出像素只取离采样点最近的一个源像素，最快，但画面有锯齿。', { hl: { P: 'cur' } });
    rec.step(4, '缩放时 Player 只调用 scaler_->scale()。换了策略，这一行代码不用改。', { hl: { a: 'cur' } });
    m.cur = 1;
    rec.step(1, '换回 Bilinear：取周围 4 个像素，按距离加权平均。', { hl: { P: 'cur' } });
    m.cur = 2;
    rec.step(3, '全屏播放换成 Bicubic：取周围 16 个像素，用三次卷积核加权，边缘更清楚，但每个像素的计算量是 Bilinear 的 4 倍多。',
      { hl: { P: 'cur' }, note: '算法是可以互换的：同一个采样点，三种策略给出不同的值，Player 的代码不变' });
  }

  function degrade(m, p, rec) {
    reset(m);
    m.cur = 2; m.show = true;
    m.frame = '帧 3：解码 6 + 缩放 21 = 27 ms（预算 33.3 ms）';
    rec.step(3, '用户选了高画质，Player 用 Bicubic。简单的画面解码只要 6 ms，加上缩放 21 ms，30 fps 的预算够用。', { hl: { P: 'cur' } });
    m.frame = '帧 4：解码 15 + 缩放 21 = 36 ms  超时';
    rec.step(4, '进入复杂的画面，解码变成 15 ms，这一帧超时了。', { hl: { P: 'warn', S: 'warn' } });
    m.frame = '帧 5：解码 16 + 缩放 21 = 37 ms  又超时 → 降级';
    m.cur = 1;
    rec.step(5, '连续两帧超时，Player 自己把策略换成便宜一级的 Bilinear（3 ms）。换策略只是换一个指针。', { hl: { P: 'cur', S: 'new' } });
    m.frame = '帧 6：解码 15 + 缩放 3 = 18 ms';
    rec.step(4, '复杂画面里也不再超时，画面稍微软一点，但不卡。', { hl: { S: 'ok' } });
    m.frame = '帧 10：连续 3 帧余量充足 → 恢复 Bicubic';
    m.cur = 2;
    rec.step(5, '画面简单下来以后，换回用户选的 Bicubic。决定换哪个策略的是 Player（上下文），策略之间互相不知道对方。',
      { hl: { P: 'cur', S: 'ok' }, note: '运行时切换：选择策略的逻辑在上下文里，策略本身只负责算' });
  }

  DSViz.register('dp-strategy', {
    title: '策略：Player 持有一个 Scaler，运行时可以换',
    width: 740,
    height: 310,
    code: [
      'Player player(std::make_unique<Bilinear>());',
      'player.setScaler(std::make_unique<Nearest>());   // 缩略图墙：要快',
      'player.setScaler(std::make_unique<Bicubic>());   // 全屏播放：要清楚',
      'Plane out = scaler_->scale(frame, w, h);         // Player 只认识接口',
      'if (连续两帧超时) setScaler(便宜一级的策略);     // 运行时降级'
    ],
    init: function () { var m = {}; reset(m); return m; },
    view: view,
    start: function (m, rec) { swap(m, {}, rec); },
    actions: [{ label: '换算法', run: swap }, { label: '掉帧时降级', run: degrade }]
  });
})();
