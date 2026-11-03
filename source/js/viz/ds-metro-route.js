/* 实战项目《地铁换乘查询》里程碑 2～5 的逐步演示
 *
 * metro-bfs     里程碑 2：在示例城的地铁图上 BFS，找出站数最少的路线
 * metro-trap    里程碑 3：按车站做 Dijkstra，为什么会算错（港口 → 老街）
 * metro-states  里程碑 3：在（车站，线路）组成的状态图上做 Dijkstra
 * metro-pair    里程碑 4：代价是（换乘次数，用时）两个数，先比较换乘次数
 * metro-prefix  里程碑 5：在排好序的站名中，用两次二分查找找出以某个前缀开头的车站
 */
(function () {
  'use strict';

  var TRANSFER = 5;                                 // 和 route.h 中的 TRANSFER_MINUTES 相同

  /* ---------- 示例城的数据：和 data/metro.txt 相同，车站按第一次出现的顺序编号 ---------- */
  var LINES = [
    ['1号线', '西山', 3, '果园', 2, '动物园', 3, '人民广场', 2, '市政府', 3, '东门', 2, '科技园', 4, '机场'],
    ['2号线', '北站', 3, '体育馆', 2, '文化宫', 3, '人民广场', 2, '南湖', 3, '大学城', 4, '南站'],
    ['3号线', '植物园', 3, '果园', 4, '体育馆', 3, '老街', 2, '市政府', 3, '会展中心', 4, '东湖'],
    ['4号线', '西站', 4, '花园路', 3, '南湖', 2, '博物馆', 3, '东门', 3, '东湖', 2, '港口']
  ];
  // 线路图上的位置（网格坐标），和《项目总览》中的线路图一致
  var GRID = {
    '西山': [0, 4.5], '果园': [2, 4.5], '动物园': [4, 4.5], '人民广场': [6, 4.5], '市政府': [8, 4.5], '东门': [10, 4.5],
    '科技园': [12, 4.5], '机场': [14, 4.5], '北站': [6, 0], '体育馆': [6, 1.5], '文化宫': [6, 3], '南湖': [6, 6],
    '大学城': [6, 7.5], '南站': [6, 9], '植物园': [0.5, 6], '老街': [8, 3], '会展中心': [9.5, 6], '东湖': [11, 7.5],
    '西站': [3, 9], '花园路': [4.5, 7.5], '博物馆': [8, 6], '港口': [11, 9]
  };

  // 和 metro.c 相同的建图：边按读取的顺序追加到两个车站的邻接表末尾
  function build(lines) {
    var m = { names: [], index: {}, lines: [], adj: [], segs: [] };
    lines.forEach(function (L, li) {
      m.lines.push(L[0]);
      var prev = -1;
      for (var i = 1; i < L.length; i += 2) {
        var name = L[i], w = i > 1 ? L[i - 1] : 0;
        if (!(name in m.index)) { m.index[name] = m.names.length; m.names.push(name); m.adj.push([]); }
        var s = m.index[name];
        if (prev >= 0) {
          m.adj[prev].push({ to: s, line: li, w: w });
          m.adj[s].push({ to: prev, line: li, w: w });
          m.segs.push({ a: prev, b: s, line: li, w: w });
        }
        prev = s;
      }
    });
    return m;
  }
  var CITY = build(LINES);
  function segKey(a, b) { return a < b ? a + '-' + b : b + '-' + a; }
  function short(lineName) { return lineName.replace('号线', '').replace('线', ''); }

  /* ---------- 里程碑 2：BFS ---------- */

  function bfsView(m, x) {
    var nodes = CITY.names.map(function (name, s) {
      var g = GRID[name], d = (x.depth || {})[s];
      return { id: s, label: s, x: 40 + g[0] * 46, y: 26 + g[1] * 36, hl: (x.hl || {})[s], note: name + (d != null ? '·' + d : '') };
    });
    var edges = CITY.segs.map(function (e) { return { a: e.a, b: e.b, hl: (x.ehl || {})[segKey(e.a, e.b)] }; });
    var q = (x.queue || []).map(function (s) { return { v: CITY.names[s], hl: null }; });
    return {
      kind: 'multi',
      parts: [
        { kind: 'graph', nodes: nodes, edges: edges },
        { kind: 'array', x0: 20, cellW: 66, top: 452, noIndex: true, label: '队列 queue（左边是队头）', cells: q }
      ],
      vars: x.vars || {}
    };
  }

  function bfsRun(m, p, rec) {
    var from = CITY.index[String(p.from).trim()], to = CITY.index[String(p.to).trim()];
    var n = CITY.names.length, prev = [], via = [], depth = {}, queue = [from], head = 0, done = {};
    for (var i = 0; i < n; i++) prev.push(-1);
    prev[from] = from; depth[from] = 0;
    function hl(cur, extra) {
      var h = {};
      for (var s = 0; s < n; s++) if (prev[s] >= 0) h[s] = done[s] ? 'ok' : 'cmp';
      if (cur != null) h[cur] = 'cur';
      return Object.assign(h, extra || {});
    }
    function tree(extra) {
      var e = {};
      for (var s = 0; s < n; s++) if (prev[s] >= 0 && prev[s] !== s) e[segKey(s, prev[s])] = 'cur';
      return Object.assign(e, extra || {});
    }
    rec.step(3, '从 ' + CITY.names[from] + ' 出发。prev[' + from + '] 设为它自己，表示已经发现了；把它放进队列。车站下方的数字是它离起点几站。',
      { hl: hl(), ehl: tree(), queue: queue.slice(head), depth: depth, vars: { from: from, to: to } });
    while (head < queue.length && prev[to] < 0) {
      var u = queue[head++], found = [];
      rec.step(5, '取出队头 ' + CITY.names[u] + '（离起点 ' + depth[u] + ' 站），按邻接表的顺序看它的相邻车站。',
        { hl: hl(u), ehl: tree(), queue: queue.slice(head), depth: depth, vars: { u: u + ' ' + CITY.names[u] } });
      CITY.adj[u].forEach(function (e) {
        if (prev[e.to] >= 0) return;
        prev[e.to] = u; via[e.to] = e.line; depth[e.to] = depth[u] + 1;
        queue.push(e.to); found.push(e);
        var nh = {}; nh[e.to] = 'new';
        var ne = {}; ne[segKey(u, e.to)] = 'new';
        rec.step(11, '第一次发现 ' + CITY.names[e.to] + '：prev = ' + CITY.names[u] + '，via = ' + CITY.lines[e.line] + '，放进队尾。',
          { hl: hl(u, nh), ehl: tree(ne), queue: queue.slice(head), depth: depth, vars: { u: u + ' ' + CITY.names[u] } });
      });
      if (!found.length) {
        rec.step(8, CITY.names[u] + ' 的相邻车站都已经发现过了，什么也不做。',
          { hl: hl(u), ehl: tree(), queue: queue.slice(head), depth: depth, vars: { u: u + ' ' + CITY.names[u] } });
      }
      done[u] = true;
    }
    var path = [], ph = {}, pe = {}, lines = [];
    for (var v = to; v !== from; v = prev[v]) { path.unshift(v); lines.unshift(CITY.lines[via[v]]); pe[segKey(v, prev[v])] = 'ok'; }
    path.unshift(from);
    path.forEach(function (s) { ph[s] = 'ok'; });
    for (var s2 = 0; s2 < n; s2++) if (!(s2 in ph)) ph[s2] = 'dim';
    var transfers = 0;
    for (var k = 1; k < lines.length; k++) if (lines[k] !== lines[k - 1]) transfers++;
    rec.step(4, CITY.names[to] + ' 已经发现了，循环结束。从终点沿着 prev 往回走，就得到路线：' +
      path.map(function (s) { return CITY.names[s]; }).join(' → ') + '，坐 ' + (path.length - 1) + ' 站，换乘 ' + transfers + ' 次。',
      { hl: ph, ehl: pe, queue: [], depth: depth, vars: {} });
  }

  DSViz.register('metro-bfs', {
    title: 'BFS：一层一层地向外扩展',
    height: 500,
    code: [
      'for (int i = 0; i < n; ++i) prev[i] = -1;',
      'prev[from] = from;',
      'queue[tail++] = from;',
      'while (head < tail && prev[to] < 0) {',
      '    int u = queue[head++];',
      '    for (每一条边 e : u 的邻接表) {',
      '        if (prev[e->to] >= 0)',
      '            continue;           // 已经发现过',
      '        prev[e->to] = u;',
      '        via[e->to] = e->line;',
      '        queue[tail++] = e->to;',
      '    }',
      '}'
    ],
    init: function () { return {}; },
    view: bfsView,
    start: function (m, rec) { bfsRun(m, { from: '西山', to: '港口' }, rec); },
    actions: [{
      label: '查找',
      fields: [{ key: 'from', label: '起点', value: '西山', size: 6 }, { key: 'to', label: '终点', value: '港口', size: 6 }],
      check: function (m, p) {
        var a = String(p.from).trim(), b = String(p.to).trim();
        if (!(a in CITY.index)) return '找不到车站：' + a;
        if (!(b in CITY.index)) return '找不到车站：' + b;
        if (a === b) return '起点和终点是同一个车站';
        return null;
      },
      run: bfsRun
    }]
  });

  /* ---------- 里程碑 3、4：Dijkstra ---------- */

  // 一张小图：车站、线路、区间，以及每个状态（车站，线路）在画面上的位置
  function smallGraph(def) {
    var g = build(def.lines);
    g.pos = def.pos;                                  // 'station' 或 'station|line' → [x, y]
    g.states = [];                                   // { s, line }
    g.first = [];
    g.names.forEach(function (name, s) {
      g.first.push(g.states.length);
      g.adj[s].forEach(function (e, i) {
        if (i > 0 && g.adj[s][i - 1].line === e.line) return;
        g.states.push({ s: s, line: e.line });
      });
    });
    g.first.push(g.states.length);
    return g;
  }
  function stateOf(g, s, line) {
    for (var v = g.first[s]; v < g.first[s + 1]; v++) if (g.states[v].line === line) return v;
    return -1;
  }

  // 港口 → 老街附近的一小块：4号线 港口-东湖-东门，3号线 东湖-会展中心-市政府-老街，1号线 东门-市政府
  var TRAP = smallGraph({
    lines: [['4号线', '港口', 2, '东湖', 3, '东门'], ['3号线', '东湖', 4, '会展中心', 3, '市政府', 2, '老街'], ['1号线', '东门', 3, '市政府']],
    pos: {
      '港口': [640, 330], '东湖': [590, 245], '东门': [590, 65], '会展中心': [430, 245], '市政府': [330, 125], '老街': [150, 185],
      '港口|4号线': [640, 330], '东湖|4号线': [640, 245], '东湖|3号线': [540, 245], '东门|4号线': [640, 65], '东门|1号线': [540, 65],
      '会展中心|3号线': [430, 245], '市政府|3号线': [330, 185], '市政府|1号线': [330, 65], '老街|3号线': [150, 185]
    }
  });

  // 和 C 代码相同的二叉堆，less(a, b) 比较两个元素的代价
  function heapPush(h, it, less) {
    var i = h.length; h.push(it);
    while (i > 0 && less(it, h[(i - 1) >> 1])) { h[i] = h[(i - 1) >> 1]; i = (i - 1) >> 1; }
    h[i] = it;
  }
  function heapPop(h, less) {
    var top = h[0], last = h.pop();
    if (!h.length) return top;
    var i = 0;
    for (;;) {
      var c = 2 * i + 1;
      if (c >= h.length) break;
      if (c + 1 < h.length && less(h[c + 1], h[c])) c++;
      if (!less(h[c], last)) break;
      h[i] = h[c]; i = c;
    }
    h[i] = last;
    return top;
  }

  function costText(c, mode) { return c == null ? '∞' : mode === 'transfers' ? '(' + c.t + ',' + c.m + ')' : String(c.m); }
  function costLess(a, b, mode) {
    if (b == null) return true;
    if (mode === 'transfers' && a.t !== b.t) return a.t < b.t;
    return a.m < b.m;
  }

  // 画面：状态图（或车站图）+ 堆
  function dijkstraView(g, x) {
    var nodes = [], edges = [];
    if (x.mode === 'station') {
      g.names.forEach(function (name, s) {
        var p = g.pos[name], d = (x.dist || {})[s];
        nodes.push({ id: s, label: x.arrive && x.arrive[s] != null ? short(g.lines[x.arrive[s]]) : '', x: p[0], y: p[1],
          hl: (x.hl || {})[s], note: name + ' ' + (d == null ? '∞' : d) });
      });
      g.segs.forEach(function (e) {
        edges.push({ a: e.a, b: e.b, w: short(g.lines[e.line]) + '·' + e.w, hl: (x.ehl || {})[segKey(e.a, e.b)] });
      });
    } else {
      g.states.forEach(function (st, v) {
        var name = g.names[st.s], p = g.pos[name + '|' + g.lines[st.line]];
        nodes.push({ id: v, label: short(g.lines[st.line]), x: p[0], y: p[1], hl: (x.hl || {})[v],
          note: name + ' ' + costText((x.dist || {})[v], x.mode) });
      });
      g.segs.forEach(function (e) {
        var a = stateOf(g, e.a, e.line), b = stateOf(g, e.b, e.line);
        edges.push({ a: a, b: b, w: e.w, hl: (x.ehl || {})[segKey(a, b)] });
      });
      g.names.forEach(function (name, s) {
        for (var v = g.first[s]; v < g.first[s + 1]; v++) {
          for (var w = v + 1; w < g.first[s + 1]; w++) edges.push({ a: v, b: w, w: '换' + TRANSFER, hl: (x.ehl || {})[segKey(v, w)] });
        }
      });
    }
    var heap = (x.heap || []).slice().sort(function (a, b) { return costLess(a.c, b.c, x.mode) ? -1 : costLess(b.c, a.c, x.mode) ? 1 : 0; });
    var cells = heap.map(function (it) {
      var name = x.mode === 'station' ? g.names[it.v] : g.names[g.states[it.v].s] + short(g.lines[g.states[it.v].line]);
      return { v: costText(it.c, x.mode) + ' ' + name, hl: it.stale ? 'dim' : null };
    });
    return {
      kind: 'multi',
      parts: [
        { kind: 'graph', nodes: nodes, edges: edges },
        { kind: 'array', x0: 20, cellW: x.mode === 'transfers' ? 96 : 84, top: 410, noIndex: true,
          label: '堆中的元素，按代价从小到大画出；灰色的已经过时', cells: cells }
      ],
      vars: x.vars || {}
    };
  }

  // 按车站做 Dijkstra：每个车站只记一个 dist，以及到达时坐的线路 arrive。这是错误的做法
  function stationRun(g, from, to, rec) {
    var n = g.names.length, dist = {}, arrive = {}, done = {}, heap = [], prevS = {};
    var less = function (a, b) { return a.c.m < b.c.m; };
    function hl(cur) { var h = {}; for (var s = 0; s < n; s++) if (done[s]) h[s] = 'ok'; else if (s in dist) h[s] = 'cmp'; if (cur != null) h[cur] = 'cur'; return h; }
    function snap(line, msg, cur, extra) {
      var hp = heap.map(function (it) { return { v: it.v, c: it.c, stale: done[it.v] || it.c.m > dist[it.v] }; });
      rec.step(line, msg, Object.assign({ mode: 'station', dist: dist, arrive: arrive, hl: hl(cur), heap: hp }, extra || {}));
    }
    dist[from] = 0; arrive[from] = null;
    heapPush(heap, { v: from, c: { m: 0 } }, less);
    snap(1, '每个车站只记一个最短时间 dist，以及到达时坐的是哪条线（圆圈里的数字）。换乘要加 ' + TRANSFER + ' 分钟：只有边的线路和「到达时坐的线」不同时才加。');
    while (heap.length) {
      var it = heapPop(heap, less), u = it.v;
      if (done[u] || it.c.m > dist[u]) continue;
      done[u] = true;
      if (u === to) {
        var path = [];
        for (var v = to; v !== from; v = prevS[v]) path.unshift(g.names[v]);
        path.unshift(g.names[from]);
        var e = {}, ph = {};
        for (var s3 = 0; s3 < n; s3++) ph[s3] = 'dim';
        for (var v2 = to; v2 !== from; v2 = prevS[v2]) { e[segKey(v2, prevS[v2])] = 'ok'; ph[v2] = 'ok'; }
        ph[from] = 'ok';
        snap(3, '取出 ' + g.names[u] + '，dist = ' + dist[u] + '。结果是 ' + path.join(' → ') + '，' + dist[u] + ' 分钟。可是正确答案是 16 分钟：在市政府时，这个算法只留下了「坐 1号线 13 分钟到达」，扔掉了「坐 3号线 14 分钟到达」，而后者往老街走不用再换乘。',
          u, { ehl: e, hl: ph, vars: { 结果: dist[u] + ' 分钟（错误）' } });
        return;
      }
      snap(3, '取出 dist 最小的 ' + g.names[u] + '（' + dist[u] + ' 分钟，到达时坐 ' + (arrive[u] == null ? '—' : g.lines[arrive[u]]) + '），它的 dist 确定了。', u);
      g.adj[u].forEach(function (e) {
        var extra = arrive[u] != null && arrive[u] !== e.line ? TRANSFER : 0, nd = dist[u] + e.w + extra;
        if (done[e.to]) return;
        var eh = {}; eh[segKey(u, e.to)] = 'cur';
        if (!(e.to in dist) || nd < dist[e.to]) {
          var old = dist[e.to];
          dist[e.to] = nd; arrive[e.to] = e.line; prevS[e.to] = u;
          heapPush(heap, { v: e.to, c: { m: nd } }, less);
          snap(9, '坐 ' + g.lines[e.line] + ' 到 ' + g.names[e.to] + '：' + dist[u] + ' + ' + e.w + (extra ? ' + 换乘 ' + extra : '') + ' = ' + nd +
            (old == null ? '' : '，比原来的 ' + old + ' 小') + '，更新 dist，到达时坐 ' + g.lines[e.line] + '。', u, { ehl: eh });
        } else {
          snap(9, '坐 ' + g.lines[e.line] + ' 到 ' + g.names[e.to] + '：' + dist[u] + ' + ' + e.w + (extra ? ' + 换乘 ' + extra : '') + ' = ' + nd +
            '，不比原来的 ' + dist[e.to] + ' 小，丢掉。' + (g.names[e.to] === '市政府' ? '注意：被丢掉的这条是坐 3号线 到达的。' : ''), u, { ehl: eh });
        }
      });
    }
  }

  // 在状态图上做 Dijkstra，和 route.c 中的 dijkstra 相同。mode 是 'time' 或 'transfers'
  function stateRun(g, from, to, mode, rec) {
    var n = g.states.length, dist = {}, done = {}, prev = {}, heap = [];
    var less = function (a, b) { return costLess(a.c, b.c, mode); };
    function stName(v) { return g.names[g.states[v].s] + '（' + g.lines[g.states[v].line] + '）'; }
    function hl(cur) { var h = {}; for (var v = 0; v < n; v++) if (done[v]) h[v] = 'ok'; else if (v in dist) h[v] = 'cmp'; if (cur != null) h[cur] = 'cur'; return h; }
    function snap(line, msg, cur, extra) {
      var hp = heap.map(function (it) { return { v: it.v, c: it.c, stale: !!done[it.v] }; });
      rec.step(line, msg, Object.assign({ mode: mode, dist: dist, hl: hl(cur), heap: hp }, extra || {}));
    }
    for (var v0 = g.first[from]; v0 < g.first[from + 1]; v0++) {
      dist[v0] = { t: 0, m: 0 };
      heapPush(heap, { v: v0, c: dist[v0] }, less);
    }
    snap(2, '每个圆圈是一个状态：（车站，线路），圆圈里是线路。起点 ' + g.names[from] + ' 的' + (g.first[from + 1] - g.first[from] > 1 ? '每一个状态' : '状态') +
      '代价都是 ' + costText({ t: 0, m: 0 }, mode) + '：刚上车不算换乘。' + (mode === 'transfers' ? '代价是（换乘次数，用时），先比较换乘次数。' : ''));
    while (heap.length) {
      var it = heapPop(heap, less), v = it.v;
      if (done[v]) { snap(5, '取出 ' + stName(v) + '，但它已经确定过了，这是一个过时的元素，跳过。'); continue; }
      done[v] = true;
      var s = g.states[v].s, c = dist[v];
      if (s === to) {
        var e = {}, hops = [], ph = {};
        for (var k = 0; k < n; k++) ph[k] = 'dim';
        for (var w = v; prev[w] != null; w = prev[w]) {
          e[segKey(w, prev[w])] = 'ok';
          ph[w] = 'ok';
          hops.unshift(stName(w));
        }
        ph[w] = 'ok';
        hops.unshift(stName(w));
        snap(7, '取出 ' + stName(v) + '，代价 ' + costText(c, mode) + '。它在终点 ' + g.names[to] + '，停下。沿着 prev 往回走：' + hops.join(' → ') + '。',
          v, { ehl: e, hl: ph, vars: { 结果: costText(c, mode) } });
        return;
      }
      snap(6, '取出代价最小的 ' + stName(v) + '，代价 ' + costText(c, mode) + '，确定了。', v);
      g.adj[s].forEach(function (e) {
        if (e.line !== g.states[v].line) return;
        var w = stateOf(g, e.to, e.line), nc = { t: c.t, m: c.m + e.w };
        if (done[w]) return;
        var eh = {}; eh[segKey(v, w)] = 'cur';
        if (costLess(nc, dist[w], mode)) {
          dist[w] = nc; prev[w] = v; heapPush(heap, { v: w, c: nc }, less);
          snap(10, '坐一站到 ' + stName(w) + '：代价 ' + costText(nc, mode) + '，比原来小，更新并入堆。', v, { ehl: eh });
        } else {
          snap(10, '坐一站到 ' + stName(w) + '：代价 ' + costText(nc, mode) + '，不比原来的 ' + costText(dist[w], mode) + ' 小。', v, { ehl: eh });
        }
      });
      for (var w2 = g.first[s]; w2 < g.first[s + 1]; w2++) {
        if (w2 === v || done[w2]) continue;
        var nc2 = { t: c.t + 1, m: c.m + TRANSFER }, eh2 = {};
        eh2[segKey(v, w2)] = 'cur';
        if (costLess(nc2, dist[w2], mode)) {
          dist[w2] = nc2; prev[w2] = v; heapPush(heap, { v: w2, c: nc2 }, less);
          snap(13, '在 ' + g.names[s] + ' 换乘到 ' + stName(w2) + '：代价 ' + costText(nc2, mode) + '，比原来小，更新并入堆。', v, { ehl: eh2 });
        } else {
          snap(13, '在 ' + g.names[s] + ' 换乘到 ' + stName(w2) + '：代价 ' + costText(nc2, mode) + '，不比原来的 ' + costText(dist[w2], mode) + ' 小。', v, { ehl: eh2 });
        }
      }
    }
  }

  var STATE_CODE = [
    'for (起点 from 的每个状态 v)',
    '    dist[v] = 0, 入堆;',
    'while (堆不空) {',
    '    取出代价最小的 v;',
    '    if (done[v]) continue;      // 过时的元素',
    '    done[v] = 1;',
    '    if (v 在终点) break;',
    '    for (沿着 v 的线路的每条边 e) {   // 坐一站',
    '        w = (e->to, 同一条线路);',
    '        用 dist[v] + e->minutes 更新 dist[w];',
    '    }',
    '    for (同一个车站的其他状态 w)      // 换乘',
    '        用 dist[v] + 换乘时间 更新 dist[w];',
    '}'
  ];

  DSViz.register('metro-trap', {
    title: '按车站做 Dijkstra：港口 → 老街',
    height: 460,
    code: [
      'dist[from] = 0, 入堆;',
      'while (堆不空) {',
      '    取出 dist 最小的 u;',
      '    if (done[u]) continue;',
      '    done[u] = 1;',
      '    for (u 的每条边 e) {',
      '        int t = e->minutes;',
      '        if (到达 u 时坐的线 != e->line) t += 换乘时间;',
      '        用 dist[u] + t 更新 dist[e->to]，记下到达时坐的线;',
      '    }',
      '}'
    ],
    init: function () { return {}; },
    view: function (m, x) { return dijkstraView(TRAP, x); },
    start: function (m, rec) { stationRun(TRAP, TRAP.index['港口'], TRAP.index['老街'], rec); },
    actions: [{ label: '重新开始', run: function (m, p, rec) { stationRun(TRAP, TRAP.index['港口'], TRAP.index['老街'], rec); } }]
  });

  DSViz.register('metro-states', {
    title: '在状态图上做 Dijkstra：港口 → 老街',
    height: 460,
    code: STATE_CODE,
    init: function () { return {}; },
    view: function (m, x) { return dijkstraView(TRAP, x); },
    start: function (m, rec) { stateRun(TRAP, TRAP.index['港口'], TRAP.index['老街'], 'time', rec); },
    actions: [{ label: '重新开始', run: function (m, p, rec) { stateRun(TRAP, TRAP.index['港口'], TRAP.index['老街'], 'time', rec); } }]
  });

  // tests/m3/data/shared.txt：乙到丙之间，A线 开 10 分钟，B线 只要 3 分钟
  var SHARED = smallGraph({
    lines: [['A线', '甲', 2, '乙', 10, '丙'], ['B线', '乙', 3, '丙', 2, '丁']],
    pos: {
      '甲|A线': [90, 120], '乙|A线': [270, 120], '丙|A线': [530, 120],
      '乙|B线': [270, 290], '丙|B线': [530, 290], '丁|B线': [670, 290]
    }
  });

  DSViz.register('metro-pair', {
    title: '同一张状态图，两种代价：甲 → 丙',
    height: 460,
    code: STATE_CODE,
    init: function () { return {}; },
    view: function (m, x) { return dijkstraView(SHARED, x); },
    start: function (m, rec) { stateRun(SHARED, SHARED.index['甲'], SHARED.index['丙'], 'transfers', rec); },
    actions: [
      { label: '换乘最少', run: function (m, p, rec) { stateRun(SHARED, SHARED.index['甲'], SHARED.index['丙'], 'transfers', rec); } },
      { label: '时间最短', run: function (m, p, rec) { stateRun(SHARED, SHARED.index['甲'], SHARED.index['丙'], 'time', rec); } }
    ]
  });

  /* ---------- 里程碑 5：前缀搜索 ---------- */

  // JavaScript 按 UTF-16 比较字符串；这些站名都在基本多文种平面内，结果和 C 中按 UTF-8 字节的 strcmp 相同
  var SORTED = CITY.names.slice().sort();
  var PER_ROW = 11;

  function prefixView(m, x) {
    var boxes = [], texts = [];
    SORTED.forEach(function (name, i) {
      var r = Math.floor(i / PER_ROW), c = i % PER_ROW;
      boxes.push({ id: i, x: 20 + c * 64, y: 46 + r * 96, w: 60, h: 34, text: name, sub: i, hl: (x.hl || {})[i] });
    });
    ['lo', 'hi', 'mid'].forEach(function (k, j) {
      var at = (x.ptr || {})[k];
      if (at == null) return;
      var last = at === SORTED.length;                 // hi 可能指向最后一个元素的后面
      var r = Math.floor((last ? at - 1 : at) / PER_ROW), c = last ? (at - 1) % PER_ROW + 1 : at % PER_ROW;
      texts.push({ id: 'p' + k, x: 50 + c * 64, y: 40 + r * 96 - j * 13 - 4, text: k + '↓', anchor: 'middle', cls: k === 'mid' ? null : 'muted' });
    });
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function prefixRun(m, p, rec) {
    var prefix = String(p.prefix == null ? '' : p.prefix).trim(), len = prefix.length, n = SORTED.length;
    function cmp(name) { var a = name.slice(0, len); return a < prefix ? -1 : a > prefix ? 1 : 0; }   // strncmp(name, prefix, len)
    function base(lo, hi, extra) {
      var h = {};
      for (var i = 0; i < n; i++) if (i < lo || i >= hi) h[i] = 'dim';
      return Object.assign(h, extra || {});
    }
    rec.step(1, 'order 中的车站已经按站名排好序（框下面的数字是下标）。以「' + prefix + '」开头的站名一定挨在一起。先找第一个「前 ' + len + ' 个字」不小于「' + prefix + '」的位置。',
      { hl: {}, ptr: { lo: 0, hi: n }, vars: { prefix: '"' + prefix + '"', lo: 0, hi: n } });
    var lo = 0, hi = n;
    while (lo < hi) {
      var mid = lo + ((hi - lo) >> 1), c = cmp(SORTED[mid]), x = {};
      x[mid] = 'cur';
      var go = c < 0 ? '小于' : '不小于';
      rec.step(4, 'mid = ' + mid + '：「' + SORTED[mid] + '」的前 ' + len + ' 个字是「' + SORTED[mid].slice(0, len) + '」，' + go + '「' + prefix + '」，' +
        (c < 0 ? '答案在右边，lo = mid + 1。' : '答案在 mid 或左边，hi = mid。'),
        { hl: base(lo, hi, x), ptr: { lo: lo, hi: hi, mid: mid }, vars: { lo: lo, hi: hi, mid: mid } });
      if (c < 0) lo = mid + 1; else hi = mid;
    }
    var first = lo;
    rec.step(7, 'lo == hi == ' + first + '，*first = ' + first + '。再找第一个前 ' + len + ' 个字大于「' + prefix + '」的位置，只要在 [' + first + ', ' + n + ') 中找。',
      { hl: base(first, n), ptr: { lo: first, hi: n }, vars: { first: first } });
    hi = n;
    while (lo < hi) {
      var mid2 = lo + ((hi - lo) >> 1), c2 = cmp(SORTED[mid2]), x2 = {};
      x2[mid2] = 'cur';
      rec.step(11, 'mid = ' + mid2 + '：「' + SORTED[mid2] + '」的前 ' + len + ' 个字是「' + SORTED[mid2].slice(0, len) + '」，' + (c2 <= 0 ? '不大于' : '大于') + '「' + prefix + '」，' +
        (c2 <= 0 ? 'lo = mid + 1。' : 'hi = mid。'),
        { hl: base(lo, hi, x2), ptr: { lo: lo, hi: hi, mid: mid2 }, vars: { first: first, lo: lo, hi: hi, mid: mid2 } });
      if (c2 <= 0) lo = mid2 + 1; else hi = mid2;
    }
    var h = {};
    for (var i = 0; i < n; i++) h[i] = i >= first && i < lo ? 'ok' : 'dim';
    rec.step(14, lo === first ? '两个位置相同，没有以「' + prefix + '」开头的车站。' :
      '[' + first + ', ' + lo + ') 就是所有以「' + prefix + '」开头的车站，一共 ' + (lo - first) + ' 个：' + SORTED.slice(first, lo).join('、') + '。两次二分查找，各比较 log₂' + n + ' ≈ 5 次。',
      { hl: h, ptr: {}, vars: { first: first, 返回: lo - first } });
  }

  DSViz.register('metro-prefix', {
    title: '前缀搜索：两次二分查找',
    height: 215,
    width: 780,
    code: [
      'int lo = 0, hi = ix->n;',
      'while (lo < hi) {             // 第一个 >= prefix 的位置',
      '    int mid = lo + (hi - lo) / 2;',
      '    if (strncmp(名字[mid], prefix, len) < 0) lo = mid + 1;',
      '    else hi = mid;',
      '}',
      '*first = lo;',
      'hi = ix->n;',
      'while (lo < hi) {             // 第一个 > prefix 的位置',
      '    int mid = lo + (hi - lo) / 2;',
      '    if (strncmp(名字[mid], prefix, len) <= 0) lo = mid + 1;',
      '    else hi = mid;',
      '}',
      'return lo - *first;'
    ],
    init: function () { return {}; },
    view: prefixView,
    start: function (m, rec) { prefixRun(m, { prefix: '东' }, rec); },
    actions: [{
      label: '搜索',
      fields: [{ key: 'prefix', label: '站名的开头', value: '东', size: 6 }],
      check: function (m, p) { return String(p.prefix).trim() ? null : '请输入站名的开头'; },
      run: prefixRun
    }]
  });
})();
