/* 《内存管理》一章的逐步演示
 *
 * lx-fault  缺页：第一次写分配物理页；读未写过的页映射到零页
 * lx-heap   堆只能从顶端收缩：留下一个块，RSS 就降不下来；malloc_trim
 * lx-dirty  脏页与写回：写入先进页缓存，脏页到了上限以后写入被限速
 * lx-cow    fork 与写时复制：共享物理页，谁写谁复制
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- 缺页 ---------- */

  var VP = 6, PF = 5;

  function faultInit() { return { pt: [null, null, null, null, null, null], frames: ['零页', null, null, null, null], faults: 0 }; }

  function faultView(m, x) {
    var boxes = [], texts = [], arrows = [], hl = x.hl || {};
    texts.push(txt('t1', 20, 20, '虚拟页', 'muted'));
    texts.push(txt('t2', 250, 20, '页表', 'muted'));
    texts.push(txt('t3', 470, 20, '物理页', 'muted'));
    for (var i = 0; i < VP; i++) {
      boxes.push(box('v' + i, 20, 30 + i * 36, 120, 28, '虚拟页 ' + i, hl['v' + i]));
      var e = m.pt[i];
      boxes.push(box('e' + i, 250, 30 + i * 36, 120, 28, e == null ? '（空）' : '→ ' + (e.f === 0 ? '零页' : '物理页 ' + e.f) + (e.ro ? '，只读' : ''), hl['e' + i] || (e == null ? 'dim' : null)));
      arrows.push({ from: 'v' + i, to: 'e' + i, hl: hl['v' + i] ? 'ok' : null });
      if (e != null) arrows.push({ from: 'e' + i, to: 'f' + e.f, hl: hl['e' + i] ? 'ok' : null });
    }
    for (var f = 0; f < PF; f++) {
      boxes.push(box('f' + f, 470, 30 + f * 42, 130, 32, f === 0 ? '零页（共享）' : (m.frames[f] == null ? '空闲' : '物理页 ' + f), hl['f' + f] || (f === 0 ? 'dim' : (m.frames[f] == null ? 'dim' : null))));
    }
    texts.push(txt('n', 20, 256, '缺页 ' + m.faults + ' 次；RSS ' + m.frames.filter(function (v, k) { return k > 0 && v != null; }).length + ' 页', null));
    return { kind: 'free', boxes: boxes, texts: texts, arrows: arrows, vars: x.vars || {} };
  }

  function access(m, i, write, rec) {
    var vars = { op: (write ? '写' : '读') + '虚拟页 ' + i };
    var h = {}; h['v' + i] = 'cur';
    rec.step(1, 'CPU 访问虚拟页 ' + i + '：MMU 查页表（先查 TLB）。', { hl: h, vars: vars });
    var e = m.pt[i];
    if (e && (!write || !e.ro)) {
      h['e' + i] = 'ok'; h['f' + e.f] = 'ok';
      rec.step(2, '页表中有这一项，直接访问物理内存，不需要内核参与。', { hl: h, vars: vars });
      return;
    }
    m.faults++;
    h['e' + i] = 'warn';
    rec.step(3, e ? '页表指向零页，而零页是只读的：写入触发缺页（写时复制）。' : '页表中没有这一项：触发缺页异常，进入内核。', { hl: h, vars: vars });
    if (!write) {
      m.pt[i] = { f: 0, ro: true };
      h['e' + i] = 'new'; h['f0'] = 'cmp';
      rec.step(5, '只是读，而这一页从没写过，内容一定全是 0：映射到共享的零页，不分配新的物理页，RSS 不变。', { hl: h, vars: vars });
      return;
    }
    var f = m.frames.indexOf(null, 1);
    if (f < 0) { rec.step(3, '没有空闲的物理页了（这时内核要回收内存，第 5 篇）。', { hl: h, vars: vars }); return; }
    m.frames[f] = 'used';
    h['f' + f] = 'new';
    rec.step(4, '分配一个空闲的物理页 ' + f + '，清零。', { hl: h, vars: vars });
    m.pt[i] = { f: f, ro: false };
    h['e' + i] = 'new';
    rec.step(5, '填进页表，回到程序重新执行这条写指令。以后再访问这一页就不会缺页了。', { hl: h, vars: vars });
  }

  DSViz.register('lx-fault', {
    title: '缺页：第一次写才分配物理页，读未写过的页映射到零页',
    height: 270,
    width: 640,
    code: [
      'MMU 查页表',
      '有对应的物理页且权限允许 → 直接访问',
      '否则触发缺页异常，进入内核',
      '写：分配一个物理页，清零',
      '更新页表，重新执行这条指令（读：映射到零页）'
    ],
    init: faultInit,
    view: faultView,
    start: function (m, rec) { access(m, 0, true, rec); access(m, 0, true, rec); access(m, 1, false, rec); access(m, 1, true, rec); },
    actions: [
      { label: '写', fields: [{ key: 'i', label: '虚拟页', value: 2, size: 2 }],
        check: function (m, p) { if (typeof p.i !== 'number' || p.i < 0 || p.i >= VP) return '请输入 0～5'; },
        run: function (m, p, rec) { access(m, p.i, true, rec); } },
      { label: '读', fields: [{ key: 'i', label: '虚拟页', value: 3, size: 2 }],
        check: function (m, p) { if (typeof p.i !== 'number' || p.i < 0 || p.i >= VP) return '请输入 0～5'; },
        run: function (m, p, rec) { access(m, p.i, false, rec); } },
      { label: '重置', run: function (m, p, rec) { var e = faultInit(); m.pt = e.pt; m.frames = e.frames; m.faults = 0; rec.step(1, '重置。', {}); } }
    ]
  });

  /* ---------- 堆与 malloc_trim ---------- */

  var PAGES = 4, PER = 4;

  function heapView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    texts.push(txt('t', 20, 20, '[heap]：' + PAGES + ' 个页，每页 ' + PER + ' 个块', 'muted'));
    for (var pg = 0; pg < PAGES; pg++) {
      var resident = m.rss[pg];
      boxes.push(box('pg' + pg, 20 + pg * 150, 32, 142, 84, '', resident ? null : 'dim', resident ? '页 ' + pg + '：在内存中' : '页 ' + pg + '：已还给内核'));
      for (var k = 0; k < PER; k++) {
        var c = pg * PER + k, used = m.used[c];
        boxes.push(box('c' + c, 26 + pg * 150 + k * 34, 52, 30, 30, used ? '用' : '空', hl[c] || (used ? 'cur' : (resident ? null : 'dim'))));
      }
    }
    boxes.push(box('brk', 20 + m.brk * 150 - 46, 136, 40, 26, 'brk', 'warn'));   // 标在堆顶那一页的右下角
    texts.push(txt('rss', 20, 186, 'RSS：' + m.rss.filter(Boolean).length + ' 页；堆顶（brk）在第 ' + m.brk + ' 页之后', null));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function heapInit() { return { used: [], rss: [], brk: PAGES }; }
  function heapFill(m) { m.used = []; m.rss = []; for (var c = 0; c < PAGES * PER; c++) m.used.push(true); for (var p = 0; p < PAGES; p++) m.rss.push(true); m.brk = PAGES; }

  function heapRun(m, keep, rec) {
    heapFill(m);
    rec.step(1, '分配了 16 个小块，堆扩展到 4 个页，每页都已写入，RSS 4 页。', {});
    for (var c = 0; c < PAGES * PER; c++) if (c !== keep) m.used[c] = false;
    var hl = {}; hl[keep] = 'warn';
    rec.step(2, '释放除第 ' + keep + ' 块以外的所有块。这些块回到 malloc 的空闲链表中。', { hl: hl });
    var top = 0;
    for (var c2 = 0; c2 < PAGES * PER; c2++) if (m.used[c2]) top = Math.floor(c2 / PER) + 1;
    if (top < m.brk) {
      for (var p = top; p < PAGES; p++) m.rss[p] = false;
      m.brk = top;
      rec.step(3, '最后一个在用的块之后，' + (PAGES - top) + ' 个页全部空闲：调用 brk 把堆缩回去，这些页还给了内核。（实际中，刚释放的小块会先留在 tcache 等缓存里，可能连这一步也不会发生。）', { hl: hl });
    } else {
      rec.step(3, '第 ' + keep + ' 块在最后一个页中，挡住了堆顶：brk 只能移动堆的末尾，一个页也缩不回去。RSS 仍然是 4 页。（实测：释放了 99.9%，RSS +114.7 MB 不变）', { hl: hl });
    }
    for (var p2 = 0; p2 < m.brk; p2++) {
      var any = false;
      for (var k = 0; k < PER; k++) if (m.used[p2 * PER + k]) any = true;
      if (!any) m.rss[p2] = false;
    }
    rec.step(4, 'malloc_trim(0)：找出堆中间完整空闲的页，用 madvise(MADV_DONTNEED) 还给内核（地址保留，下次用到时再缺页）。（实测：降到了 +11.9 MB）', { hl: hl });
  }

  DSViz.register('lx-heap', {
    title: '堆只能从顶端收缩：一个块就能挡住所有的空闲页',
    height: 200,
    width: 640,
    code: [
      '分配很多小块',
      'free 其中的大部分',
      '堆顶空闲？用 brk 缩小堆',
      'malloc_trim：把中间整页空闲的内存 madvise 还给内核'
    ],
    init: heapInit,
    view: heapView,
    start: function (m, rec) { heapRun(m, 15, rec); },
    actions: [
      { label: '留下最后一块', run: function (m, p, rec) { heapRun(m, 15, rec); } },
      { label: '留下第一块', run: function (m, p, rec) { heapRun(m, 0, rec); } },
      { label: '留下第 6 块', run: function (m, p, rec) { heapRun(m, 6, rec); } }
    ]
  });

  /* ---------- 脏页与写回 ---------- */

  var BG = 171, LIMIT = 342, DISK = 100;   // MB，MB/s

  function dirtyView(m, x) {
    var boxes = [], texts = [];
    var S = 1.5;   // 每 MB 的像素
    texts.push(txt('t', 20, 20, '页缓存中的脏页（Dirty + Writeback）', 'muted'));
    boxes.push(box('bar', 20, 34, Math.max(2, x.dirty * S), 34, Math.round(x.dirty) + ' MB', x.dirty >= LIMIT - 20 ? 'warn' : 'cur'));
    boxes.push(box('bg', 20 + BG * S - 1, 74, 2, 20, '', 'dim'));
    boxes.push(box('lim', 20 + LIMIT * S - 1, 74, 2, 20, '', 'warn'));
    texts.push(txt('bgt', 20 + BG * S, 110, '后台写回阈值 ≈ 171 MB', 'muted', 'middle'));
    texts.push(txt('limt', 20 + LIMIT * S, 128, '上限 ≈ 342 MB', 'muted', 'middle'));
    texts.push(txt('w', 20, 160, '已写入 ' + x.written + ' MB；这一段 write 的速度：' + x.speed, null));
    texts.push(txt('d', 20, 182, '磁盘：' + (x.writing ? '正在以 100 MB/s 写回' : '空闲'), x.writing ? null : 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function dirtyRun(m, p, rec) {
    var dirty = 0, written = 0, t = 0;
    rec.step(1, '程序不停地 write，每次 64 MB。磁盘只有 100 MB/s。', { dirty: 0, written: 0, speed: '—', writing: false });
    for (var i = 0; i < 12; i++) {
      var chunk = 64, writing = dirty > BG;
      if (dirty + chunk <= LIMIT) {
        var dt = chunk / 5000;
        t += dt;
        if (writing) dirty -= DISK * dt;
        dirty += chunk; written += chunk;
        rec.step(writing ? 3 : 2, writing ? '脏页超过了后台阈值，内核开始写回；但还没到上限，write 仍然只是复制进内存，5 GB/s。'
          : 'write 把数据复制进页缓存，标记为脏页，立即返回：约 5 GB/s。', { dirty: dirty, written: written, speed: '约 5000 MB/s', writing: dirty > BG });
      } else {
        // 到了上限：write 要等磁盘写回腾出空间
        dirty = LIMIT;
        written += chunk;
        rec.step(4, '脏页到了上限：write 的进程被内核暂停，直到写回腾出空间。写 64 MB 要等磁盘写出 64 MB，速度降到约 100 MB/s。（实测：从第 3 个 128 MB 开始稳定在 99～109 MB/s）', { dirty: dirty, written: written, speed: '约 100 MB/s（被限速）', writing: true });
      }
    }
    rec.step(5, '页缓存只能吸收一段突发的写入；持续写入的速度最终由磁盘决定。', { dirty: dirty, written: written, speed: '约 100 MB/s', writing: true });
  }

  DSViz.register('lx-dirty', {
    title: '脏页与写回：到了上限，write 被限速到磁盘的速度',
    height: 195,
    width: 640,
    code: [
      'write(fd, buf, 64 MB)',
      '  复制进页缓存，标记为脏页，返回',
      '  脏页 > 后台阈值：唤醒写回线程',
      '  脏页 > 上限：让写入的进程等待写回',
      '持续写入的速度 = 磁盘的速度'
    ],
    init: function () { return {}; },
    view: dirtyView,
    start: function (m, rec) { dirtyRun(m, {}, rec); },
    actions: [{ label: '重新演示', run: dirtyRun }]
  });

  /* ---------- 写时复制 ---------- */

  function cowInit() {
    return { par: [1, 2, 3, 4], chi: [1, 2, 3, 4], ro: true, frames: { 1: 'A', 2: 'B', 3: 'C', 4: 'D' }, next: 5, copies: 0 };
  }

  function cowView(m, x) {
    var boxes = [], texts = [], arrows = [], hl = x.hl || {};
    texts.push(txt('p', 20, 20, '父进程的页表', 'muted'));
    texts.push(txt('c', 20, 190, '子进程的页表', 'muted'));
    texts.push(txt('f', 360, 20, '物理页', 'muted'));
    for (var i = 0; i < 4; i++) {
      var pr = m.par[i], cr = m.chi[i];
      var shared = pr === cr;
      boxes.push(box('pe' + i, 20, 30 + i * 36, 140, 28, '页 ' + i + (shared ? '（只读）' : '（可写）'), hl['pe' + i]));
      boxes.push(box('ce' + i, 20, 200 + i * 36, 140, 28, '页 ' + i + (shared ? '（只读）' : '（可写）'), hl['ce' + i]));
    }
    var ids = Object.keys(m.frames).map(Number).sort(function (a, b) { return a - b; });
    ids.forEach(function (f, k) {
      boxes.push(box('f' + f, 360, 30 + k * 40, 120, 30, '物理页 ' + f + '：' + m.frames[f], hl['f' + f]));
    });
    for (var j = 0; j < 4; j++) {
      arrows.push({ from: 'pe' + j, to: 'f' + m.par[j], hl: hl['pe' + j] ? 'ok' : null });
      arrows.push({ from: 'ce' + j, to: 'f' + m.chi[j], hl: hl['ce' + j] ? 'ok' : null });
    }
    texts.push(txt('n', 520, 50, '复制了 ' + m.copies + ' 页', null));
    return { kind: 'free', boxes: boxes, texts: texts, arrows: arrows, vars: x.vars || {} };
  }

  function cowWrite(m, who, i, rec) {
    var tbl = who === 'p' ? m.par : m.chi, other = who === 'p' ? m.chi : m.par;
    var key = (who === 'p' ? 'pe' : 'ce') + i, vars = { op: (who === 'p' ? '父进程' : '子进程') + '写页 ' + i };
    var h = {}; h[key] = 'cur';
    if (tbl[i] !== other[i]) {
      h['f' + tbl[i]] = 'ok';
      rec.step(5, '这一页已经是私有的了（之前复制过），直接写，不再缺页。', { hl: h, vars: vars });
      return;
    }
    h['f' + tbl[i]] = 'warn';
    rec.step(2, '这一页被父子进程共享，页表项是只读的：写入触发缺页。', { hl: h, vars: vars });
    var nf = m.next++;
    m.frames[nf] = m.frames[tbl[i]] + '′';
    m.copies++;
    h['f' + nf] = 'new';
    rec.step(3, '内核分配一个新的物理页 ' + nf + '，把内容复制过去（4 KB）。', { hl: h, vars: vars });
    tbl[i] = nf;
    h[key] = 'new';
    rec.step(4, '写的一方指向新页并恢复可写；另一方继续使用原来的页。没被写的页仍然共享。', { hl: h, vars: vars });
  }

  DSViz.register('lx-cow', {
    title: 'fork 与写时复制：共享物理页，谁写谁复制',
    height: 350,
    width: 640,
    code: [
      'fork：复制页表，所有私有页标记为只读',
      '写一个共享的页 → 缺页',
      '分配新页，复制 4 KB',
      '写的一方指向新页，恢复可写',
      '已经私有的页：直接写'
    ],
    init: cowInit,
    view: cowView,
    start: function (m, rec) {
      rec.step(1, 'fork 以后：子进程的页表是父进程的副本，指向同样的 4 个物理页，双方都标记为只读。没有复制任何数据。', { vars: {} });
      cowWrite(m, 'c', 1, rec);
      cowWrite(m, 'p', 3, rec);
    },
    actions: [
      { label: '子进程写', fields: [{ key: 'i', label: '页', value: 2, size: 2 }],
        check: function (m, p) { if (typeof p.i !== 'number' || p.i < 0 || p.i > 3) return '请输入 0～3'; if (m.next > 8) return '演示的物理页用完了，先重置'; },
        run: function (m, p, rec) { cowWrite(m, 'c', p.i, rec); } },
      { label: '父进程写', fields: [{ key: 'i', label: '页', value: 0, size: 2 }],
        check: function (m, p) { if (typeof p.i !== 'number' || p.i < 0 || p.i > 3) return '请输入 0～3'; if (m.next > 8) return '演示的物理页用完了，先重置'; },
        run: function (m, p, rec) { cowWrite(m, 'p', p.i, rec); } },
      { label: '重置', run: function (m, p, rec) { var e = cowInit(); Object.keys(e).forEach(function (k) { m[k] = e[k]; }); rec.step(1, '重新 fork。', { vars: {} }); } }
    ]
  });
})();
