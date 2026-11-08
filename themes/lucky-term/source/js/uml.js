/**
 * uml —— 把一段 UML 类图描述画成 SVG（浏览器和 Hexo 构建脚本共用）
 *
 * 文章里写一个 ```uml 代码块，内容是 YAML（themes/lucky-term/scripts/uml.js 在构建时换成内联 SVG）；
 * dsviz 演示的场景写 kind: 'uml'，字段相同，每一步可以给类和关系加 hl 高亮。
 *
 *   label: 图的说明（读屏用，可选）
 *   width / height: 画布大小，不写则按内容自动计算
 *   classes:
 *     - id: Shape            # 关系里引用的名字；不写 name 时也作为类名显示
 *       name: Shape
 *       stereo: interface    # interface → 显示 «interface»；abstract → 类名斜体；其他文字 → «文字»
 *       x: 40, y: 20         # 左上角
 *       w: 160               # 最小宽度；不写或放不下时按最长的一行自动计算
 *       attrs: ['- r_ : double']                 # 写了这个键才画属性栏（可以是空数组）
 *       ops:   ['+ area() : double']             # 写了这个键才画操作栏
 *       hl: cur | new | warn | ok | dim         # 高亮（演示用）
 *     成员行的前缀：* 斜体（抽象 / 纯虚函数），$ 下划线（静态成员）
 *   rels:
 *     - { from: Circle, to: Shape, type: inherit }
 *       type：inherit 泛化（实线 + 空心三角，指向父类）
 *             realize 实现（虚线 + 空心三角，指向接口）
 *             assoc   关联（实线 + 箭头，指向被引用的一方；nav: false 不画箭头）
 *             aggregate 聚合（from 是整体，画空心菱形；to 是部分）
 *             compose   组合（from 是整体，画实心菱形；to 是部分）
 *             depend  依赖（虚线 + 箭头，指向被依赖的一方）
 *       label: 关系上的文字；fm / tm: from 端 / to 端的多重性（如 1、0..1、*）
 *       fdx / tdx: 起点 / 终点沿边框偏移的像素（避免多条线重叠）；mid: 折线拐弯的位置
 *       route: straight 直接连直线（默认按位置走横平竖直的折线）
 *       points: [[x, y], ...] 手动指定整条折线（第一个点在 from 的边框上，最后一个点在 to 的边框上）
 *       hl: 同上
 *   notes:  [{ x, y, w, text: '一行' 或 ['多行'], to: 类 id, hl }]   折角注释，to 画一条虚线连过去
 *   texts:  [{ x, y, text, muted: true, anchor: start | middle | end }]
 *
 * 时序图：写了 participants 就按时序图画
 *   participants: [{ id, name, x, w, hl }]       # 从左往右排；不写 x 时按 gap（默认 60）自动排开
 *   messages:                                    # 从上往下，一条一行
 *     - { from: a, to: b, text: 'update()' }    # 同步调用：实线 + 实心三角，b 上出现激活条
 *     - { from: b, to: a, text: 'state', type: reply }   # 返回：虚线 + 开放箭头，结束 b 的激活条
 *     - { from: a, to: a, text: 'notify()' }    # 自调用：从右边绕一圈回来
 *       type 还可以是 create（目标在这一行才出现）、async（实线 + 开放箭头）
 *       n: 序号（显示成「1. 」）；note: 线下方的小字；hl: 高亮；key: 演示中复用元素的名字
 *   frags: [{ from: 2, to: 4, label: 'loop [每个观察者]' }]   # 组合片段，框住第 from～to 条消息（从 0 数）
 *   rows: 生命线固定画多少行（演示里消息一条条出现时，图的高度不变）；gap: 参与者之间的距离
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.UML = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  var FS = 12, NAME_FS = 13, LINE = 17, PADX = 10;
  var C = {
    fg: 'var(--fg)', fg2: 'var(--fg-2)', muted: 'var(--muted)', panel: 'var(--panel)', panel2: 'var(--panel-2)',
    accent: 'var(--accent)', soft: 'var(--accent-soft)', link: 'var(--link)', danger: 'var(--danger)', mark: 'var(--mark)', border: 'var(--border-2)'
  };

  function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
  // 等宽字体下的文字宽度：中文按一个字号宽，其余按 0.6 个字号
  function textW(s, fs) {
    var w = 0;
    for (var i = 0; i < s.length; i++) w += s.charCodeAt(i) > 0x2e80 ? fs : fs * 0.6;
    return w;
  }
  function num(v) { return Math.round(v * 10) / 10; }
  function lines(t) { return t == null ? [] : [].concat(t).map(String); }

  // 高亮状态 → 颜色
  function boxStyle(hl) {
    var s = { stroke: C.fg2, sw: 1.3, head: C.panel2, body: C.panel, dash: '', op: 1 };
    if (hl === 'cur') { s.stroke = C.accent; s.sw = 2.5; s.head = C.soft; }
    else if (hl === 'new') { s.stroke = C.link; s.sw = 2.2; s.dash = '5 3'; }
    else if (hl === 'warn') { s.stroke = C.danger; s.sw = 2.5; s.head = C.mark; }
    else if (hl === 'ok') { s.stroke = C.accent; s.sw = 1.8; s.head = C.soft; }
    else if (hl === 'dim') { s.op = 0.3; }
    return s;
  }
  function lineColor(hl) {
    return hl === 'cur' || hl === 'ok' ? C.accent : hl === 'new' ? C.link : hl === 'warn' ? C.danger : C.fg2;
  }

  /* ---------------- 类 ---------------- */

  function member(raw) {
    var t = String(raw), italic = false, under = false;
    for (;;) {
      if (t[0] === '*') { italic = true; t = t.slice(1); }
      else if (t[0] === '$') { under = true; t = t.slice(1); }
      else break;
    }
    return { text: t, italic: italic, under: under };
  }

  function measureClass(c) {
    var name = c.name || c.id, stereo = c.stereo;
    var stereoText = stereo && stereo !== 'abstract' ? '«' + stereo + '»' : '';
    var attrs = c.attrs ? c.attrs.map(member) : null, ops = c.ops ? c.ops.map(member) : null;
    var w = Math.max(textW(name, NAME_FS) + 2 * PADX, stereoText ? textW(stereoText, 11) + 2 * PADX : 0, 90);
    [attrs, ops].forEach(function (list) {
      (list || []).forEach(function (m) { w = Math.max(w, textW(m.text, FS) + 2 * PADX); });
    });
    w = Math.max(c.w || 0, Math.ceil(w));     // 指定的宽度只是下限，文字放不下时自动加宽
    var headH = (stereoText ? 14 : 0) + 30;
    var attrH = attrs ? (attrs.length ? attrs.length * LINE + 8 : 10) : 0;
    var opsH = ops ? (ops.length ? ops.length * LINE + 8 : 10) : 0;
    return { id: c.id, x: c.x || 0, y: c.y || 0, w: w, h: headH + attrH + opsH, headH: headH, attrH: attrH, opsH: opsH,
      name: name, stereoText: stereoText, italic: stereo === 'abstract', attrs: attrs, ops: ops, hl: c.hl };
  }

  function drawClass(b) {
    var s = boxStyle(b.hl), o = [];
    var dash = s.dash ? ';stroke-dasharray:' + s.dash : '';
    var stroke = 'stroke:' + s.stroke + ';stroke-width:' + s.sw + dash;
    o.push('<g' + (s.op < 1 ? ' style="opacity:' + s.op + '"' : '') + '>');
    o.push('<rect x="' + b.x + '" y="' + b.y + '" width="' + b.w + '" height="' + b.h + '" rx="3" style="fill:' + s.body + ';' + stroke + '"/>');
    o.push('<path d="M' + (b.x + 0.7) + ',' + (b.y + 0.7) + ' h' + (b.w - 1.4) + ' v' + (b.headH - 0.7) + ' h-' + (b.w - 1.4) + ' z" style="fill:' + s.head + '"/>');
    var cx = num(b.x + b.w / 2), y = b.y;
    if (b.stereoText) o.push('<text x="' + cx + '" y="' + (y + 17) + '" text-anchor="middle" style="fill:' + C.muted + ';font-size:11px">' + esc(b.stereoText) + '</text>');
    o.push('<text x="' + cx + '" y="' + (y + b.headH - 11) + '" text-anchor="middle" style="fill:' + C.fg + ';font-size:' + NAME_FS + 'px;font-weight:700' +
      (b.italic ? ';font-style:italic' : '') + '">' + esc(b.name) + '</text>');
    y += b.headH;
    [[b.attrs, b.attrH], [b.ops, b.opsH]].forEach(function (sec) {
      if (!sec[0]) return;
      o.push('<line x1="' + b.x + '" y1="' + y + '" x2="' + (b.x + b.w) + '" y2="' + y + '" style="' + stroke + '"/>');
      sec[0].forEach(function (m, i) {
        o.push('<text x="' + (b.x + PADX) + '" y="' + (y + 16 + i * LINE) + '" style="fill:' + C.fg + ';font-size:' + FS + 'px' +
          (m.italic ? ';font-style:italic' : '') + (m.under ? ';text-decoration:underline' : '') + '">' + esc(m.text) + '</text>');
      });
      y += sec[1];
    });
    o.push('</g>');
    return o.join('');
  }

  /* ---------------- 关系 ---------------- */

  // 两个框之间的连线：上下错开走竖向折线，左右错开走横向折线
  function route(r, A, B) {
    if (r.points) return r.points.map(function (q) { return [q[0], q[1]]; });
    var gapY = Math.max(B.y - (A.y + A.h), A.y - (B.y + B.h));
    var gapX = Math.max(B.x - (A.x + A.w), A.x - (B.x + B.w));
    var vertical = gapY > 0 && gapY >= gapX;
    var fdx = r.fdx || 0, tdx = r.tdx || 0, p;
    if (vertical) {
      var down = B.y > A.y;
      var fx = A.x + A.w / 2 + fdx, fy = down ? A.y + A.h : A.y;
      var tx = B.x + B.w / 2 + tdx, ty = down ? B.y : B.y + B.h;
      if (r.route === 'straight' || Math.abs(fx - tx) < 1) p = [[fx, fy], [tx, ty]];
      else { var my = r.mid != null ? r.mid : (fy + ty) / 2; p = [[fx, fy], [fx, my], [tx, my], [tx, ty]]; }
    } else {
      var right = B.x > A.x;
      var fx2 = right ? A.x + A.w : A.x, fy2 = A.y + A.h / 2 + fdx;
      var tx2 = right ? B.x : B.x + B.w, ty2 = B.y + B.h / 2 + tdx;
      if (r.route === 'straight' || Math.abs(fy2 - ty2) < 1) p = [[fx2, fy2], [tx2, ty2]];
      else { var mx = r.mid != null ? r.mid : (fx2 + tx2) / 2; p = [[fx2, fy2], [mx, fy2], [mx, ty2], [tx2, ty2]]; }
    }
    return p;
  }

  function unit(a, b) {
    var dx = b[0] - a[0], dy = b[1] - a[1], d = Math.sqrt(dx * dx + dy * dy) || 1;
    return [dx / d, dy / d];
  }
  function pt(p) { return num(p[0]) + ',' + num(p[1]); }

  function drawRel(r, A, B) {
    var p = route(r, A, B), n = p.length, o = [];
    var type = r.type || 'assoc', col = lineColor(r.hl);
    var dashed = type === 'realize' || type === 'depend';
    var sw = r.hl === 'cur' || r.hl === 'warn' ? 2.4 : r.hl === 'new' ? 2 : 1.4;
    var ue = unit(p[n - 2], p[n - 1]), us = unit(p[1], p[0]);   // 终点方向、起点方向（都朝外）
    var end = p[n - 1].slice(), start = p[0].slice(), shapes = [];

    if (type === 'inherit' || type === 'realize') {            // 空心三角
      var L = 13, W = 7, base = [end[0] - ue[0] * L, end[1] - ue[1] * L];
      shapes.push('<path d="M' + pt(end) + ' L' + pt([base[0] - ue[1] * W, base[1] + ue[0] * W]) + ' L' + pt([base[0] + ue[1] * W, base[1] - ue[0] * W]) +
        ' z" style="fill:' + C.panel + ';stroke:' + col + ';stroke-width:' + Math.max(sw, 1.4) + ';stroke-linejoin:round"/>');
      p[n - 1] = base;
    } else if ((type === 'assoc' && r.nav !== false) || type === 'depend') {   // 开放箭头
      var AL = 10, AW = 5.5, bb = [end[0] - ue[0] * AL, end[1] - ue[1] * AL];
      shapes.push('<path d="M' + pt([bb[0] - ue[1] * AW, bb[1] + ue[0] * AW]) + ' L' + pt(end) + ' L' + pt([bb[0] + ue[1] * AW, bb[1] - ue[0] * AW]) +
        '" style="fill:none;stroke:' + col + ';stroke-width:' + Math.max(sw, 1.4) + ';stroke-linejoin:round"/>');
    }
    if (type === 'aggregate' || type === 'compose') {          // 菱形画在整体（from）一端
      var DL = 18, DW = 6, far = [start[0] - us[0] * DL, start[1] - us[1] * DL], mid = [start[0] - us[0] * DL / 2, start[1] - us[1] * DL / 2];
      shapes.push('<path d="M' + pt(start) + ' L' + pt([mid[0] - us[1] * DW, mid[1] + us[0] * DW]) + ' L' + pt(far) + ' L' + pt([mid[0] + us[1] * DW, mid[1] - us[0] * DW]) +
        ' z" style="fill:' + (type === 'compose' ? col : C.panel) + ';stroke:' + col + ';stroke-width:' + Math.max(sw, 1.4) + ';stroke-linejoin:round"/>');
      p[0] = far;
    }
    o.push('<g' + (r.hl === 'dim' ? ' style="opacity:.3"' : '') + '>');
    o.push('<path d="M' + p.map(pt).join(' L') + '" style="fill:none;stroke:' + col + ';stroke-width:' + sw + (dashed ? ';stroke-dasharray:6 4' : '') + '"/>');
    o.push(shapes.join(''));

    var lab = function (x, y, t, anchor, color) {
      return '<text x="' + num(x) + '" y="' + num(y) + '" text-anchor="' + anchor + '" style="fill:' + (color || C.muted) +
        ';font-size:11px;paint-order:stroke;stroke:' + C.panel + ';stroke-width:3px;stroke-linejoin:round">' + esc(t) + '</text>';
    };
    if (r.label) {
      var full = route(r, A, B), k = Math.floor((full.length - 1) / 2), a = full[k], b = full[k + 1];
      var mx = (a[0] + b[0]) / 2 + (r.lx || 0), my = (a[1] + b[1]) / 2 + (r.ly || 0);
      var horiz = Math.abs(a[1] - b[1]) < 1;
      o.push(lab(horiz ? mx : mx + 6, horiz ? my - 6 : my + 4, r.label, horiz ? 'middle' : 'start', r.hl && r.hl !== 'dim' ? col : null));
    }
    // 多重性：写在端点旁边、线的外侧，沿线方向让开箭头或菱形；u 是从线上指向端点（朝框外）的方向
    [[r.fm, start, us, type === 'aggregate' || type === 'compose' ? 22 : 5], [r.tm, end, ue, type === 'inherit' || type === 'realize' ? 17 : 13]].forEach(function (m) {
      if (m[0] == null) return;
      var e = m[1], u = m[2], back = m[3];
      if (Math.abs(u[0]) >= Math.abs(u[1])) {        // 横着进入框：写在线的上方，靠线的一侧对齐
        o.push(lab(e[0] - u[0] * back, e[1] - u[1] * back - 6, m[0], u[0] > 0 ? 'end' : 'start'));
      } else {                                        // 竖着进入框：写在线的右边
        o.push(lab(e[0] + 7, u[1] > 0 ? e[1] - back + 4 : e[1] + back + 4, m[0], 'start'));
      }
    });
    o.push('</g>');
    return o.join('');
  }

  /* ---------------- 注释和文字 ---------------- */

  function drawNote(nt, boxes) {
    var ls = lines(nt.text), w = nt.w || Math.ceil(Math.max.apply(null, ls.map(function (l) { return textW(l, FS); }).concat([40])) + 2 * PADX + 6);
    var h = ls.length * LINE + 12, x = nt.x || 0, y = nt.y || 0, F = 9, o = [];
    var s = boxStyle(nt.hl);
    o.push('<g' + (s.op < 1 ? ' style="opacity:' + s.op + '"' : '') + '>');
    var t = nt.to && boxes[nt.to];
    if (t) {
      var cx = x + w / 2, cy = y + h / 2, tx = Math.max(t.x, Math.min(cx, t.x + t.w)), ty = Math.max(t.y, Math.min(cy, t.y + t.h));
      var sx = Math.max(x, Math.min(tx, x + w)), sy = Math.max(y, Math.min(ty, y + h));
      o.push('<path d="M' + pt([sx, sy]) + ' L' + pt([tx, ty]) + '" style="fill:none;stroke:' + C.border + ';stroke-width:1.3;stroke-dasharray:4 3"/>');
    }
    var stroke = nt.hl && nt.hl !== 'dim' ? s.stroke : C.border;
    o.push('<path d="M' + x + ',' + y + ' h' + (w - F) + ' l' + F + ',' + F + ' v' + (h - F) + ' h-' + w + ' z" style="fill:' + C.mark + ';stroke:' + stroke + ';stroke-width:1.3"/>');
    o.push('<path d="M' + (x + w - F) + ',' + y + ' v' + F + ' h' + F + '" style="fill:none;stroke:' + stroke + ';stroke-width:1.3"/>');
    ls.forEach(function (l, i) {
      o.push('<text x="' + (x + PADX) + '" y="' + (y + 18 + i * LINE) + '" style="fill:' + C.fg + ';font-size:' + FS + 'px;white-space:pre">' + esc(l) + '</text>');
    });
    o.push('</g>');
    return { svg: o.join(''), w: w, h: h };
  }

  /* ---------------- 时序图 ---------------- */

  var ROW = 36, TOP = 10, HEAD = 32;

  function seqParts(spec) {
    var out = [], ps = [], byId = {}, msgs = spec.messages || [], x = 20, gap = spec.gap || 60;
    // 每条消息占一行，自调用占两行；rowY[i] 是第 i 条消息的 y
    var rowY = [], y = TOP + HEAD + 30, frags = spec.frags || [];
    msgs.forEach(function (m, i) {
      frags.forEach(function (f) { if (f.from === i) y += 26; });           // 片段的标签要占一点地方
      rowY.push(y);
      y += m.from === m.to ? ROW + 14 : ROW;
      frags.forEach(function (f) { if (f.to === i) y += 10; });
    });
    var rows = spec.rows != null ? spec.rows : msgs.length;          // 演示时固定生命线长度
    var bottom = Math.max(y, TOP + HEAD + 30 + rows * ROW) + 4;

    (spec.participants || []).forEach(function (p) {
      var name = p.name || p.id, w = Math.max(p.w || 0, Math.ceil(textW(name, FS) + 2 * PADX), 80);
      var q = { id: p.id, name: name, w: w, x: p.x != null ? p.x : x, hl: p.hl, top: TOP, actor: p.actor };
      q.cx = q.x + w / 2;
      x = q.x + w + gap;
      ps.push(q); byId[q.id] = q;
    });
    // create 消息：目标在这一行才出现
    msgs.forEach(function (m, i) { if (m.type === 'create' && byId[m.to]) byId[m.to].top = rowY[i] - HEAD / 2; });

    // 激活条：调用压栈，同一个对象的返回弹栈；没有返回的调用只占一小段
    var acts = [], stack = [];
    msgs.forEach(function (m, i) {
      if (m.from === m.to) { acts.push({ who: m.to, y1: rowY[i] + 8, y2: rowY[i] + ROW, depth: 1, hl: m.hl }); return; }
      if (m.type === 'reply') {
        for (var k = stack.length - 1; k >= 0; k--) if (stack[k].who === m.from) { stack[k].y2 = rowY[i]; acts.push(stack[k]); stack.splice(k, 1); break; }
        return;
      }
      if (m.type === 'create' || m.type === 'async') return;
      stack.push({ who: m.to, y1: rowY[i], y2: null, depth: 0 });
    });
    stack.forEach(function (a) { a.y2 = a.y1 + 18; acts.push(a); });

    ps.forEach(function (q) {
      var s = boxStyle(q.hl), o = [];
      o.push('<g' + (s.op < 1 ? ' style="opacity:' + s.op + '"' : '') + '>');
      o.push('<line x1="' + q.cx + '" y1="' + (q.top + HEAD) + '" x2="' + q.cx + '" y2="' + bottom + '" style="stroke:' + C.border + ';stroke-width:1.3;stroke-dasharray:5 4"/>');
      o.push('<rect x="' + q.x + '" y="' + q.top + '" width="' + q.w + '" height="' + HEAD + '" rx="3" style="fill:' + (q.hl ? s.head : C.panel2) +
        ';stroke:' + s.stroke + ';stroke-width:' + s.sw + (s.dash ? ';stroke-dasharray:' + s.dash : '') + '"/>');
      o.push('<text x="' + num(q.cx) + '" y="' + (q.top + 21) + '" text-anchor="middle" style="fill:' + C.fg + ';font-size:' + FS + 'px;font-weight:700">' + esc(q.name) + '</text>');
      o.push('</g>');
      out.push({ key: 'p:' + q.id, layer: 0, svg: o.join('') });
    });
    acts.forEach(function (a, i) {
      var q = byId[a.who];
      if (!q) return;
      var ax = q.cx - 5 + a.depth * 6;
      out.push({ key: 'a:' + a.who + ':' + Math.round(a.y1), layer: 1, svg: '<rect x="' + ax + '" y="' + a.y1 + '" width="10" height="' + Math.max(8, a.y2 - a.y1) +
        '" style="fill:' + (a.hl === 'cur' ? C.soft : C.panel) + ';stroke:' + C.fg2 + ';stroke-width:1.2"/>' });
    });

    // 组合片段（loop / alt / opt）：框住第 from 到第 to 条消息（从 0 数）
    frags.forEach(function (f, i) {
      var inv = msgs.slice(f.from, f.to + 1), ids = {};
      inv.forEach(function (m) { ids[m.from] = 1; ids[m.to] = 1; });
      var xs = Object.keys(ids).map(function (id) { return byId[id]; }).filter(Boolean);
      if (!xs.length) return;
      var x1 = Math.min.apply(null, xs.map(function (q) { return q.cx; })) - (f.padL || 40);
      var x2 = Math.max.apply(null, xs.map(function (q) { return q.cx; })) + (f.padR || 40);
      var y1 = rowY[f.from] - 36, y2 = rowY[f.to] + (msgs[f.to].from === msgs[f.to].to ? ROW + 6 : 14);
      var lw = textW(f.label, 11) + 14;
      out.push({ key: 'f:' + i, layer: 0, svg: '<g' + (f.hl === 'dim' ? ' style="opacity:.3"' : '') + '><rect x="' + num(x1) + '" y="' + y1 + '" width="' + num(x2 - x1) + '" height="' + (y2 - y1) +
        '" style="fill:none;stroke:' + (f.hl === 'cur' ? C.accent : C.muted) + ';stroke-width:1.2"/>' +
        '<path d="M' + num(x1) + ',' + (y1 + 16) + ' h' + num(lw) + ' l6,-6 v-10" style="fill:' + C.panel2 + ';stroke:' + C.muted + ';stroke-width:1.2"/>' +
        '<text x="' + num(x1 + 6) + '" y="' + (y1 + 12) + '" style="fill:' + C.fg + ';font-size:11px;font-weight:700">' + esc(f.label) + '</text></g>' });
    });

    msgs.forEach(function (m, i) {
      var A = byId[m.from], B = byId[m.to];
      if (!A || !B) return;
      var col = lineColor(m.hl), sw = m.hl === 'cur' || m.hl === 'warn' ? 2.2 : 1.4, yy = rowY[i], o = [];
      var dashed = m.type === 'reply' || m.type === 'create';
      var lab = function (lx, ly, anchor) {
        return '<text x="' + num(lx) + '" y="' + num(ly) + '" text-anchor="' + anchor + '" style="fill:' + (m.hl && m.hl !== 'dim' ? col : C.fg) + ';font-size:' + FS +
          'px;paint-order:stroke;stroke:' + C.panel + ';stroke-width:3px;stroke-linejoin:round">' + (m.n != null ? esc(m.n) + '. ' : '') + esc(m.text || '') + '</text>';
      };
      o.push('<g' + (m.hl === 'dim' ? ' style="opacity:.3"' : '') + '>');
      if (A === B) {                                   // 自调用：从右边绕出去再回来
        var sx = A.cx + 5, ex = A.cx + 11, rx = A.cx + 40;
        o.push('<path d="M' + sx + ',' + yy + ' H' + rx + ' V' + (yy + ROW - 4) + ' H' + (ex + 1) + '" style="fill:none;stroke:' + col + ';stroke-width:' + sw + '"/>');
        o.push('<path d="M' + (ex + 10) + ',' + (yy + ROW - 9) + ' L' + (ex + 1) + ',' + (yy + ROW - 4) + ' L' + (ex + 10) + ',' + (yy + ROW + 1) + ' z" style="fill:' + col + '"/>');
        o.push(lab(rx + 6, yy + 16, 'start'));
      } else {
        var right = B.cx > A.cx, x1 = A.cx + (right ? 5 : -5);
        var x2 = m.type === 'create' ? (right ? B.x : B.x + B.w) : B.cx + (right ? -5 : 5);
        var dir = right ? 1 : -1, tip = x2;
        o.push('<path d="M' + x1 + ',' + yy + ' H' + (tip - dir * (m.type === 'reply' ? 0 : 2)) + '" style="fill:none;stroke:' + col + ';stroke-width:' + sw + (dashed ? ';stroke-dasharray:6 4' : '') + '"/>');
        if (m.type === 'reply' || m.type === 'async') {  // 开放箭头
          o.push('<path d="M' + (tip - dir * 10) + ',' + (yy - 5) + ' L' + tip + ',' + yy + ' L' + (tip - dir * 10) + ',' + (yy + 5) + '" style="fill:none;stroke:' + col + ';stroke-width:' + Math.max(sw, 1.4) + '"/>');
        } else {                                         // 同步调用：实心三角
          o.push('<path d="M' + (tip - dir * 11) + ',' + (yy - 5) + ' L' + tip + ',' + yy + ' L' + (tip - dir * 11) + ',' + (yy + 5) + ' z" style="fill:' + col + '"/>');
        }
        o.push(lab((x1 + x2) / 2, yy - 6, 'middle'));
      }
      if (m.note) o.push('<text x="' + num(Math.max(A.cx, B.cx) + (A === B ? 44 : 12)) + '" y="' + (yy + (A === B ? 32 : 14)) + '" style="fill:' + C.muted + ';font-size:11px">' + esc(m.note) + '</text>');
      o.push('</g>');
      out.push({ key: 'm:' + (m.key || i), layer: 1, svg: o.join('') });
    });

    var last = ps[ps.length - 1];
    var maxX = last ? last.x + last.w : 100;
    msgs.forEach(function (m, i) {                   // 自调用和注释的文字可能超出最右边的参与者
      var A = byId[m.from];
      if (A && m.from === m.to) maxX = Math.max(maxX, A.cx + 46 + textW((m.n != null ? m.n + '. ' : '') + (m.text || ''), FS));
      if (A && m.note) maxX = Math.max(maxX, Math.max(A.cx, (byId[m.to] || A).cx) + 50 + textW(m.note, 11));
    });
    return { parts: out, width: spec.width || Math.ceil(maxX + 16), height: spec.height || bottom + 10 };
  }

  /* ---------------- 对外接口 ---------------- */

  // 返回每个元素各自的 SVG 片段（dsviz 按 key 复用元素，做出现 / 消失的动画）
  function parts(spec) {
    spec = spec || {};
    if (spec.participants) return seqParts(spec);
    var out = [], boxes = {}, maxX = 0, maxY = 0;
    (spec.classes || []).forEach(function (c) {
      var b = measureClass(c);
      boxes[b.id] = b;
      maxX = Math.max(maxX, b.x + b.w); maxY = Math.max(maxY, b.y + b.h);
    });
    (spec.rels || []).forEach(function (r, i) {
      var A = boxes[r.from], B = boxes[r.to];
      if (!A || !B) return;
      out.push({ key: 'r:' + (r.key || r.from + '>' + r.to + ':' + (r.type || 'assoc')), layer: 0, svg: drawRel(r, A, B) });
      route(r, A, B).forEach(function (q) {           // 手动指定的折线可能伸到所有框的外面，标签再留点地方
        maxX = Math.max(maxX, q[0] + (r.label ? textW(String(r.label), 11) + 10 : 4));
        maxY = Math.max(maxY, q[1] + 4);
      });
    });
    (spec.notes || []).forEach(function (nt, i) {
      var d = drawNote(nt, boxes);
      maxX = Math.max(maxX, (nt.x || 0) + d.w); maxY = Math.max(maxY, (nt.y || 0) + d.h);
      out.push({ key: 'n:' + (nt.id || i), layer: 0, svg: d.svg });
    });
    Object.keys(boxes).forEach(function (id) { out.push({ key: 'c:' + id, layer: 1, svg: drawClass(boxes[id]) }); });
    (spec.texts || []).forEach(function (t, i) {
      out.push({ key: 't:' + (t.id || i), layer: 1, svg: '<text x="' + t.x + '" y="' + t.y + '" text-anchor="' + (t.anchor || 'start') + '" style="fill:' +
        (t.muted ? C.muted : C.fg) + ';font-size:' + (t.size || FS) + 'px' + (t.bold ? ';font-weight:700' : '') + '">' + esc(t.text) + '</text>' });
      maxX = Math.max(maxX, t.x + (t.anchor === 'middle' ? textW(String(t.text), FS) / 2 : t.anchor === 'end' ? 0 : textW(String(t.text), FS)));
      maxY = Math.max(maxY, t.y + 6);
    });
    return { parts: out, width: spec.width || Math.ceil(maxX + 12), height: spec.height || Math.ceil(maxY + 12) };
  }

  function svg(spec) {
    var r = parts(spec);
    var label = spec.label || (spec.participants
      ? '时序图：' + spec.participants.map(function (p) { return p.name || p.id; }).join('、')
      : '类图：' + (spec.classes || []).map(function (c) { return c.name || c.id; }).join('、'));
    // 最大按原始尺寸显示：页面比图宽时不放大，居中
    return '<svg viewBox="0 0 ' + r.width + ' ' + r.height + '" style="max-width:' + r.width + 'px;margin:0 auto" role="img" aria-label="' + esc(label) + '">' +
      '<g style="font-family:var(--mono)">' + r.parts.map(function (p) { return p.svg; }).join('') + '</g></svg>';
  }

  return { parts: parts, svg: svg, textWidth: textW };
});
