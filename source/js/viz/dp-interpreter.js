/* 设计模式专栏《解释器》：AST 自底向上求值与 and / or 短路求值（dp-interpreter） */
(function () {
  'use strict';

  var NODES = [
    { id: 'or',    x: 260, y: 42,  w: 195, h: 44, text: 'OrExpr (or)' },
    { id: 'and',   x: 105, y: 124, w: 195, h: 44, text: 'AndExpr (and)' },
    { id: 'codec', x: 430, y: 124, w: 225, h: 44, text: 'Cmp: codec == "hevc"' },
    { id: 'w',     x: 20,  y: 210, w: 195, h: 44, text: 'Cmp: width >= 1920' },
    { id: 'f',     x: 235, y: 210, w: 180, h: 44, text: 'Cmp: fps >= 60' }
  ];

  var EDGES = [
    { from: 'or',  to: 'and' },
    { from: 'or',  to: 'codec' },
    { from: 'and', to: 'w' },
    { from: 'and', to: 'f' }
  ];

  function reset(m, clip) {
    m.clip = clip;
    m.res = { or: '待求值', and: '待求值', codec: '待求值', w: '待求值', f: '待求值' };
    m.result = '—';
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, ehl = x.ehl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'expr', x: 20, y: 22, text: 'AST：(width >= 1920 and fps >= 60) or codec == "hevc"', cls: 'muted' });
    NODES.forEach(function (n) {
      boxes.push({
        id: n.id, x: n.x, y: n.y, w: n.w, h: n.h,
        text: n.text, sub: m.res[n.id], hl: hl[n.id]
      });
    });
    EDGES.forEach(function (e) {
      var k = e.from + '->' + e.to;
      arrows.push({ from: e.from, to: e.to, hl: ehl[k] });
    });
    boxes.push({
      id: 'ctx', x: 455, y: 204, w: 245, h: 56,
      text: 'Context: ' + m.clip.name,
      sub: 'width=' + m.clip.width + '  fps=' + m.clip.fps + '  codec="' + m.clip.codec + '"',
      hl: hl.ctx || 'new'
    });
    texts.push({ id: 'out', x: 20, y: 288, text: '求值结果：' + m.result + (x.note ? '    ' + x.note : '') });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function runClip(m, clip, rec) {
    reset(m, clip);
    rec.step(1, '从根节点 OrExpr::eval(clip) 开始求值。Context 传入当前素材 ' + clip.name + '（' + clip.width + '×' + clip.fps + 'fps, ' + clip.codec + '）。', {
      hl: { or: 'cur', ctx: 'new' }
    });
    rec.step(2, 'OrExpr 先求值左子树 AndExpr::eval(clip)。', {
      hl: { or: 'cmp', and: 'cur' }, ehl: { 'or->and': 'cur' }
    });

    var wOk = clip.width >= 1920;
    m.res.w = clip.width + ' >= 1920 → ' + (wOk ? 'true' : 'false');
    rec.step(3, 'AndExpr 先求值左叶子 CmpExpr(width >= 1920)：' + clip.width + ' >= 1920 得到 ' + wOk + '。', {
      hl: { and: 'cmp', w: wOk ? 'ok' : 'warn' }, ehl: { 'and->w': 'cur' }
    });

    var andOk;
    if (!wOk) {
      m.res.f = '跳过（and 短路）';
      m.res.and = '→ false（短路）';
      andOk = false;
      rec.step(4, '左子树已经是 false！C++ 的 && 触发短路，直接跳过右叶子 fps >= 60，AndExpr 返回 false。', {
        hl: { and: 'warn', w: 'warn', f: 'dim' }
      });
    } else {
      var fOk = clip.fps >= 60;
      m.res.f = clip.fps + ' >= 60 → ' + (fOk ? 'true' : 'false');
      rec.step(4, '左叶子为 true，AndExpr 继续求值右叶子 CmpExpr(fps >= 60)：' + clip.fps + ' >= 60 得到 ' + fOk + '。', {
        hl: { and: 'cmp', w: 'ok', f: fOk ? 'ok' : 'warn' }, ehl: { 'and->f': 'cur' }
      });
      andOk = fOk;
      m.res.and = '→ ' + (andOk ? 'true' : 'false');
      rec.step(4, '左右叶子结果自底向上汇总到 AndExpr：true && ' + fOk + ' = ' + andOk + '。', {
        hl: { and: andOk ? 'ok' : 'warn', w: 'ok', f: fOk ? 'ok' : 'warn' }
      });
    }

    if (andOk) {
      m.res.codec = '跳过（or 短路）';
      m.res.or = '→ true（短路）';
      m.result = 'true（命中）';
      rec.step(5, 'OrExpr 的左子树已经返回 true！|| 触发短路，直接跳过右子树 codec == "hevc"，整棵树返回 true。', {
        hl: { or: 'ok', and: 'ok', codec: 'dim' },
        note: '少做了 1 次字符串比较'
      });
    } else {
      var cOk = clip.codec === 'hevc';
      m.res.codec = '"' + clip.codec + '" == "hevc" → ' + (cOk ? 'true' : 'false');
      rec.step(5, 'OrExpr 左子树为 false，必须继续求值右子树 CmpExpr(codec == "hevc")：得到 ' + cOk + '。', {
        hl: { or: 'cmp', and: 'warn', codec: cOk ? 'ok' : 'warn' }, ehl: { 'or->codec': 'cur' }
      });
      m.res.or = '→ ' + (cOk ? 'true' : 'false');
      m.result = cOk ? 'true（命中）' : 'false（过滤掉）';
      rec.step(1, '结果自底向上返回到根节点 OrExpr：false || ' + cOk + ' = ' + cOk + '，素材' + (cOk ? '保留。' : '被过滤掉。'), {
        hl: { or: cOk ? 'ok' : 'warn', codec: cOk ? 'ok' : 'warn' }
      });
    }
  }

  var CLIPS = [
    { label: 'intro_4k（or 短路）',  name: 'intro_4k.mov',  width: 3840, fps: 60, codec: 'hevc' },
    { label: 'drone_hdr（右枝命中）', name: 'drone_hdr.mkv', width: 2560, fps: 30, codec: 'hevc' },
    { label: 'proxy_720（and 短路）', name: 'proxy_720.mp4', width: 1280, fps: 24, codec: 'h264' },
    { label: 'interview（全树未中）', name: 'interview.mp4', width: 1920, fps: 30, codec: 'h264' }
  ];

  DSViz.register('dp-interpreter', {
    title: '解释器 AST 求值：自底向上汇总与 and / or 短路',
    width: 720,
    height: 304,
    code: [
      'bool ok = ast->eval(clip);                        // 同一棵 AST 反复对每条素材求值',
      'bool OrExpr::eval(c)  { return l->eval(c) || r->eval(c); }',
      'bool CmpExpr::eval(c) { return lhs >= ival; }     // 终结符：从 Context 取字段比较',
      'bool AndExpr::eval(c) { return l->eval(c) && r->eval(c); } // l 为 false 时短路',
      'bool OrExpr::eval(c)  { return l->eval(c) || r->eval(c); } // l 为 true 时短路'
    ],
    init: function () { var m = {}; reset(m, CLIPS[0]); return m; },
    view: view,
    start: function (m, rec) { runClip(m, CLIPS[0], rec); },
    actions: CLIPS.map(function (c) {
      return { label: c.label, run: function (m, p, rec) { runClip(m, c, rec); } };
    })
  });
})();
