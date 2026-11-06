/* 《容器原理》一章的逐步演示
 *
 * lx-pid1     容器中的 1 号进程：孤儿进程被过继给它；不回收就成了僵尸；没有处理函数的信号被忽略
 * lx-overlay  overlayfs：读穿透到下层；第一次写先把整个文件复制到上层；删除留下白化文件
 * lx-cpumax   CPU 配额：4 个线程在周期的前一半用光配额，后一半全部暂停
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- 1 号进程 ---------- */

  function pidView(m, x) {
    var boxes = [], texts = [], arrows = [], hl = x.hl || {};
    texts.push(txt('t', 20, 20, '容器的 PID 命名空间', 'muted'));
    boxes.push(box('p1', 250, 34, 140, 40, '1 号进程', hl.p1 || 'cur', m.mode === 'reap' ? '会回收、会转发信号' : '只等自己的命令'));
    var procs = m.procs;
    procs.forEach(function (p, i) {
      var x0 = 30 + i * 150;
      boxes.push(box('q' + p.id, x0, 130, 130, 40, p.name + '（' + p.id + '）', hl['q' + p.id] || (p.zombie ? 'warn' : (p.gone ? 'dim' : null)), p.zombie ? '僵尸' : (p.gone ? '已回收' : p.note)));
      if (!p.gone) arrows.push({ from: p.parent === 1 ? 'p1' : 'q' + p.parent, to: 'q' + p.id, hl: hl['a' + p.id] });
    });
    if (x.sig) boxes.push(box('sig', 440, 34, 180, 40, x.sig, 'new'));
    texts.push(txt('n', 20, 210, x.note || '', 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, arrows: arrows, vars: x.vars || {} };
  }

  function pidRun(m, mode, rec) {
    m.mode = mode;
    m.procs = [{ id: 2, name: 'sh', parent: 1, note: '命令' }];
    rec.step(1, '容器的 1 号进程启动了命令 sh（2 号）。', {});
    m.procs.push({ id: 4, name: 'sleep', parent: 2, note: '后台运行' });
    rec.step(2, 'sh 在一个子 shell 中把 sleep 放到后台，子 shell 退出。', { hl: { q4: 'new' } });
    m.procs[1].parent = 1; m.procs[1].note = '孤儿';
    rec.step(2, 'sleep 的父进程不在了，它成了孤儿，被过继给这个命名空间的 1 号进程。', { hl: { q4: 'cur', a4: 'cur' } });
    if (mode === 'lazy') {
      m.procs[1].zombie = true;
      rec.step(3, 'sleep 退出了，等父进程（1 号进程）调用 wait。可是 1 号进程只等自己启动的 sh，不管它：sleep 成了僵尸，一直占着进程号。', { hl: { q4: 'warn' } });
      rec.step(4, '从外面发 SIGTERM 给 1 号进程：它没有为 SIGTERM 设置处理函数，内核直接忽略。docker stop 只能等 10 秒后发 SIGKILL。', { sig: 'SIGTERM：被忽略', hl: { p1: 'warn' }, note: '实测：docker stop 10.11 秒' });
    } else {
      m.procs[1].gone = true;
      rec.step(3, 'sleep 退出了，1 号进程在 waitpid(-1) 中收到它，回收掉，不会留下僵尸。', { hl: { q4: 'ok', p1: 'ok' } });
      m.procs[0].gone = true;
      rec.step(4, '从外面发 SIGTERM：1 号进程有处理函数，把信号转发给 sh；sh 不是 1 号进程，按默认动作退出，1 号进程随之退出。', { sig: 'SIGTERM：转发给命令', hl: { p1: 'ok' }, note: '实测：docker stop --init 0.06 秒' });
    }
  }

  DSViz.register('lx-pid1', {
    title: '容器中的 1 号进程：孤儿、僵尸与信号',
    height: 230,
    width: 640,
    code: [
      '1 号进程启动命令',
      '孤儿进程被过继给 1 号进程',
      '孤儿退出：1 号进程要 wait 它',
      '收到 SIGTERM：没有处理函数就被忽略'
    ],
    init: function () { return { procs: [], mode: 'lazy' }; },
    view: pidView,
    start: function (m, rec) { pidRun(m, 'lazy', rec); },
    actions: [
      { label: '普通的应用做 1 号进程', run: function (m, p, rec) { pidRun(m, 'lazy', rec); } },
      { label: '用 tini 做 1 号进程', run: function (m, p, rec) { pidRun(m, 'reap', rec); } }
    ]
  });

  /* ---------- overlayfs ---------- */

  function ovlInit() {
    return {
      lower: { 'big.bin': '500 MB', 'small.txt': 'hello', 'old.txt': 'bye' },
      upper: {}
    };
  }

  function ovlView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    var names = ['big.bin', 'small.txt', 'old.txt'];
    texts.push(txt('t1', 20, 20, '合并视图（容器看到的 /）', 'muted'));
    texts.push(txt('t2', 20, 112, '上层 upper（容器自己的，可写）', 'muted'));
    texts.push(txt('t3', 20, 192, '下层 lower（镜像，只读）', 'muted'));
    names.forEach(function (n, i) {
      var X = 20 + i * 200, up = m.upper[n], lo = m.lower[n];
      var visible = up ? (up === 'whiteout' ? null : up) : lo;
      boxes.push(box('m' + i, X, 30, 180, 40, visible ? n : '（看不到）', hl['m' + i] || (visible ? null : 'dim'), visible ? (up ? '来自上层' : '来自下层') : null));
      boxes.push(box('u' + i, X, 122, 180, 40, up ? (up === 'whiteout' ? n + '：白化文件' : n + '（' + up + '）') : '', hl['u' + i] || (up ? (up === 'whiteout' ? 'warn' : 'new') : 'dim')));
      boxes.push(box('l' + i, X, 202, 180, 40, n + '（' + lo + '）', hl['l' + i]));
    });
    if (x.note) texts.push(txt('n', 20, 268, x.note, 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function ovlRun(m, what, rec) {
    if (what === 'read') {
      rec.step(1, '读 big.bin：上层没有这个文件，直接读下层。不复制任何东西。', { hl: { m0: 'cur', l0: 'ok' } });
    } else if (what === 'write') {
      if (m.upper['big.bin']) { rec.step(3, 'big.bin 已经在上层了，直接写，和普通文件一样快。（实测 open 0.0 ms）', { hl: { m0: 'cur', u0: 'ok' } }); return; }
      rec.step(2, '以可写方式打开 big.bin：它在只读的下层，不能直接改。', { hl: { m0: 'cur', l0: 'warn' } });
      m.upper['big.bin'] = '500 MB 副本';
      rec.step(2, 'copy-up：把整个文件复制到上层，哪怕只改 1 个字节。（实测 open 用了 300 ms，上层多了 501 MB）', { hl: { u0: 'new', l0: 'cur' } });
      rec.step(3, '之后的读写都在上层的副本上进行，下层的原文件不变，别的容器看到的仍然是原文件。', { hl: { m0: 'ok', u0: 'ok' } });
    } else {
      m.upper['old.txt'] = 'whiteout';
      rec.step(4, '删除 old.txt：下层是只读的，删不掉。在上层创建一个设备号为 0, 0 的字符设备（白化文件），合并视图中就看不到它了。镜像不会因此变小。', { hl: { u2: 'warn', m2: 'dim', l2: 'cur' } });
    }
  }

  DSViz.register('lx-overlay', {
    title: 'overlayfs：只读的下层加可写的上层',
    height: 260,
    width: 640,
    code: [
      '读：上层没有，就读下层',
      '第一次写下层的文件：整个复制到上层（copy-up）',
      '之后：读写上层的副本',
      '删除下层的文件：在上层放一个白化文件'
    ],
    init: ovlInit,
    view: ovlView,
    start: function (m, rec) { ovlRun(m, 'read', rec); ovlRun(m, 'write', rec); },
    actions: [
      { label: '读 big.bin', run: function (m, p, rec) { ovlRun(m, 'read', rec); } },
      { label: '写 big.bin', run: function (m, p, rec) { ovlRun(m, 'write', rec); } },
      { label: '删除 old.txt', run: function (m, p, rec) { ovlRun(m, 'rm', rec); } },
      { label: '重置', run: function (m, p, rec) { var e = ovlInit(); m.lower = e.lower; m.upper = e.upper; rec.step(1, '重新挂载：上层是空的。', {}); } }
    ]
  });

  /* ---------- CPU 配额 ---------- */

  // 时间轴：2 个周期，每个周期 100 ms，每格 10 ms；2 个 CPU
  function quotaView(m, x) {
    var boxes = [], texts = [];
    texts.push(txt('t', 20, 20, 'cpu.max = 100000 100000（每 100 ms 最多用 100 ms CPU）；' + x.threads + ' 个线程，2 个 CPU', 'muted'));
    for (var c = 0; c < 2; c++) {
      texts.push(txt('c' + c, 20, 56 + c * 44, 'CPU ' + c, null));
      for (var s = 0; s < 20; s++) {
        var st = x.cells[c][s];
        var hl = st === 'run' ? 'cur' : (st === 'thr' ? 'warn' : 'dim');
        boxes.push(box('k' + c + '_' + s, 80 + s * 27, 36 + c * 44, 25, 34, '', s <= x.t ? hl : 'dim'));
      }
    }
    texts.push(txt('p0', 80, 140, '| 周期 1', 'muted'));
    texts.push(txt('p1', 80 + 10 * 27, 140, '| 周期 2', 'muted'));
    texts.push(txt('u', 20, 170, '本周期已用 ' + x.used + ' ms / 100 ms　　（绿色：运行；红框：被暂停；每格 10 ms）', null));
    if (x.note) texts.push(txt('n', 20, 196, x.note, 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function quotaRun(threads, rec) {
    var cells = [[], []];
    for (var s = 0; s < 20; s++) { cells[0].push(null); cells[1].push(null); }
    var cpus = Math.min(threads, 2), used = 0;
    rec.step(1, threads + ' 个线程一直在计算。每个周期开始时，配额恢复为 100 ms。', { threads: threads, cells: cells, t: -1, used: 0 });
    for (var t = 0; t < 20; t++) {
      if (t % 10 === 0) used = 0;
      for (var c = 0; c < 2; c++) {
        if (c >= cpus) { cells[c][t] = null; continue; }
        if (used < 100) { cells[c][t] = 'run'; used += 10; }
        else cells[c][t] = 'thr';
      }
      var snap = JSON.parse(JSON.stringify(cells));
      var thr = snap[0][t] === 'thr';
      if (t % 10 === 0 || t % 10 === 4 || t % 10 === 5 || t === 19)
        rec.step(thr ? 3 : 2, thr ? '配额已经用完：这个 cgroup 中的所有线程都被暂停，直到下一个周期开始。正在处理的请求要多等这么久。'
          : (cpus === 2 ? '2 个 CPU 同时运行，每 10 ms 消耗 20 ms 的配额。' : '只有 1 个线程，每 10 ms 只消耗 10 ms 的配额。'),
          { threads: threads, cells: snap, t: t, used: Math.min(used, 100) });
    }
    rec.step(4, cpus === 2
      ? '每个周期前 50 ms 运行、后 50 ms 全部暂停。平均正好 1 个 CPU，但碰上暂停的请求要多等 50 ms：实测 p99 从 13.5 ms 变成 65 ms。'
      : '1 个线程永远用不完配额，一直在运行，没有暂停：同样的吞吐量，p99 只有 5 ms。',
      { threads: threads, cells: JSON.parse(JSON.stringify(cells)), t: 19, used: Math.min(used, 100), note: cpus === 2 ? '吞吐量一样，延迟差了一个数量级' : '' });
  }

  DSViz.register('lx-cpumax', {
    title: 'CPU 配额：周期的前一半用光，后一半全部暂停',
    height: 210,
    width: 640,
    code: [
      '周期开始，配额恢复',
      '线程运行，消耗配额',
      '配额用完：所有线程暂停到周期结束',
      '统计：平均使用率不变，尾延迟变大'
    ],
    init: function () { return {}; },
    view: quotaView,
    start: function (m, rec) { quotaRun(4, rec); },
    actions: [
      { label: '4 个线程', run: function (m, p, rec) { quotaRun(4, rec); } },
      { label: '1 个线程', run: function (m, p, rec) { quotaRun(1, rec); } }
    ]
  });
})();
