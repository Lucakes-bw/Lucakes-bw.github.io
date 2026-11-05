/* 《性能分析》一章的逐步演示
 *
 * lx-sample   采样：每隔一段时间看一眼程序在执行哪个函数，次数的比例就是时间的比例
 * lx-unwind   回溯调用栈：沿着帧指针一层层往上找；没有帧指针时链就断了
 * lx-cache    缓存行：按行访问一次缺失换来 4 次命中，按列访问每次都缺失
 * lx-offcpu   on-CPU 与 off-CPU：采样只看得到正在运行的线程，等待的时间要在线程切换时记录
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- 采样 ---------- */

  // 程序的执行过程：20 个时间片，每 5 片重复一次：上传 3 片、下载 1 片、排序 1 片
  var SLOTS = 20;
  var KINDS = {
    U: { leaf: 'checksum', path: 'main → handleUpload → checksum', short: 'U' },
    D: { leaf: 'checksum', path: 'main → handleDownload → checksum', short: 'D' },
    S: { leaf: 'cmpRecord', path: 'main → sortRecords → qsort → cmpRecord', short: 'S' }
  };
  var PATTERN = 'UUUDS';
  function slotKind(i) { return PATTERN[i % PATTERN.length]; }

  function sampleInit() { return { hits: [], count: { U: 0, D: 0, S: 0 } }; }

  function sampleView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    texts.push(txt('t1', 20, 20, '程序的执行过程（每格是一小段时间；U 上传、D 下载、S 排序）', 'muted'));
    for (var i = 0; i < SLOTS; i++) {
      var k = slotKind(i);
      boxes.push(box('s' + i, 20 + i * 30, 30, 26, 30, k, hl['s' + i] || (m.hits.indexOf(i) >= 0 ? 'ok' : (k === 'S' ? 'dim' : null))));
    }
    var total = m.count.U + m.count.D + m.count.S;
    texts.push(txt('t2', 20, 92, '采样到的调用栈', 'muted'));
    texts.push(txt('t3', 470, 92, '次数　估计　真实', 'muted'));
    ['U', 'D', 'S'].forEach(function (k, r) {
      var y = 100 + r * 38;
      boxes.push(box('c' + k, 20, y, 430, 30, KINDS[k].path, hl['c' + k]));
      var real = { U: 60, D: 20, S: 20 }[k];
      var est = total ? Math.round(m.count[k] * 100 / total) + '%' : '—';
      texts.push(txt('n' + k, 470, y + 20, pad(m.count[k], 4) + '　' + pad(est, 4) + '　' + real + '%', null));
    });
    texts.push(txt('sum', 20, 230, '共采样 ' + total + ' 次。checksum 的自身时间 = U、D 两行之和；分清调用者要靠调用栈', 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function pad(v, n) { v = String(v); while (v.length < n) v = ' ' + v; return v; }

  function sampleRun(m, step, first, rec) {
    m.hits = []; m.count = { U: 0, D: 0, S: 0 };
    rec.step(1, '每隔 ' + step + ' 格采样一次，从第 ' + first + ' 格开始。', { vars: { 间隔: step } });
    for (var i = first; i < SLOTS; i += step) {
      var k = slotKind(i), h = {};
      h['s' + i] = 'cur';
      rec.step(2, '定时器中断：程序正在执行 ' + KINDS[k].leaf + '。', { hl: h, vars: { 间隔: step, 第: i } });
      m.hits.push(i);
      m.count[k]++;
      h['c' + k] = 'new';
      rec.step(3, '记下这一刻的调用栈：' + KINDS[k].path + '。', { hl: h, vars: { 间隔: step, 第: i } });
    }
    var only = Object.keys(m.count).filter(function (k) { return m.count[k] > 0; });
    var msg = only.length === 1
      ? '采样的间隔正好是程序循环周期（5 格）的倍数，每次都落在同一个函数上，结果完全错了。所以 perf 常用 99、999 这样的频率，避免和程序的节奏同步。'
      : '次数的比例就是时间的比例。采样越多，估计越准：真实情况是上传 60%、下载 20%、排序 20%。';
    rec.step(4, msg, { vars: { 间隔: step } });
  }

  DSViz.register('lx-sample', {
    title: '采样：每隔一段时间看一眼程序在执行哪个函数',
    height: 250,
    width: 640,
    code: [
      '设定采样频率（perf record -F）',
      '定时器到时，打断程序',
      '记录当前的函数和调用栈，程序继续运行',
      '结束后统计：每个调用栈出现的次数'
    ],
    init: sampleInit,
    view: sampleView,
    start: function (m, rec) { sampleRun(m, 3, 1, rec); },
    actions: [
      { label: '采样', fields: [{ key: 'step', label: '间隔', value: 2, size: 2 }, { key: 'first', label: '起点', value: 0, size: 2 }],
        check: function (m, p) {
          if (typeof p.step !== 'number' || p.step < 1 || p.step > 10) return '间隔请输入 1～10';
          if (typeof p.first !== 'number' || p.first < 0 || p.first >= p.step) return '起点请输入 0～间隔-1';
        },
        run: function (m, p, rec) { sampleRun(m, p.step, p.first, rec); } },
      { label: '间隔 5（和程序同步）', run: function (m, p, rec) { sampleRun(m, 5, 1, rec); } }
    ]
  });

  /* ---------- 回溯调用栈 ---------- */

  // 栈从高地址（上）向低地址（下）增长；每个栈帧最上面保存着调用者的帧指针和返回地址
  var FRAMES = [
    { fn: 'main', ret: '__libc_start_main+…' },
    { fn: 'handleUpload', ret: 'main+0x5c' },
    { fn: 'checksum', ret: 'handleUpload+0x10' }
  ];

  function unwindView(m, x) {
    var boxes = [], texts = [], arrows = [], hl = x.hl || {};
    texts.push(txt('t1', 20, 20, '线程的栈（高地址在上）', 'muted'));
    FRAMES.forEach(function (f, i) {
      var y = 30 + i * 70;
      boxes.push(box('f' + i, 20, y, 300, 62, '', hl['f' + i] || null, null));
      texts.push(txt('fn' + i, 30, y + 16, f.fn + ' 的栈帧', null));
      boxes.push(box('fp' + i, 30, y + 24, 130, 30, x.nofp ? '（别的数据）' : (i === 0 ? '帧指针 = 0' : '上一帧的帧指针'), hl['fp' + i] || (x.nofp ? 'dim' : null)));
      boxes.push(box('ra' + i, 170, y + 24, 140, 30, '返回地址', hl['ra' + i]));
    });
    boxes.push(box('reg', 380, 190, 110, 32, x.nofp ? 'x29 = ？' : 'x29（帧指针）', hl.reg || 'cur'));
    texts.push(txt('t2', 380, 180, '寄存器', 'muted'));
    if (!x.nofp) arrows.push({ from: 'reg', to: 'fp2', hl: hl.a2 });
    texts.push(txt('t3', 380, 40, '回溯得到的调用栈', 'muted'));
    (x.got || []).forEach(function (g, i) {
      boxes.push(box('g' + i, 380, 50 + i * 30, 240, 26, g, i === (x.got.length - 1) ? 'new' : null));
    });
    if (x.note) texts.push(txt('note', 20, 250, x.note, 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, arrows: arrows, vars: x.vars || {} };
  }

  function unwindFp(m, rec) {
    var got = ['checksum（PC 寄存器）'];
    rec.step(1, '采样时程序正在 checksum 中。PC 寄存器给出当前的函数；帧指针寄存器 x29 指向 checksum 的栈帧。', { got: got.slice(), hl: { reg: 'cur', f2: 'cur' } });
    got.push('handleUpload（返回地址）');
    rec.step(2, '帧里保存着返回地址：checksum 返回后回到 handleUpload，所以调用者是 handleUpload。', { got: got.slice(), hl: { ra2: 'ok', f2: 'cur', a2: 'cur' } });
    rec.step(3, '帧里还保存着上一帧的帧指针，沿着它跳到上面 handleUpload 的栈帧。', { got: got.slice(), hl: { fp2: 'cur', f1: 'cur' } });
    got.push('main（返回地址）');
    rec.step(2, '读出 handleUpload 帧中的返回地址：调用者是 main。', { got: got.slice(), hl: { ra1: 'ok', f1: 'cur' } });
    rec.step(3, '再沿 handleUpload 帧中保存的帧指针跳一次，到 main 的栈帧。', { got: got.slice(), hl: { fp1: 'cur', f0: 'cur' } });
    rec.step(4, '帧指针是 0，回溯结束。整个过程只是几次读内存，所以每次采样都可以做。', { got: got.slice(), hl: { fp0: 'ok' } });
  }

  function unwindNoFp(m, rec) {
    var got = ['checksum（PC 寄存器）'];
    rec.step(1, '编译时没有 -fno-omit-frame-pointer：开了优化，GCC 不再维护帧指针，x29 被当成普通寄存器保存别的数据。', { nofp: true, got: got.slice(), hl: { reg: 'warn' } });
    rec.step(5, 'perf 仍然把 x29 当成帧指针去读，读到的不是栈帧：回溯在第一层就停了（或者得到一个错误的地址）。', { nofp: true, got: got.slice(), hl: { reg: 'warn', f2: 'warn' } });
    rec.step(5, '报告中 checksum 上面的调用者都丢了，分不清它是被谁调用的。（实际的程序中常常是中间断掉一截，下文的实验中丢的是 main。）', { nofp: true, got: got.slice(), note: '解决：-fno-omit-frame-pointer 重新编译，或者 --call-graph dwarf' });
  }

  DSViz.register('lx-unwind', {
    title: '回溯调用栈：沿着帧指针一层层往上找',
    height: 260,
    width: 640,
    code: [
      'PC 寄存器：当前的函数',
      '当前帧中的返回地址：调用者',
      '当前帧中保存的帧指针：跳到调用者的栈帧',
      '帧指针为 0：到了最外层，结束',
      '没有帧指针：读不到正确的栈帧，回溯中断'
    ],
    init: function () { return {}; },
    view: unwindView,
    start: function (m, rec) { unwindFp(m, rec); },
    actions: [
      { label: '有帧指针', run: function (m, p, rec) { unwindFp(m, rec); } },
      { label: '没有帧指针', run: function (m, p, rec) { unwindNoFp(m, rec); } }
    ]
  });

  /* ---------- 缓存行 ---------- */

  // 4×8 的 int 矩阵按行存放；一个缓存行放 4 个 int，所以每行是 2 个缓存行；缓存只能放 3 个缓存行（最久没用的被换出）
  var ROWS = 4, COLS = 8, PER_LINE = 4, CAP = 3;

  function cacheInit() { return { lines: [], hit: 0, miss: 0, done: [] }; }

  function cacheView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    texts.push(txt('t1', 20, 20, '矩阵 m[4][8]：格中是元素所在的缓存行', 'muted'));
    for (var r = 0; r < ROWS; r++)
      for (var c = 0; c < COLS; c++) {
        var id = 'e' + r + '_' + c, line = r * 2 + Math.floor(c / PER_LINE);
        boxes.push(box(id, 20 + c * 40 + (c >= PER_LINE ? 8 : 0), 30 + r * 38, 36, 32, 'L' + line, hl[id] || (m.done.indexOf(id) >= 0 ? 'dim' : null)));
      }
    texts.push(txt('t2', 380, 20, '缓存（只能放 ' + CAP + ' 个缓存行）', 'muted'));
    for (var i = 0; i < CAP; i++) {
      var l = m.lines[i];
      boxes.push(box('k' + i, 380, 30 + i * 40, 120, 32, l == null ? '空' : '缓存行 L' + l, hl['k' + i] || (l == null ? 'dim' : null)));
    }
    texts.push(txt('st', 380, 168, '命中 ' + m.hit + ' 次，缺失 ' + m.miss + ' 次', null));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function touch(m, r, c, rec) {
    var line = r * 2 + Math.floor(c / PER_LINE), id = 'e' + r + '_' + c, h = {};
    var at = m.lines.indexOf(line);
    h[id] = 'cur';
    if (at >= 0) {
      m.hit++;
      m.lines.splice(at, 1); m.lines.push(line);
      h['k' + (m.lines.length - 1)] = 'ok';
      rec.step(2, '读 m[' + r + '][' + c + ']：缓存行 L' + line + ' 已经在缓存中，命中（约 1 ns）。', { hl: h, vars: { 行: r, 列: c } });
    } else {
      m.miss++;
      var out = null;
      if (m.lines.length >= CAP) out = m.lines.shift();
      m.lines.push(line);
      h['k' + (m.lines.length - 1)] = 'warn';
      rec.step(3, '读 m[' + r + '][' + c + ']：缺失，从内存读入整个缓存行 L' + line + '（4 个元素）' + (out != null ? '，换出最久没用的 L' + out : '') + '。', { hl: h, vars: { 行: r, 列: c } });
    }
    m.done.push(id);
  }

  function cacheRun(m, byCol, rec) {
    var e = cacheInit(); Object.keys(e).forEach(function (k) { m[k] = e[k]; });
    rec.step(1, byCol ? '按列访问：先走完第 0 列的 4 个元素，再走第 1 列……' : '按行访问：一行一行地访问，和内存中的存放顺序一致。', { vars: {} });
    if (byCol) { for (var c = 0; c < COLS; c++) for (var r = 0; r < ROWS; r++) touch(m, r, c, rec); }
    else { for (var r2 = 0; r2 < ROWS; r2++) for (var c2 = 0; c2 < COLS; c2++) touch(m, r2, c2, rec); }
    rec.step(4, byCol
      ? '按列：每一列要用到 4 个不同的缓存行，缓存放不下，下一列回来时它们已被换出，32 次全部缺失。'
      : '按行：每读入一个缓存行，接下来 3 次都命中，32 次中只缺失 8 次。', { vars: {} });
  }

  DSViz.register('lx-cache', {
    title: '缓存行：按行访问和按列访问',
    height: 200,
    width: 640,
    code: [
      '开始遍历',
      '要读的数据所在的缓存行已在缓存中：命中',
      '不在：从内存读入整个缓存行，必要时换出最久没用的一行',
      '统计命中和缺失'
    ],
    init: cacheInit,
    view: cacheView,
    start: function (m, rec) { cacheRun(m, false, rec); },
    actions: [
      { label: '按行访问', run: function (m, p, rec) { cacheRun(m, false, rec); } },
      { label: '按列访问', run: function (m, p, rec) { cacheRun(m, true, rec); } }
    ]
  });

  /* ---------- on-CPU 与 off-CPU ---------- */

  // 3 个线程在 12 个时刻的状态：P parse（计算），K lookup（持锁计算），L 等锁，I 等磁盘
  var TL = [
    'PKIIIIPKIIII',
    'PLKIIIPLKIII',
    'PLLKIIIPLKII'
  ];
  var STATE = {
    P: { t: 'parse', c: '解析', hl: 'cur', on: true },
    K: { t: 'lookup', c: '查表', hl: 'cur', on: true },
    L: { t: '等锁', c: '等锁', hl: 'warn', on: false },
    I: { t: '等磁盘', c: '等盘', hl: 'dim', on: false }
  };

  function offInit() { return { tick: -1, on: { P: 0, K: 0 }, off: { L: 0, I: 0 } }; }

  function offView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    texts.push(txt('t1', 20, 20, '3 个线程的状态（每格是一个时刻；解析 = parse，查表 = lookup 持锁，等盘 = 等磁盘）', 'muted'));
    for (var t = 0; t < 3; t++) {
      texts.push(txt('th' + t, 20, 52 + t * 36, '线程 ' + (t + 1), null));
      for (var i = 0; i < 12; i++) {
        var s = TL[t][i], id = 'x' + t + '_' + i;
        boxes.push(box(id, 80 + i * 44, 32 + t * 36, 40, 30, i <= m.tick ? STATE[s].c : '', i <= m.tick ? (hl[id] || STATE[s].hl) : 'dim'));
      }
    }
    texts.push(txt('t2', 20, 166, 'on-CPU 采样（perf record）', 'muted'));
    texts.push(txt('a', 20, 188, 'parse ' + m.on.P + '　lookup ' + m.on.K, null));
    texts.push(txt('t3', 340, 166, 'off-CPU 记录（线程切换时）', 'muted'));
    texts.push(txt('b', 340, 188, '等锁 ' + m.off.L + '　等磁盘 ' + m.off.I, null));
    if (x.note) texts.push(txt('n', 20, 220, x.note, 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function offRun(m, rec) {
    var e = offInit(); Object.keys(e).forEach(function (k) { m[k] = e[k]; });
    rec.step(1, '每个请求：parse（计算）→ lookup（持有全局锁计算）→ 从磁盘读数据。3 个线程同时处理请求。', {});
    for (var i = 0; i < 12; i++) {
      m.tick = i;
      var h = {}, run = [], wait = [];
      for (var t = 0; t < 3; t++) {
        var s = TL[t][i];
        h['x' + t + '_' + i] = STATE[s].on ? 'new' : STATE[s].hl;
        if (STATE[s].on) { m.on[s]++; run.push('线程 ' + (t + 1) + ' ' + STATE[s].t); }
        else { m.off[s]++; wait.push('线程 ' + (t + 1) + ' ' + STATE[s].t); }
      }
      rec.step(run.length ? 2 : 3,
        '时刻 ' + i + '：' + (run.length ? '采样看到 ' + run.join('、') : '没有线程在运行，采样什么也看不到') + (wait.length ? '；' + wait.join('、') + '，只有切换时的记录知道' : '') + '。',
        { hl: h, vars: { 时刻: i } });
    }
    rec.step(4, '只看 on-CPU：parse 和 lookup 各占一半，好像该优化计算。但 36 格中有 24 格在等待，等磁盘就占了 19 格：真正的瓶颈在 IO 和锁。',
      { note: '运行 12 格，等锁 5 格，等磁盘 19 格' });
  }

  DSViz.register('lx-offcpu', {
    title: 'on-CPU 与 off-CPU：采样只看得到正在运行的线程',
    height: 236,
    width: 640,
    code: [
      '线程们开始处理请求',
      'on-CPU 采样：记录正在运行的线程的调用栈',
      'off-CPU：线程让出 CPU 时记下原因和调用栈，回来时记下等了多久',
      '把两种结果放在一起看'
    ],
    init: offInit,
    view: offView,
    start: function (m, rec) { offRun(m, rec); },
    actions: [
      { label: '重新运行', run: function (m, p, rec) { offRun(m, rec); } }
    ]
  });
})();
