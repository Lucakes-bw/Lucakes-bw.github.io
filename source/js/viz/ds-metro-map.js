/* 实战项目《地铁换乘查询》：示例城的线路图
 *
 * 文章中写 <div class="dsviz metro-map" data-metro="map"></div>
 * （有 dsviz 的外观，但没有 data-viz，不会被 dsviz.js 当成逐步演示）
 *
 * - 地铁图 / 抽象成图：同一份数据的两种画法，后者显示车站编号和边的权重
 * - 点车站：显示它所在的线路和相邻车站（和程序的 station 命令输出相同）
 * - 点线路：显示整条线路（和 line 命令输出相同）
 * - 示例路线：西山到港口的几种走法
 */
(function () {
  'use strict';
  var NS = 'http://www.w3.org/2000/svg';
  var U = 52, PAD_X = 52, PAD_Y = 40;               // 网格单位和边距（viewBox 坐标）

  // 和 data/metro.txt 相同的数据；items 中的字符串是车站，数组是线路的拐点
  var LINES = [
    { name: '1号线', color: '#e03131',
      items: ['西山', 3, '果园', 2, '动物园', 3, '人民广场', 2, '市政府', 3, '东门', 2, '科技园', 4, '机场'] },
    { name: '2号线', color: '#2f9e44',
      items: ['北站', 3, '体育馆', 2, '文化宫', 3, '人民广场', 2, '南湖', 3, '大学城', 4, '南站'] },
    { name: '3号线', color: '#1c7ed6',
      items: ['植物园', 3, '果园', 4, [5, 1.5], '体育馆', 3, [8, 1.5], '老街', 2, '市政府', 3, '会展中心', 4, '东湖'] },
    { name: '4号线', color: '#f08c00',
      items: ['西站', 4, '花园路', 3, '南湖', 2, '博物馆', 3, [8.5, 6], '东门', 3, [11, 5.5], '东湖', 2, '港口'] }
  ];
  // 车站的位置（网格坐标）和站名的位置 [dx, dy, 对齐]
  var POS = {
    '西山': [0, 4.5, 0, 22, 'middle'], '果园': [2, 4.5, -10, -12, 'end'], '动物园': [4, 4.5, 0, 22, 'middle'],
    '人民广场': [6, 4.5, -12, -12, 'end'], '市政府': [8, 4.5, -12, 22, 'end'], '东门': [10, 4.5, 0, -14, 'middle'],
    '科技园': [12, 4.5, 0, 22, 'middle'], '机场': [14, 4.5, 0, 22, 'middle'],
    '北站': [6, 0, 14, 5, 'start'], '体育馆': [6, 1.5, -10, -12, 'end'], '文化宫': [6, 3, -14, 5, 'end'],
    '南湖': [6, 6, 12, -10, 'start'], '大学城': [6, 7.5, 14, 5, 'start'], '南站': [6, 9, 14, 5, 'start'],
    '植物园': [0.5, 6, 0, 22, 'middle'], '老街': [8, 3, 14, 5, 'start'], '会展中心': [9.5, 6, 14, 5, 'start'],
    '东湖': [11, 7.5, -12, 18, 'end'], '西站': [3, 9, 0, 22, 'middle'], '花园路': [4.5, 7.5, -14, 5, 'end'],
    '博物馆': [8, 6, 0, 22, 'middle'], '港口': [11, 9, 14, 5, 'start']
  };
  // 西山到港口的几种走法：[车站, 坐的线路, 车站, 坐的线路, ...]
  var ROUTES = [
    { name: 'A', path: ['西山', '1号线', '东门', '4号线', '港口'] },
    { name: 'B', path: ['西山', '1号线', '市政府', '3号线', '东湖', '4号线', '港口'] },
    { name: 'C', path: ['西山', '1号线', '果园', '3号线', '东湖', '4号线', '港口'] },
    { name: 'D', path: ['西山', '1号线', '人民广场', '2号线', '南湖', '4号线', '港口'] }
  ];

  /* ---------- 和 C 程序相同的建图：车站按第一次出现的顺序编号，边按读取的顺序追加 ---------- */
  var stations = [], index = {}, lines = [], segments = [];
  LINES.forEach(function (L, li) {
    var line = { name: L.name, color: L.color, stations: [], minutes: [], shapes: [] };
    var prev = null, pending = 0, bends = [];
    L.items.forEach(function (it) {
      if (typeof it === 'number') { pending = it; return; }
      if (Array.isArray(it)) { bends.push(it); return; }
      if (!(it in index)) { index[it] = stations.length; stations.push({ id: stations.length, name: it, edges: [] }); }
      var s = index[it];
      line.stations.push(s);
      line.minutes.push(prev == null ? 0 : pending);
      if (prev != null) {
        var seg = { a: prev, b: s, line: li, minutes: pending, bends: bends };
        segments.push(seg);
        stations[prev].edges.push({ to: s, line: li, minutes: pending, seg: seg });
        stations[s].edges.push({ to: prev, line: li, minutes: pending, seg: seg });
      }
      prev = s; bends = [];
    });
    lines.push(line);
  });
  function linesOf(s) {
    var out = [];
    stations[s].edges.forEach(function (e) { if (out[out.length - 1] !== e.line) out.push(e.line); });
    return out;
  }
  function xy(p) { return [PAD_X + p[0] * U, PAD_Y + p[1] * U]; }
  function spos(s) { return xy(POS[stations[s].name]); }
  function segPoints(seg, straight) {
    var pts = [spos(seg.a)];
    if (!straight) seg.bends.forEach(function (b) { pts.push(xy(b)); });
    pts.push(spos(seg.b));
    return pts;
  }
  function midpoint(pts) {                          // 折线长度一半处的点
    var total = 0, i, d = [];
    for (i = 1; i < pts.length; i++) { d.push(Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1])); total += d[i - 1]; }
    var half = total / 2;
    for (i = 1; i < pts.length; i++) {
      if (half <= d[i - 1]) {
        var t = half / d[i - 1];
        return [pts[i - 1][0] + (pts[i][0] - pts[i - 1][0]) * t, pts[i - 1][1] + (pts[i][1] - pts[i - 1][1]) * t];
      }
      half -= d[i - 1];
    }
    return pts[0];
  }
  // 线路 li 上从车站 a 到 b 经过的区间
  function ride(li, a, b) {
    var st = lines[li].stations, i = st.indexOf(a), j = st.indexOf(b), out = [];
    var step = i < j ? 1 : -1;
    for (var k = i; k !== j; k += step) {
      var x = st[k], y = st[k + step];
      segments.forEach(function (sg) { if (sg.line === li && ((sg.a === x && sg.b === y) || (sg.a === y && sg.b === x))) out.push(sg); });
    }
    return out;
  }
  function lineIndex(name) { for (var i = 0; i < lines.length; i++) if (lines[i].name === name) return i; return -1; }

  /* ---------- 样式：沿用 dsviz 的外框和按钮，这里只补充线路图自己的部分 ---------- */
  function injectStyle() {
    if (document.getElementById('metro-map-style')) return;
    var css = [
      '.metro-map .mm-stage{padding:6px 4px;overflow-x:auto}',
      '.metro-map svg{display:block;width:100%;min-width:620px;height:auto}',
      '.metro-map svg text{font:13px var(--mono);fill:var(--fg)}',
      '.metro-map .mm-seg path{fill:none;stroke-linecap:round;stroke-linejoin:round;transition:opacity .25s,stroke-width .25s}',
      '.metro-map .mm-st circle{fill:var(--panel);stroke-width:2.5;transition:opacity .25s}',
      '.metro-map .mm-st.is-transfer circle{stroke:var(--fg);stroke-width:2.5}',
      '.metro-map .mm-st{cursor:pointer;outline:none}',
      '.metro-map .mm-st:focus-visible circle,.metro-map .mm-st.is-sel circle{stroke:var(--accent);stroke-width:4}',
      '.metro-map .mm-st text{paint-order:stroke;stroke:var(--panel);stroke-width:4px;stroke-linejoin:round}',
      '.metro-map .mm-st .mm-id{font-size:12px;font-weight:700;stroke:none}',
      '.metro-map .mm-w{font-size:11px!important;fill:var(--muted)!important;paint-order:stroke;stroke:var(--panel);stroke-width:3px}',
      '.metro-map.is-graph .mm-seg path{stroke:var(--border-2)!important}',
      '.metro-map.is-graph .mm-seg.is-on path{stroke:var(--accent)!important}',
      '.metro-map.is-graph .mm-st circle{stroke:var(--fg-2)}',
      '.metro-map.is-graph .mm-st .mm-name{font-size:11px;fill:var(--muted)}',
      '.metro-map .is-dim{opacity:.22}',
      '.metro-map .mm-seg.is-on path{stroke-width:9}',
      '.metro-map .mm-legend{display:flex;flex-wrap:wrap;gap:6px;align-items:center}',
      '.metro-map .mm-sw{display:inline-block;width:14px;height:6px;border-radius:3px;margin-right:6px;vertical-align:middle}',
      '.metro-map .dv-btn[aria-pressed=true]{border-color:var(--accent);color:var(--accent);background:var(--accent-soft)}',
      '.metro-map .mm-check{display:inline-flex;align-items:center;gap:4px;color:var(--muted);cursor:pointer}',
      '.metro-map .mm-out{margin:0;padding:8px 12px;border-top:1px solid var(--border);background:var(--code-bg);',
      'font:12.5px/1.7 var(--mono);color:var(--fg);white-space:pre-wrap;min-height:5.2em}',
      '.metro-map .mm-out .mm-prompt{color:var(--accent)}',
      '.metro-map .mm-out .mm-note{color:var(--muted)}',
      '.metro-map .mm-label{color:var(--muted);margin-right:2px}'
    ].join('\n');
    var st = document.createElement('style');
    st.id = 'metro-map-style';
    st.textContent = css;
    document.head.appendChild(st);
  }

  function el(tag, attrs, parent) {
    var e = document.createElementNS(NS, tag);
    for (var k in attrs) if (attrs[k] != null) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  function h(tag, cls, parent, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    if (parent) parent.appendChild(e);
    return e;
  }
  function btn(parent, text, title) {
    var b = h('button', 'dv-btn', parent, text);
    b.type = 'button';
    if (title) b.title = title;
    return b;
  }

  function mount(host) {
    host.innerHTML = '';
    host.classList.add('dsviz');
    var state = { graph: false, minutes: false, sel: null };   // sel：{type:'station'|'line'|'route', id}

    var head = h('div', 'dv-head', host);
    h('span', 'dv-title', head, '示例城地铁线路图');
    h('span', 'dv-count', head, '4 条线路 · 22 个车站');

    // 第一排：画法和区间时间
    var bar1 = h('div', 'dv-bar', host);
    bar1.style.borderTop = 'none';
    h('span', 'mm-label', bar1, '画法');
    var bMap = btn(bar1, '地铁图'), bGraph = btn(bar1, '抽象成图', '车站变成顶点（显示编号），区间变成带权重的边');
    var chk = h('label', 'mm-check', bar1);
    var box = h('input', null, chk);
    box.type = 'checkbox';
    chk.appendChild(document.createTextNode('显示区间时间'));

    var stage = h('div', 'mm-stage', host);
    var W = PAD_X * 2 + 14 * U + 40, H = PAD_Y * 2 + 9 * U + 10;
    var svg = el('svg', { viewBox: '0 0 ' + W + ' ' + H, role: 'img', 'aria-label': '示例城地铁线路图' }, stage);
    var gSeg = el('g', {}, svg), gW = el('g', {}, svg), gSt = el('g', {}, svg);

    // 第二排：线路
    var bar2 = h('div', 'dv-bar mm-legend', host);
    h('span', 'mm-label', bar2, '线路');
    var lineBtns = lines.map(function (L, li) {
      var b = btn(bar2, '');
      var sw = h('span', 'mm-sw', b); sw.style.background = L.color;
      b.appendChild(document.createTextNode(L.name));
      b.addEventListener('click', function () { select({ type: 'line', id: li }); });
      return b;
    });
    // 第三排：示例路线
    var bar3 = h('div', 'dv-bar', host);
    h('span', 'mm-label', bar3, '西山 → 港口');
    var routeBtns = ROUTES.map(function (r, ri) {
      var b = btn(bar3, '走法 ' + r.name);
      b.addEventListener('click', function () { select({ type: 'route', id: ri }); });
      return b;
    });
    var bClear = btn(bar3, '清除');
    bClear.addEventListener('click', function () { select(null); });

    var out = h('pre', 'mm-out', host);
    out.setAttribute('aria-live', 'polite');

    // 区间
    var segEls = segments.map(function (sg) {
      var g = el('g', { 'class': 'mm-seg' }, gSeg);
      var p = el('path', { stroke: lines[sg.line].color, 'stroke-width': 6 }, g);
      var t = el('text', { 'class': 'mm-w', 'text-anchor': 'middle' }, gW);
      return { g: g, p: p, t: t, sg: sg };
    });
    // 车站
    var stEls = stations.map(function (s) {
      var p = POS[s.name], c = spos(s.id), ls = linesOf(s.id);
      var g = el('g', { 'class': 'mm-st' + (ls.length > 1 ? ' is-transfer' : ''), tabindex: 0, role: 'button',
        'aria-label': s.name + '，' + ls.map(function (l) { return lines[l].name; }).join('、') }, gSt);
      var circle = el('circle', { cx: c[0], cy: c[1], r: 6, stroke: lines[ls[0]].color }, g);
      var name = el('text', { 'class': 'mm-name', 'text-anchor': p[4] }, g);
      name.textContent = s.name;
      var id = el('text', { 'class': 'mm-id', x: c[0], y: c[1] + 4, 'text-anchor': 'middle' }, g);
      id.textContent = s.id;
      g.addEventListener('click', function () { select({ type: 'station', id: s.id }); });
      g.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); select({ type: 'station', id: s.id }); }
      });
      return { g: g, circle: circle, name: name, id: id, transfer: ls.length > 1, c: c, p: p };
    });

    function draw() {
      host.classList.toggle('is-graph', state.graph);
      bMap.setAttribute('aria-pressed', !state.graph);
      bGraph.setAttribute('aria-pressed', state.graph);
      var showW = state.minutes || state.graph;
      box.checked = showW;
      box.disabled = state.graph;
      segEls.forEach(function (e) {
        var pts = segPoints(e.sg, state.graph);
        e.p.setAttribute('d', 'M' + pts.map(function (q) { return q[0] + ',' + q[1]; }).join('L'));
        e.p.setAttribute('stroke-width', state.graph ? 2.5 : 6);
        var m = midpoint(pts);
        e.t.setAttribute('x', m[0]); e.t.setAttribute('y', m[1] - 6);
        e.t.textContent = e.sg.minutes;
        e.t.style.display = showW ? '' : 'none';
      });
      stEls.forEach(function (e) {
        e.circle.setAttribute('r', state.graph ? 12 : (e.transfer ? 8 : 5.5));
        var push = state.graph ? 7 : 0;             // 抽象成图时圆圈变大，站名往外移一点
        e.name.setAttribute('x', e.c[0] + e.p[2] + Math.sign(e.p[2]) * push);
        e.name.setAttribute('y', e.c[1] + e.p[3] + Math.sign(e.p[3]) * push);
        e.id.style.display = state.graph ? '' : 'none';
      });
      highlight();
    }

    // 根据选中的对象，决定哪些区间和车站高亮，并输出文字
    function highlight() {
      var onSeg = null, onSt = null, text = [];
      var sel = state.sel;
      if (sel && sel.type === 'station') {
        var s = sel.id, ls = linesOf(s);
        onSeg = {}; onSt = {}; onSt[s] = true;
        stations[s].edges.forEach(function (e) { onSeg[segments.indexOf(e.seg)] = true; onSt[e.to] = true; });
        text.push(['p', 'metro> station ' + stations[s].name]);
        text.push(['', stations[s].name + '：' + ls.map(function (l) { return lines[l].name; }).join('、') + (ls.length > 1 ? '（换乘站）' : '')]);
        ls.forEach(function (l) {
          var nb = stations[s].edges.filter(function (e) { return e.line === l; })
            .map(function (e) { return stations[e.to].name + ' ' + e.minutes + ' 分钟'; });
          text.push(['', '  ' + lines[l].name + '：' + nb.join('、')]);
        });
        if (state.graph) text.push(['n', '# 顶点 ' + s + ' 的邻接表：' + stations[s].edges.map(function (e) {
          return '(' + e.to + ', ' + lines[e.line].name + ', ' + e.minutes + ')';
        }).join(' → ')]);
      } else if (sel && sel.type === 'line') {
        var L = lines[sel.id], total = 0;
        onSeg = {}; onSt = {};
        segments.forEach(function (sg, i) { if (sg.line === sel.id) onSeg[i] = true; });
        L.stations.forEach(function (x) { onSt[x] = true; });
        L.minutes.forEach(function (m) { total += m; });
        text.push(['p', 'metro> line ' + L.name]);
        text.push(['', L.name + '：共 ' + L.stations.length + ' 站，全程 ' + total + ' 分钟']);
        text.push(['', L.stations.map(function (x, i) { return (i ? ' -' + L.minutes[i] + '- ' : '') + stations[x].name; }).join('')]);
      } else if (sel && sel.type === 'route') {
        var r = ROUTES[sel.id].path, minutes = 0, nseg = 0, legs = [];
        onSeg = {}; onSt = {};
        for (var i = 1; i < r.length; i += 2) {
          var li = lineIndex(r[i]), a = index[r[i - 1]], b = index[r[i + 1]], m = 0;
          ride(li, a, b).forEach(function (sg) {
            onSeg[segments.indexOf(sg)] = true; onSt[sg.a] = true; onSt[sg.b] = true; m += sg.minutes; nseg++;
          });
          minutes += m;
          legs.push('  ' + r[i] + '  ' + r[i - 1] + ' → ' + r[i + 1] + '（' + ride(li, a, b).length + ' 站，' + m + ' 分钟）');
        }
        text.push(['', '走法 ' + ROUTES[sel.id].name + '：坐 ' + nseg + ' 站，用时 ' + minutes + ' 分钟，换乘 ' + (legs.length - 1) + ' 次']);
        legs.forEach(function (x) { text.push(['', x]); });
        text.push(['n', '# 用时只算了列车运行的时间，没有算换乘时走通道、等车的时间']);
      } else {
        text.push(['n', state.graph
          ? '# 每个车站是一个顶点，圆圈中是它的编号；每个区间是一条边，数字是权重（分钟）。点一个顶点看它的邻接表。'
          : '# 点一个车站、一条线路或一种走法试试。粗白圈是换乘站。']);
      }
      segEls.forEach(function (e, i) {
        e.g.classList.toggle('is-dim', !!onSeg && !onSeg[i]);
        e.g.classList.toggle('is-on', !!onSeg && !!onSeg[i] && sel.type !== 'line');
        e.t.classList.toggle('is-dim', !!onSeg && !onSeg[i]);
      });
      stEls.forEach(function (e, i) {
        e.g.classList.toggle('is-dim', !!onSt && !onSt[i]);
        e.g.classList.toggle('is-sel', !!sel && sel.type === 'station' && sel.id === i);
      });
      lineBtns.forEach(function (b, i) { b.setAttribute('aria-pressed', !!sel && sel.type === 'line' && sel.id === i); });
      routeBtns.forEach(function (b, i) { b.setAttribute('aria-pressed', !!sel && sel.type === 'route' && sel.id === i); });
      out.innerHTML = '';
      text.forEach(function (t) {
        var span = h('span', t[0] === 'p' ? 'mm-prompt' : t[0] === 'n' ? 'mm-note' : null, out, t[1]);
        out.appendChild(document.createTextNode('\n'));
        return span;
      });
    }

    function select(sel) {
      var same = sel && state.sel && sel.type === state.sel.type && sel.id === state.sel.id;
      state.sel = same ? null : sel;               // 再点一次取消
      highlight();
    }
    bMap.addEventListener('click', function () { state.graph = false; draw(); });
    bGraph.addEventListener('click', function () { state.graph = true; draw(); });
    box.addEventListener('change', function () { state.minutes = box.checked; draw(); });
    draw();
  }

  function mountAll() {
    injectStyle();
    var list = document.querySelectorAll('.metro-map[data-metro]');
    for (var i = 0; i < list.length; i++) mount(list[i]);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mountAll);
  else mountAll();
})();
