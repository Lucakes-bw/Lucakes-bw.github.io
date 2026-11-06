/* 《分布式系统》专栏「分布式基础」一章的逐步演示
 *
 * dist-split    部分网络分区下的自动故障转移：两个主节点，以及任期号能挡住什么
 * dist-lamport  Lamport 逻辑时钟：本地事件加一，收到消息取最大值再加一
 * dist-quorum   法定人数：写 W 个、读 R 个，W + R > N 时一定相交
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- 脑裂 ---------- */

  // 三个节点排成一行：n1（旧主节点）在左，n3 在中间，n2 在右
  var POS = { n1: 20, n3: 245, n2: 470 };

  function splitInit() {
    return {
      fence: false, cut: false,
      nodes: {
        n1: { role: '主', epoch: 1, log: [] },
        n2: { role: '从', epoch: 1, log: [] },
        n3: { role: '从', epoch: 1, log: [] }
      }
    };
  }

  function splitView(m, x) {
    var boxes = [], texts = [], arrows = [], hl = x.hl || {};
    texts.push(txt('cut', 320, 18, 'n1 和 n2 之间的网络：' + (m.cut ? '已切断 ✕' : '正常'), m.cut ? null : 'muted', 'middle'));
    texts.push(txt('ca', 95, 46, '客户端 a 写 n1', 'muted', 'middle'));
    texts.push(txt('cb', 545, 46, '客户端 b 写 n2', 'muted', 'middle'));
    ['n1', 'n3', 'n2'].forEach(function (id) {
      var n = m.nodes[id];
      boxes.push(box(id, POS[id], 56, 150, 44, id + ' ' + n.role + ' 任期' + n.epoch, hl[id] || (n.role === '主' ? 'cur' : null),
        '日志：' + (n.log.length ? n.log.join(' ') : '（空）')));
    });
    (x.arrows || []).forEach(function (a) { arrows.push({ from: a[0], to: a[1], hl: a[2] || 'cur', label: a[3] }); });
    if (x.note) texts.push(txt('note', 20, 150, x.note, null));
    if (x.note2) texts.push(txt('note2', 20, 174, x.note2, 'muted'));
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars || {} };
  }

  function has(n, e) { return n.log.indexOf(e) >= 0; }

  function splitRun(m, rec) {
    var N = m.nodes;
    // 1. 正常复制
    ['a1', 'a2'].forEach(function (e) { N.n1.log.push(e); N.n2.log.push(e); N.n3.log.push(e); });
    rec.step(1, 'n1 是主节点，写入 a1、a2，复制到 n2、n3，等一个从节点确认后告诉客户端成功。',
      { arrows: [['n1', 'n3', 'cur', 'a1 a2']], hl: { n2: 'new', n3: 'new' } });
    // 2. 切断
    m.cut = true;
    rec.step(2, '切断 n1 和 n2 之间的网络。n1 其实还活着，和 n3、客户端之间也是通的。', { hl: { n2: 'warn' } });
    // 3. n1 继续写，n3 确认
    N.n1.log.push('a3'); N.n3.log.push('a3');
    rec.step(3, 'n1 继续工作：写入 a3，n3 确认，n1 告诉客户端 a 成功。n2 收不到。',
      { arrows: [['n1', 'n3', 'cur', 'a3']], hl: { n2: 'dim' }, note: 'a3 已经确认：它在 n1、n3 上，不在 n2 上。' });
    // 4. n2 晋升
    N.n2.role = '主'; N.n2.epoch = 2;
    rec.step(4, 'n2 一秒收不到 n1 的心跳，自己晋升为主节点，任期 1 → 2。它不知道自己缺了 a3。',
      { hl: { n2: 'new' }, note: '现在有两个主节点：n1（任期 1）和 n2（任期 2）。' });
    // 5. n2 写 b1，复制给 n3
    N.n2.log.push('b1'); N.n3.log.push('b1'); N.n3.epoch = 2;
    rec.step(5, '客户端 b 写入 b1，n2 把它复制给 n3。n3 由此得知任期 2。',
      { arrows: [['n2', 'n3', 'cur', 'b1']], hl: { n3: 'new' } });
    // 6. n1 又写 a4
    if (!m.fence) {
      N.n1.log.push('a4'); N.n3.log.push('a4');
      rec.step(6, '不检查任期：n1（任期 1）写入 a4，n3 照样确认。两个主节点都在告诉客户端「成功」：脑裂。',
        { arrows: [['n1', 'n3', 'warn', 'a4']], hl: { n1: 'warn', n2: 'warn' }, note: 'n3 同时为两个主节点确认写入。' });
    } else {
      N.n1.role = '从'; N.n1.epoch = 2;
      rec.step(6, '检查任期：n1 发来任期 1 的 a4，n3 回复「我已经在任期 2 了」并拒绝。n1 知道自己过时了，立刻退位，a4 失败。',
        { arrows: [['n1', 'n3', 'warn', 'a4 ✕']], hl: { n1: 'dim', n3: 'cmp' }, note: '脑裂结束：只剩 n2 一个主节点。客户端 a 收到错误，而不是假的「成功」。' });
    }
    // 7. 结论
    var lost = ['a3', 'a4'].filter(function (e) { return has(N.n1, e) && !has(N.n2, e); });
    var hl7 = {};
    if (!m.fence) { hl7.n1 = 'warn'; hl7.n2 = 'warn'; hl7.n3 = 'warn'; } else { hl7.n2 = 'warn'; }
    rec.step(7, m.fence
      ? '但 a3 还是丢了：它在晋升之前就确认了，在 n1、n3 上，偏偏不在新主节点 n2 上。晋升的不是最新的节点。'
      : '结果：' + lost.join('、') + ' 确认过却不在新主节点 n2 上；n3 两边的写入都有。三个节点的数据各不相同。',
      { hl: hl7, note: m.fence ? '要不丢 a3，晋升必须经过投票，而且只能选日志最新的 n3：这就是 Raft。' : '确认过的写入丢失：' + lost.join('、') + '。',
        note2: m.fence ? '' : '打开「检查任期」再看一遍。' });
  }

  DSViz.register('dist-split', {
    title: '部分网络分区下的自动故障转移',
    height: 190,
    width: 640,
    code: [
      '正常复制：等一个从节点确认',
      '切断 n1 和 n2 之间的网络',
      'n1 继续写入，n3 确认',
      'n2 收不到心跳，自己晋升，任期 +1',
      'n2 写入，复制给 n3',
      'n1 再写入：n3 接受还是拒绝？',
      '结果'
    ],
    init: splitInit,
    view: splitView,
    start: function (m, rec) { splitRun(m, rec); },
    actions: [
      { label: '不检查任期', run: function (m, p, rec) { var e = splitInit(); Object.keys(e).forEach(function (k) { m[k] = e[k]; }); splitRun(m, rec); } },
      { label: '检查任期', run: function (m, p, rec) { var e = splitInit(); Object.keys(e).forEach(function (k) { m[k] = e[k]; }); m.fence = true; splitRun(m, rec); } }
    ]
  });

  /* ---------- Lamport 时钟 ---------- */

  var COLS = 9, X0 = 70, DX = 62;

  function lamInit() { return { L: [0, 0, 0], events: [], col: 0, sent: {} }; }

  function lamView(m, x) {
    var boxes = [], texts = [], arrows = [], hl = x.hl || {};
    var first = Math.max(0, m.col - COLS);
    for (var p = 0; p < 3; p++) {
      texts.push(txt('p' + p, 14, 44 + p * 62, 'P' + (p + 1), null));
      texts.push(txt('pl' + p, 14, 60 + p * 62, 'L=' + m.L[p], 'muted'));
    }
    m.events.forEach(function (e) {
      if (e.col < first) return;
      var bx = X0 + (e.col - first) * DX;
      boxes.push(box(e.id, bx, 22 + e.p * 62, 54, 40, 'L=' + e.L, hl[e.id] || (e.kind === 'recv' ? 'ok' : null), e.label));
      if (e.from && m.events.some(function (o) { return o.id === e.from && o.col >= first; }))
        arrows.push({ from: e.from, to: e.id, hl: hl[e.id] ? 'cur' : null });
    });
    if (x.note) texts.push(txt('n', 14, 228, x.note, null));
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars || {} };
  }

  function addEvent(m, p, kind, label, from) {
    var id = 'e' + m.events.length;
    m.events.push({ id: id, p: p, col: m.col++, L: m.L[p], kind: kind, label: label, from: from });
    return id;
  }

  function lamLocal(m, p, rec) {
    m.L[p] += 1;
    var id = addEvent(m, p, 'local', '本地');
    var h = {}; h[id] = 'new';
    rec.step(1, 'P' + (p + 1) + ' 发生一个本地事件：L 加一，变成 ' + m.L[p] + '。', { hl: h });
  }

  function lamSend(m, a, b, rec) {
    m.L[a] += 1;
    var sid = addEvent(m, a, 'send', '发→P' + (b + 1));
    var h1 = {}; h1[sid] = 'new';
    rec.step(2, 'P' + (a + 1) + ' 发消息给 P' + (b + 1) + '：发送也是一个事件，L 加一变成 ' + m.L[a] + '，消息中带上 ' + m.L[a] + '。', { hl: h1 });
    var old = m.L[b];
    m.L[b] = Math.max(m.L[b], m.L[a]) + 1;
    var rid = addEvent(m, b, 'recv', '收P' + (a + 1), sid);
    var h2 = {}; h2[rid] = 'new';
    rec.step(3, 'P' + (b + 1) + ' 收到消息：L = max(' + old + ', ' + m.L[a] + ') + 1 = ' + m.L[b] + '。收消息一定排在发消息之后。',
      { hl: h2, note: '发送 L=' + m.L[a] + ' < 接收 L=' + m.L[b] + '：因果关系被保住了。' });
  }

  DSViz.register('dist-lamport', {
    title: 'Lamport 逻辑时钟',
    height: 240,
    width: 640,
    code: [
      '本地事件：L = L + 1',
      '发送消息：L = L + 1，消息带上 L',
      '收到消息：L = max(L, 消息中的 L) + 1'
    ],
    init: lamInit,
    view: lamView,
    start: function (m, rec) {
      lamLocal(m, 0, rec);
      lamSend(m, 0, 1, rec);
      lamLocal(m, 2, rec);
      lamLocal(m, 1, rec);
      lamSend(m, 1, 2, rec);
    },
    actions: [
      { label: '本地事件', fields: [{ key: 'p', label: '进程', value: 1, size: 2 }],
        check: function (m, p) { if ([1, 2, 3].indexOf(+p.p) < 0) return '进程请输入 1、2 或 3'; },
        run: function (m, p, rec) { lamLocal(m, +p.p - 1, rec); } },
      { label: '发消息', fields: [{ key: 'a', label: '从', value: 3, size: 2 }, { key: 'b', label: '到', value: 1, size: 2 }],
        check: function (m, p) {
          if ([1, 2, 3].indexOf(+p.a) < 0 || [1, 2, 3].indexOf(+p.b) < 0) return '进程请输入 1、2 或 3';
          if (+p.a === +p.b) return '发给别的进程';
        },
        run: function (m, p, rec) { lamSend(m, +p.a - 1, +p.b - 1, rec); } },
      { label: '重置', run: function (m, p, rec) { var e = lamInit(); Object.keys(e).forEach(function (k) { m[k] = e[k]; }); rec.step(1, '重新开始：三个进程的 L 都是 0。', {}); } }
    ]
  });

  /* ---------- 法定人数 ---------- */

  function qInit() { return { n: 5, w: 3, r: 3, ver: [0, 0, 0, 0, 0], latest: 0, seed: 7 }; }

  function rnd(m) { m.seed = (m.seed * 1103515245 + 12345) % 2147483648; return m.seed / 2147483648; }

  function pick(m, k) {
    var idx = [];
    for (var i = 0; i < m.n; i++) idx.push(i);
    for (var j = idx.length - 1; j > 0; j--) { var t = Math.floor(rnd(m) * (j + 1)), s = idx[j]; idx[j] = idx[t]; idx[t] = s; }
    return idx.slice(0, k).sort(function (a, b) { return a - b; });
  }

  function qView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    var w = Math.min(80, Math.floor(600 / m.n) - 8);
    texts.push(txt('h', 20, 20, 'N = ' + m.n + '，W = ' + m.w + '，R = ' + m.r + '：W + R = ' + (m.w + m.r) + (m.w + m.r > m.n ? ' > N，一定相交' : ' ≤ N，可能不相交'), null));
    for (var i = 0; i < m.n; i++) {
      boxes.push(box('s' + i, 20 + i * (w + 8), 40, w, 50, '副本' + (i + 1), hl['s' + i] || (m.ver[i] === m.latest && m.latest ? 'ok' : 'dim'), '版本 ' + m.ver[i]));
    }
    if (x.wset) texts.push(txt('ws', 20, 130, '写入确认的副本：' + x.wset.map(function (i) { return i + 1; }).join('、'), 'muted'));
    if (x.rset) texts.push(txt('rs', 20, 152, '读取的副本：' + x.rset.map(function (i) { return i + 1; }).join('、'), 'muted'));
    if (x.note) texts.push(txt('n', 20, 176, x.note, null));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function qWrite(m, rec) {
    m.latest += 1;
    var set = pick(m, m.w), h = {};
    set.forEach(function (i) { m.ver[i] = m.latest; h['s' + i] = 'new'; });
    rec.step(1, '写入版本 ' + m.latest + '：收到 ' + m.w + ' 个副本的确认就算成功。其余副本这一次没有收到（网络问题、宕机，或者还在路上）。',
      { hl: h, wset: set });
    return set;
  }

  function qRead(m, wset, rec) {
    var set = pick(m, m.r), h = {}, best = 0, inter = [];
    set.forEach(function (i) {
      h['s' + i] = m.ver[i] === m.latest ? 'cur' : 'warn';
      if (m.ver[i] > best) best = m.ver[i];
      if (wset && wset.indexOf(i) >= 0) inter.push(i + 1);
    });
    var ok = best === m.latest;
    rec.step(ok ? 2 : 3, '读取 ' + m.r + ' 个副本，取版本号最大的：版本 ' + best + '。' +
      (ok ? '读到了最新的值' + (inter.length ? '，交集是副本 ' + inter.join('、') : '') + '。' : '最新是版本 ' + m.latest + '，读到了旧值：读的副本中没有一个收到过最新的写入。'),
      { hl: h, wset: wset, rset: set, note: ok ? '✓ 读到最新值' : '✗ 读到旧值（W + R ≤ N 时可能发生）' });
  }

  DSViz.register('dist-quorum', {
    title: '法定人数读写：写 W 个，读 R 个',
    height: 190,
    width: 640,
    code: [
      '写：等 W 个副本确认',
      '读：读 R 个副本，取版本号最大的',
      '读到旧值：读写集合没有交集'
    ],
    init: qInit,
    view: qView,
    start: function (m, rec) {
      var s = qWrite(m, rec);
      qRead(m, s, rec);
    },
    actions: [
      { label: '设置', fields: [{ key: 'n', label: 'N', value: 5, size: 2 }, { key: 'w', label: 'W', value: 2, size: 2 }, { key: 'r', label: 'R', value: 2, size: 2 }],
        check: function (m, p) {
          var n = +p.n, w = +p.w, r = +p.r;
          if (!(n >= 1 && n <= 7)) return 'N 取 1～7';
          if (!(w >= 1 && w <= n && r >= 1 && r <= n)) return 'W、R 取 1～N';
        },
        run: function (m, p, rec) {
          m.n = +p.n; m.w = +p.w; m.r = +p.r; m.latest = 0; m.ver = [];
          for (var i = 0; i < m.n; i++) m.ver.push(0);
          rec.step(1, '设置 N = ' + m.n + '，W = ' + m.w + '，R = ' + m.r + '。点「写入并读取」试几次。', {});
        } },
      { label: '写入并读取', run: function (m, p, rec) { var s = qWrite(m, rec); qRead(m, s, rec); } },
      { label: '只读取', run: function (m, p, rec) { qRead(m, null, rec); } }
    ]
  });
})();
