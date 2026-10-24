/**
 * dsviz —— 数据结构的逐步演示
 *
 * 文章中写 <div class="dsviz" data-viz="名字"></div>，
 * 再在 front-matter 的 viz 中列出定义了这个名字的脚本（source/js/viz/*.js）。
 *
 * 定义一个演示：
 *   DSViz.register('名字', {
 *     title: '标题',
 *     code: ['C 代码第 1 行', ...],          // 可选，右侧显示，当前行高亮
 *     height: 220,                            // 舞台高度（viewBox 单位）
 *     init() { return 模型; },                // 创建初始的数据
 *     view(模型, 额外) { return 场景; },      // 把模型画成场景
 *     actions: [{ label, fields: [{ key, label, value }], run(模型, 参数, rec) {} }],
 *   });
 *
 * run 中调用 rec.step(行号, 说明, 额外) 记录一步：引擎会调用 view(模型, 额外) 拍下当前的场景。
 * 场景 = { kind: 'list' | 'array' | 'tree' | 'graph' | 'bars' | 'free' | 'multi', ...，vars: { 变量名: 值 } }。
 */
(function () {
  'use strict';
  var NS = 'http://www.w3.org/2000/svg';
  var registry = {};
  var reduceMotion = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;

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
  function clone(o) { return JSON.parse(JSON.stringify(o)); }

  /* ---------------- 场景 → 图元 ----------------
     图元：box（矩形节点）、circ（圆形节点）、arrow（箭头）、line（无向边）、text、ptr（指针标签） */

  var R = {};

  // 链表：nodes [{id, val, col, row, hl, sub}]，links [{from, to, hl, back}]，ptrs [{name, at, hl}]
  // next 为 NULL 的节点（没有出边）在指针域中画一条斜线
  R.list = function (s) {
    var items = [], W = s.nodeW || 64, H = 36, GAP = s.gap || 96, X0 = s.x0 || 24, Y0 = s.top || 58, ROWH = s.rowH || 90;
    var pos = {}, hasOut = {};
    var FY = (s.links || []).some(function (l) { return l.back; }) ? 11 : H / 2;   // 双向链表：next 画在上半部分
    (s.links || []).forEach(function (l) { if (!l.back && l.to != null) hasOut[l.from] = true; });
    (s.nodes || []).forEach(function (n) {
      var x = X0 + n.col * GAP, y = Y0 + (n.row || 0) * ROWH;
      pos[n.id] = { x: x, y: y };
      items.push({ key: 'n' + n.id, type: 'box', x: x, y: y, w: W, h: H, text: n.val, sub: n.sub, hl: n.hl, split: s.split !== false, nul: s.split !== false && !hasOut[n.id] && !n.noNull, dbl: s.doubly });
    });
    (s.links || []).forEach(function (l) {
      var a = pos[l.from], b = pos[l.to];
      if (!a || !b) return;
      var key = 'l' + l.from + (l.back ? 'b' : '');
      if (l.back) {                                   // 双向链表的 prev 指针：从左侧下方指回前驱
        items.push({ key: key, type: 'arrow', x1: a.x + 4, y1: a.y + H - 7, x2: b.x + W - 2, y2: b.y + H - 7, hl: l.hl });
      } else if (a.y === b.y && b.x < a.x) {
        if (a.x - b.x <= GAP + 1) {                   // 反转后指向左边相邻的节点
          items.push({ key: key, type: 'arrow', x1: a.x + W - 9, y1: a.y + H, x2: b.x + (W - 18) / 2 + 6, y2: b.y + H + 2, curve: 26, hl: l.hl });
        } else {                                      // 指回前面较远的节点（环）：从上方绕回
          items.push({ key: key, type: 'arrow', x1: a.x + W - 9, y1: a.y + 4, x2: b.x + W / 2, y2: b.y - 1, curve: -60, hl: l.hl });
        }
      } else if (a.y === b.y) {
        items.push({ key: key, type: 'arrow', x1: a.x + W - 9, y1: a.y + FY, x2: b.x - 1, y2: b.y + FY, curve: l.curve || 0, hl: l.hl });
      } else {
        items.push({ key: key, type: 'arrow', x1: a.x + W - 9, y1: b.y > a.y ? a.y + H : a.y,
          x2: b.x + W / 2 - 8, y2: b.y > a.y ? b.y - 1 : b.y + H + 1, hl: l.hl });
      }
    });
    ptrs(items, s.ptrs, function (at) { var p = pos[at]; return p && { x: p.x + (W - 18) / 2, y: p.y }; });
    return items;
  };

  // 数组：cells [{v, hl}]，cap（容量，多出的格子画成虚线），ptrs [{name, at}]，vertical（栈）
  R.array = function (s) {
    var items = [], n = s.cells.length, cap = Math.max(s.cap || 0, n);
    var W = s.cellW || 48, H = 40, X0 = s.x0 || 24, Y0 = s.top || 52;
    for (var i = 0; i < cap; i++) {
      var c = s.cells[i];
      var x = s.vertical ? X0 : X0 + i * W, y = s.vertical ? Y0 + (cap - 1 - i) * H : Y0;
      items.push({
        key: 'c' + i, type: 'box', x: x, y: y, w: W, h: H, text: c ? c.v : '', hl: c ? c.hl : 'empty',
        idx: s.noIndex ? null : i, vertical: s.vertical
      });
    }
    ptrs(items, s.ptrs, function (at) {
      if (at == null || at < 0 || at > cap) return null;
      return s.vertical ? { x: X0 + W, y: Y0 + (cap - 1 - at) * H + H / 2, side: true } : { x: X0 + at * W + W / 2, y: Y0 };
    }, s.ptrsBelow ? Y0 + H + 28 : null);
    if (s.label) items.push({ key: 'label', type: 'text', x: X0, y: s.vertical ? Y0 - 10 : Y0 - 48, text: s.label, cls: 'muted', anchor: 'start' });
    return items;
  };

  // 柱状图（排序）：values [], hl {下标: 类型}, ptrs
  R.bars = function (s) {
    var items = [], n = s.values.length, W = Math.min(60, 640 / n), X0 = ((s.width || 720) - n * W) / 2, BASE = (s.height || 220) - 40;
    var max = Math.max.apply(null, s.values.map(function (v) { return v.v; }).concat([1]));
    s.values.forEach(function (c, i) {
      var bh = Math.max(6, (BASE - 30) * c.v / max);
      items.push({ key: 'b' + c.id, type: 'column', x: X0 + i * W + 3, y: BASE - bh, w: W - 6, h: bh, text: c.v, hl: c.hl });
    });
    ptrs(items, s.ptrs, function (at) { return { x: X0 + at * W + W / 2, y: BASE + 4, below: true }; });
    return items;
  };

  // 二叉树：nodes [{id, val, left, right, hl}]，root；按中序排列横坐标，按深度排列纵坐标
  R.tree = function (s) {
    var items = [], map = {}, xs = {}, ds = {}, order = 0, maxDepth = 0;
    (s.nodes || []).forEach(function (n) { map[n.id] = n; });
    (function walk(id, d) {
      if (id == null || !map[id]) return;
      walk(map[id].left, d + 1);
      xs[id] = order++; ds[id] = d; if (d > maxDepth) maxDepth = d;
      walk(map[id].right, d + 1);
    })(s.root, 0);
    var width = s.treeWidth || s.width || 640, gap = Math.min(s.maxGap || 56, (width - 60) / Math.max(order, 1));
    var X0 = (s.offsetX || 0) + (width - gap * (order - 1)) / 2, TOP = s.top || 44;
    var DY = Math.min(s.maxDy || 64, ((s.treeHeight || s.height || 260) - TOP - 26) / Math.max(maxDepth, 1));
    function P(id) { return { x: X0 + xs[id] * gap, y: TOP + ds[id] * DY }; }
    (s.nodes || []).forEach(function (n) {
      if (xs[n.id] == null) return;
      var p = P(n.id);
      ['left', 'right'].forEach(function (side) {
        var c = n[side];
        if (c != null && xs[c] != null) {
          var q = P(c);
          items.push({ key: 'e' + n.id + side, type: 'line', x1: p.x, y1: p.y, x2: q.x, y2: q.y, hl: (n.edgeHl && n.edgeHl[side]) || null, r: 17 });
        }
      });
    });
    (s.nodes || []).forEach(function (n) {
      if (xs[n.id] == null) return;
      var p = P(n.id);
      items.push({ key: 'n' + n.id, type: 'circ', x: p.x, y: p.y, r: 17, text: n.val, sub: n.sub, hl: n.hl });
    });
    ptrs(items, s.ptrs, function (at) { if (xs[at] == null) return null; var p = P(at); return { x: p.x, y: p.y - 17 }; });
    return items;
  };

  // 图：nodes [{id, label, x, y, hl, note}]，edges [{a, b, w, hl, directed}]
  R.graph = function (s) {
    var items = [], pos = {};
    (s.nodes || []).forEach(function (n) { pos[n.id] = n; });
    (s.edges || []).forEach(function (e) {
      var a = pos[e.a], b = pos[e.b];
      items.push({ key: 'e' + e.a + '-' + e.b, type: e.directed ? 'arrow' : 'line', x1: a.x, y1: a.y, x2: b.x, y2: b.y, r: 18, hl: e.hl, label: e.w, curve: e.curve || 0 });
    });
    (s.nodes || []).forEach(function (n) {
      items.push({ key: 'n' + n.id, type: 'circ', x: n.x, y: n.y, r: 18, text: n.label, sub: n.note, hl: n.hl });
    });
    return items;
  };

  // 自由布局：boxes [{id, x, y, w, h, text, sub, hl}]，arrows [{from, to, hl, label, curve}]，texts [{id, x, y, text, cls, anchor}]
  // 箭头从 from 的右边中点指向 to 的左边中点
  R.free = function (s) {
    var items = [], pos = {};
    (s.boxes || []).forEach(function (b) {
      pos[b.id] = b;
      items.push({ key: 'b' + b.id, type: 'box', x: b.x, y: b.y, w: b.w, h: b.h || 36, text: b.text, sub: b.sub, hl: b.hl });
    });
    (s.arrows || []).forEach(function (a) {
      var p = pos[a.from], q = pos[a.to];
      if (!p || !q) return;
      items.push({ key: 'a' + a.from + '>' + a.to, type: 'arrow', x1: p.x + p.w, y1: p.y + (p.h || 36) / 2,
        x2: q.x - 2, y2: q.y + (q.h || 36) / 2, hl: a.hl, label: a.label, curve: a.curve || 0 });
    });
    (s.texts || []).forEach(function (t, i) {
      items.push({ key: 't' + (t.id || i), type: 'text', x: t.x, y: t.y, text: t.text, cls: t.cls, anchor: t.anchor });
    });
    return items;
  };

  // 组合场景：parts 中的每个子场景各自带有 x0 / top 等位置参数
  R.multi = function (s) {
    var items = [];
    (s.parts || []).forEach(function (p, i) {
      R[p.kind](Object.assign({ height: s.height, width: s.width }, p)).forEach(function (it) {
        it.key = i + ':' + it.key;
        items.push(it);
      });
    });
    return items;
  };

  // 指针标签：在目标的上方画名字和一个向下的小箭头；同一个位置的多个指针叠放
  function ptrs(items, list, locate, belowY) {
    var stack = {};
    (list || []).forEach(function (p) {
      var at = locate(p.at);
      if (!at) return;
      var k = Math.round(at.x) + ',' + Math.round(at.y);
      var level = stack[k] = (stack[k] || 0) + 1;
      if (at.side) {
        items.push({ key: 'p' + p.name, type: 'ptr', x: at.x + 8 + (level - 1) * 52, y: at.y, text: p.name, hl: p.hl, dir: 'left' });
      } else if (at.below || belowY) {
        items.push({ key: 'p' + p.name, type: 'ptr', x: at.x, y: (belowY || at.y) + (level - 1) * 16, text: p.name, hl: p.hl, dir: 'up' });
      } else {
        items.push({ key: 'p' + p.name, type: 'ptr', x: at.x, y: at.y - 8 - (level - 1) * 16, text: p.name, hl: p.hl, dir: 'down' });
      }
    });
  }

  /* ---------------- 图元 → SVG（按 key 复用元素，产生过渡动画） ---------------- */

  function arrowPath(it) {
    var x1 = it.x1, y1 = it.y1, x2 = it.x2, y2 = it.y2;
    if (it.r) {                         // 从圆的边缘出发、到圆的边缘结束
      var dx = x2 - x1, dy = y2 - y1, d = Math.sqrt(dx * dx + dy * dy) || 1;
      x1 += dx / d * it.r; y1 += dy / d * it.r;
      x2 -= dx / d * (it.r + (it.type === 'arrow' ? 3 : 0)); y2 -= dy / d * (it.r + (it.type === 'arrow' ? 3 : 0));
    }
    if (!it.curve) return 'M' + x1 + ',' + y1 + ' L' + x2 + ',' + y2;
    var mx = (x1 + x2) / 2, my = (y1 + y2) / 2 + it.curve;
    if (it.type !== 'arrow' || it.r) {  // 图中的弯曲边：法线方向偏移
      var ddx = x2 - x1, ddy = y2 - y1, len = Math.sqrt(ddx * ddx + ddy * ddy) || 1;
      mx = (x1 + x2) / 2 - ddy / len * it.curve; my = (y1 + y2) / 2 + ddx / len * it.curve;
    }
    return 'M' + x1 + ',' + y1 + ' Q' + mx + ',' + my + ' ' + x2 + ',' + y2;
  }

  function Stage(svg, markerId) {
    this.svg = svg;
    this.layerE = el('g', {}, svg);   // 边在下
    this.layerN = el('g', {}, svg);   // 节点在上
    this.layerP = el('g', {}, svg);   // 指针最上
    this.map = {};
    this.markerId = markerId;
  }
  Stage.prototype.draw = function (items, animate) {
    var seen = {}, self = this;
    items.forEach(function (it) {
      seen[it.key] = true;
      var g = self.map[it.key];
      var fresh = !g || g._type !== it.type;
      if (fresh) {
        if (g) g.remove();
        g = self.create(it);
        self.map[it.key] = g;
        g._type = it.type;
        if (animate) { g.style.opacity = 0; g.getBoundingClientRect(); setTimeout(function () { g.style.opacity = ''; }, 16); }
      }
      self.update(g, it, animate && !fresh);
    });
    Object.keys(this.map).forEach(function (k) {
      if (!seen[k]) {
        var g = self.map[k];
        delete self.map[k];
        if (animate) { g.style.opacity = 0; setTimeout(function () { g.remove(); }, 300); } else g.remove();
      }
    });
  };
  Stage.prototype.create = function (it) {
    var t = it.type, layer = (t === 'arrow' || t === 'line') ? this.layerE : (t === 'ptr' ? this.layerP : this.layerN);
    var g = el('g', { 'class': 'dv-item' }, layer);
    if (t === 'box' || t === 'column') {
      g._rect = el('rect', { rx: t === 'column' ? 3 : 6 }, g);
      g._sep = el('line', {}, g);
      g._nul = el('line', { 'class': 'dv-nul' }, g);
      g._text = el('text', { 'text-anchor': 'middle' }, g);
      g._sub = el('text', { 'text-anchor': 'middle', 'class': 'dv-sub' }, g);
    } else if (t === 'circ') {
      g._circ = el('circle', {}, g);
      g._text = el('text', { 'text-anchor': 'middle' }, g);
      g._sub = el('text', { 'text-anchor': 'middle', 'class': 'dv-sub' }, g);
    } else if (t === 'arrow' || t === 'line') {
      g._path = el('path', { fill: 'none' }, g);
      if (t === 'arrow') g._path.setAttribute('marker-end', 'url(#' + this.markerId + ')');
      g._label = el('text', { 'text-anchor': 'middle', 'class': 'dv-w' }, g);
    } else if (t === 'text') {
      g._text = el('text', {}, g);
    } else if (t === 'ptr') {
      g._text = el('text', { 'text-anchor': 'middle' }, g);
      g._path = el('path', { fill: 'none' }, g);
    }
    return g;
  };
  Stage.prototype.update = function (g, it, animate) {
    g.setAttribute('class', 'dv-item dv-' + it.type + (it.hl ? ' is-' + it.hl : '') + (it.cls ? ' dv-' + it.cls : ''));
    g.style.transition = animate ? '' : 'none';
    var t = it.type;
    if (t === 'box' || t === 'column') {
      g.setAttribute('transform', 'translate(' + it.x + ',' + it.y + ')');
      g._rect.setAttribute('width', it.w); g._rect.setAttribute('height', it.h);
      if (t === 'box' && it.split) {   // 链表节点：右侧是 next 指针域
        g._sep.setAttribute('x1', it.w - 18); g._sep.setAttribute('x2', it.w - 18);
        g._sep.setAttribute('y1', 0); g._sep.setAttribute('y2', it.h);
        g._sep.style.display = '';
      } else g._sep.style.display = 'none';
      if (t === 'box' && it.nul) {
        g._nul.setAttribute('x1', it.w - 15); g._nul.setAttribute('y1', it.h - 5);
        g._nul.setAttribute('x2', it.w - 3); g._nul.setAttribute('y2', 5);
        g._nul.style.display = '';
      } else g._nul.style.display = 'none';
      var tx = (t === 'box' && it.split) ? (it.w - 18) / 2 : it.w / 2;
      g._text.setAttribute('x', tx);
      g._text.setAttribute('y', t === 'column' ? -6 : it.h / 2 + 5);
      g._text.textContent = it.text == null ? '' : it.text;
      g._sub.setAttribute('x', it.w / 2);
      g._sub.setAttribute('y', t === 'column' ? it.h + 14 : (it.vertical ? it.h / 2 + 4 : it.h + 15));
      if (it.vertical) { g._sub.setAttribute('x', -14); }
      g._sub.textContent = it.idx != null ? it.idx : (it.sub == null ? '' : it.sub);
    } else if (t === 'circ') {
      g.setAttribute('transform', 'translate(' + it.x + ',' + it.y + ')');
      g._circ.setAttribute('r', it.r);
      g._text.setAttribute('y', 5);
      g._text.textContent = it.text;
      g._sub.setAttribute('y', it.r + 15);
      g._sub.textContent = it.sub == null ? '' : it.sub;
    } else if (t === 'arrow' || t === 'line') {
      g._path.setAttribute('d', arrowPath(it));
      if (it.label != null) {
        var mx = (it.x1 + it.x2) / 2, my = (it.y1 + it.y2) / 2;
        if (it.curve) {
          var dx = it.x2 - it.x1, dy = it.y2 - it.y1, len = Math.sqrt(dx * dx + dy * dy) || 1;
          mx -= dy / len * it.curve / 2; my += dx / len * it.curve / 2;
        }
        g._label.setAttribute('x', mx); g._label.setAttribute('y', my - 4);
        g._label.textContent = it.label;
      } else g._label.textContent = '';
    } else if (t === 'text') {
      g._text.setAttribute('x', it.x); g._text.setAttribute('y', it.y);
      g._text.setAttribute('text-anchor', it.anchor || 'start');
      g._text.textContent = it.text;
    } else if (t === 'ptr') {
      g.setAttribute('transform', 'translate(' + it.x + ',' + it.y + ')');
      g._text.textContent = it.text;
      if (it.dir === 'down') {
        g._text.setAttribute('x', 0); g._text.setAttribute('y', -12); g._text.setAttribute('text-anchor', 'middle');
        g._path.setAttribute('d', 'M0,-8 L0,-1 M-3,-4 L0,-1 L3,-4');
      } else if (it.dir === 'up') {
        g._text.setAttribute('x', 0); g._text.setAttribute('y', 22); g._text.setAttribute('text-anchor', 'middle');
        g._path.setAttribute('d', 'M0,10 L0,1 M-3,4 L0,1 L3,4');
      } else {
        g._text.setAttribute('x', 12); g._text.setAttribute('y', 4); g._text.setAttribute('text-anchor', 'start');
        g._path.setAttribute('d', 'M9,0 L1,0 M4,-3 L1,0 L4,3');
      }
    }
  };

  /* ---------------- 播放器 ---------------- */

  var uid = 0;
  function mount(host) {
    var def = registry[host.getAttribute('data-viz')];
    if (!def) { host.textContent = '找不到演示：' + host.getAttribute('data-viz'); return; }
    var id = ++uid;
    host.innerHTML = '';
    host.setAttribute('tabindex', '0');

    var head = h('div', 'dv-head', host);
    h('span', 'dv-title', head, def.title);
    var counter = h('span', 'dv-count', head);

    var body = h('div', 'dv-body' + (def.code ? ' has-code' : ''), host);
    var stageBox = h('div', 'dv-stage', body);
    var width = def.width || 720, height = def.height || 200;
    var svg = el('svg', { viewBox: '0 0 ' + width + ' ' + height, role: 'img', 'aria-label': def.title }, null);
    stageBox.appendChild(svg);
    var defs = el('defs', {}, svg);
    var mk = el('marker', { id: 'dv-arrow-' + id, viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse' }, defs);
    el('path', { d: 'M0,0 L10,5 L0,10 z', 'class': 'dv-marker' }, mk);
    var stage = new Stage(svg, 'dv-arrow-' + id);

    var codeBox = null, codeLines = [];
    if (def.code) {
      codeBox = h('pre', 'dv-code', body);
      def.code.forEach(function (line, i) {
        var ln = h('span', 'dv-line', codeBox);
        h('span', 'dv-ln', ln, String(i + 1));
        h('span', null, ln, line || ' ');
        codeLines.push(ln);
      });
    }

    var msg = h('div', 'dv-msg', host);
    var vars = h('div', 'dv-vars', host);

    var bar = h('div', 'dv-bar', host);
    var bReset = h('button', 'dv-btn', bar, '⏮'); bReset.title = '回到第一步';
    var bPrev = h('button', 'dv-btn', bar, '◀'); bPrev.title = '上一步（←）';
    var bPlay = h('button', 'dv-btn dv-play', bar, '▶ 播放'); bPlay.title = '播放 / 暂停（空格）';
    var bNext = h('button', 'dv-btn', bar, '▶|'); bNext.title = '下一步（→）';
    var speed = h('select', 'dv-speed', bar);
    [['0.5×', 1800], ['1×', 1000], ['2×', 500]].forEach(function (o, i) {
      var opt = h('option', null, speed, o[0]); opt.value = o[1]; if (i === 1) opt.selected = true;
    });
    speed.title = '播放速度';

    var acts = h('div', 'dv-acts', host);
    var model = def.init ? def.init() : {};
    var steps = [], cur = 0, timer = null;

    function snapshot(line, text, extra) {
      var scene = def.view(model, extra || {});
      steps.push({ line: line, msg: text, scene: clone(scene) });
    }
    var rec = { step: snapshot };

    function show(i, animate) {
      cur = Math.max(0, Math.min(i, steps.length - 1));
      var s = steps[cur];
      var kind = s.scene.kind;
      stage.draw(R[kind](Object.assign({ height: height, width: width }, s.scene)), animate && !reduceMotion);
      msg.innerHTML = '';
      h('span', 'dv-step', msg, '第 ' + (cur + 1) + ' 步');
      h('span', null, msg, s.msg || '');
      vars.innerHTML = '';
      var v = s.scene.vars || {};
      Object.keys(v).forEach(function (k) {
        var chip = h('span', 'dv-var', vars);
        h('span', 'dv-k', chip, k);
        h('span', 'dv-v', chip, String(v[k]));
      });
      codeLines.forEach(function (ln, j) { ln.classList.toggle('is-cur', s.line === j + 1); });
      if (codeBox && s.line) {
        var ln = codeLines[s.line - 1];
        if (ln) codeBox.scrollTop = Math.max(0, ln.offsetTop - codeBox.clientHeight / 2);
      }
      counter.textContent = (cur + 1) + ' / ' + steps.length;
      bPrev.disabled = bReset.disabled = cur === 0;
      bNext.disabled = cur === steps.length - 1;
    }
    function stop() {
      if (timer) { clearInterval(timer); timer = null; }
      bPlay.textContent = '▶ 播放';
    }
    function play() {
      if (cur >= steps.length - 1) show(0, false);
      bPlay.textContent = '⏸ 暂停';
      timer = setInterval(function () {
        if (cur >= steps.length - 1) { stop(); return; }
        show(cur + 1, true);
      }, +speed.value);
    }
    function run(action, params) {
      stop();
      steps = [];
      if (action) action.run(model, params, rec);
      if (!steps.length) snapshot(0, '');
      show(0, true);
    }

    bReset.onclick = function () { stop(); show(0, true); };
    bPrev.onclick = function () { stop(); show(cur - 1, true); };
    bNext.onclick = function () { stop(); show(cur + 1, true); };
    bPlay.onclick = function () { if (timer) stop(); else play(); };
    speed.onchange = function () { if (timer) { stop(); play(); } };
    host.addEventListener('keydown', function (e) {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
      if (e.key === 'ArrowRight') { e.preventDefault(); bNext.click(); }
      else if (e.key === 'ArrowLeft') { e.preventDefault(); bPrev.click(); }
      else if (e.key === ' ') { e.preventDefault(); bPlay.click(); }
    });

    (def.actions || []).forEach(function (a) {
      var grp = h('div', 'dv-act', acts);
      var inputs = {};
      (a.fields || []).forEach(function (f) {
        var lab = h('label', null, grp);
        h('span', null, lab, f.label);
        var inp = h('input', null, lab);
        inp.value = f.value;
        inp.size = f.size || 4;
        inp.addEventListener('keydown', function (e) { if (e.key === 'Enter') btn.click(); });
        inputs[f.key] = inp;
      });
      var btn = h('button', 'dv-btn dv-go', grp, a.label);
      btn.onclick = function () {
        var params = {};
        for (var k in inputs) {
          var raw = inputs[k].value.trim();
          params[k] = raw !== '' && !isNaN(+raw) ? +raw : raw;
        }
        var err = a.check && a.check(model, params);
        if (err) { stop(); msg.innerHTML = ''; h('span', 'dv-err', msg, err); return; }
        run(a, params);
        if (!reduceMotion) play();
      };
    });

    // 初始画面：第一个 action 用默认参数先算好，但不自动播放
    if (def.start) {
      steps = [];
      def.start(model, rec);
      if (!steps.length) snapshot(0, '');
      show(0, false);
    } else {
      run(null, {});
    }
  }

  window.DSViz = {
    register: function (name, def) { registry[name] = def; },
    mountAll: function () {
      var list = document.querySelectorAll('.dsviz[data-viz]');
      for (var i = 0; i < list.length; i++) mount(list[i]);
    }
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { window.DSViz.mountAll(); });
  else setTimeout(function () { window.DSViz.mountAll(); }, 0);
})();
