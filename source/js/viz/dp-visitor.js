/* 设计模式专栏《访问者》：双分派——accept 按元素类型分派一次，visit 按访问者类型再分派一次（dp-visitor） */
(function () {
  'use strict';

  // 字幕元素树：Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见
  var NODES = [
    { id: 'n0', kind: 'Cue', label: 'Cue（一条字幕）', depth: 0 },
    { id: 'n1', kind: 'Text', label: 'Text "Hello "', s: 'Hello ', depth: 1 },
    { id: 'n2', kind: 'Bold', label: 'Bold', depth: 1, kids: ['n3'] },
    { id: 'n3', kind: 'Text', label: 'Text "big"', s: 'big', depth: 2 },
    { id: 'n4', kind: 'Text', label: 'Text " "', s: ' ', depth: 1 },
    { id: 'n5', kind: 'Italic', label: 'Italic', depth: 1, kids: ['n6'] },
    { id: 'n6', kind: 'Color', label: 'Color #FF0000', depth: 2, kids: ['n7'] },
    { id: 'n7', kind: 'Text', label: 'Text "world"', s: 'world', depth: 3 },
    { id: 'n8', kind: 'Br', label: 'Br', depth: 1 },
    { id: 'n9', kind: 'Text', label: 'Text "再见"', s: '再见', depth: 1 }
  ];
  var TOP = ['n1', 'n2', 'n4', 'n5', 'n8', 'n9'];
  var BY = {};
  NODES.forEach(function (n) { BY[n.id] = n; });

  var ASS = {
    Bold: ['{\\b1}', '{\\b0}'], Italic: ['{\\i1}', '{\\i0}'], Color: ['{\\c&H0000FF&}', '{\\c}']
  };

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'tt', x: 20, y: 22, text: '字幕元素树', cls: 'muted' });
    texts.push({ id: 'tv', x: 400, y: 22, text: '两次分派', cls: 'muted' });
    NODES.forEach(function (n, i) {
      boxes.push({ id: n.id, x: 20 + n.depth * 30, y: 34 + i * 32, w: 190, h: 26, text: n.label, hl: hl[n.id] });
    });
    texts.push({ id: 't1', x: 400, y: 48, text: '① accept：看元素类型', cls: 'muted' });
    boxes.push({ id: 'd1', x: 460, y: 56, w: 260, h: 32, text: m.d1 || '—', hl: hl.d1 });
    texts.push({ id: 't2', x: 400, y: 130, text: '② visit：看访问者类型', cls: 'muted' });
    boxes.push({ id: 'd2', x: 460, y: 138, w: 260, h: 32, text: m.d2 || '—', hl: hl.d2 });
    texts.push({ id: 't3', x: 400, y: 212, text: '访问者', cls: 'muted' });
    boxes.push({ id: 'vis', x: 460, y: 220, w: 260, h: 44, text: m.visitor, sub: m.state, hl: hl.vis });
    if (m.cur) arrows.push({ from: m.cur, to: 'd1', hl: 'cur' });
    texts.push({ id: 'out', x: 20, y: 396, text: m.outLabel + ' ' + m.out });
    if (x.note) texts.push({ id: 'note', x: 400, y: 310, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m, visitor, outLabel) {
    m.visitor = visitor; m.state = ''; m.d1 = ''; m.d2 = ''; m.cur = null;
    m.outLabel = outLabel; m.out = '';
  }

  function hlOf(id, extra) {
    var h = { d1: 'cur', d2: 'cur', vis: 'new' };
    h[id] = 'cur';
    for (var k in extra) h[k] = extra[k];
    return h;
  }

  function assWalk(m, p, rec) {
    reset(m, 'AssExporter', 'out =');
    m.state = 'out = ""';
    rec.step(1, '遍历字幕的每个顶层元素，对每个元素调用 n->accept(v)，v 是一个 AssExporter。遍历代码手里只有 Node*，不知道具体类型。', { hl: { n0: 'cur', vis: 'new' } });
    function visit(id) {
      var n = BY[id];
      m.cur = id;
      m.d1 = n.kind + '::accept';
      m.d2 = 'AssExporter::visit(const ' + n.kind + '&)';
      if (n.kind === 'Text') {
        m.out += n.s; m.state = 'out += "' + n.s + '"';
        rec.step(3, 'accept 是虚函数，按元素类型选中 Text::accept；在里面 *this 的静态类型就是 const Text&，重载选中 visit(const Text&)，visit 又是虚函数，按访问者类型选中 AssExporter 的版本：原样输出文字。', { hl: hlOf(id) });
      } else if (n.kind === 'Br') {
        m.out += '\\N'; m.state = 'out += "\\N"';
        rec.step(2, 'Br::accept → AssExporter::visit(const Br&)：ASS 的换行写成 \\N。', { hl: hlOf(id) });
      } else {
        m.out += ASS[n.kind][0]; m.state = 'out += "' + ASS[n.kind][0] + '"';
        rec.step(4, n.kind + '::accept → AssExporter::visit(const ' + n.kind + '&)：先输出开标签，再由访问者自己调用 visitKids 访问子节点。', { hl: hlOf(id) });
        n.kids.forEach(visit);
        m.cur = id;
        m.d1 = '（' + n.kind + '::accept 还没返回）';
        m.d2 = 'AssExporter::visit(const ' + n.kind + '&)';
        m.out += ASS[n.kind][1]; m.state = 'out += "' + ASS[n.kind][1] + '"';
        rec.step(4, '子节点都访问完，回到 visit(const ' + n.kind + '&) 里，输出闭标签。遍历放在访问者里，才能在子节点之后做事。', { hl: hlOf(id, { d1: 'cmp' }) });
      }
    }
    TOP.forEach(visit);
    m.cur = null; m.d1 = ''; m.d2 = ''; m.state = '完成';
    rec.step(1, '整棵树走完：每个元素都是两次虚调用。元素类里没有一行 ASS 的代码，加一种导出格式只要再写一个访问者。', { hl: { vis: 'ok' }, note: '双分派：元素类型 × 访问者类型 → 一个具体的 visit' });
  }

  function countWalk(m, p, rec) {
    reset(m, 'CharCounter', '字数 =');
    m.state = 'n = 0'; m.out = '0';
    var n = 0;
    rec.step(1, '换一个访问者：CharCounter。遍历代码、元素类都和导出 ASS 时一模一样。', { hl: { n0: 'cur', vis: 'new' } });
    function visit(id) {
      var x = BY[id];
      m.cur = id;
      m.d1 = x.kind + '::accept';
      m.d2 = 'CharCounter::visit(const ' + x.kind + '&)';
      if (x.kind === 'Text') {
        n += Array.from(x.s).length; m.state = 'n += ' + Array.from(x.s).length; m.out = String(n);
        rec.step(5, '同一个 Text::accept，这次第二次分派选中的是 CharCounter::visit(const Text&)：数一下字数。', { hl: hlOf(id) });
      } else if (x.kind === 'Br') {
        m.state = '换行不算字';
        rec.step(5, 'CharCounter::visit(const Br&) 什么也不做。', { hl: hlOf(id) });
      } else {
        m.state = '只访问子节点';
        rec.step(6, 'CharCounter::visit(const ' + x.kind + '&)：样式不影响字数，只访问子节点。', { hl: hlOf(id) });
        x.kids.forEach(visit);
      }
    }
    TOP.forEach(visit);
    m.cur = null; m.d1 = ''; m.d2 = ''; m.state = 'n = ' + n;
    rec.step(1, '一共 ' + n + ' 个字。同一个 accept 配不同的访问者，做的是不同的事。', { hl: { vis: 'ok' } });
  }

  function overloadOnly(m, p, rec) {
    reset(m, 'AssExporter（只有重载）', 'out =');
    m.state = '三个重载：Text& / Br& / Node&';
    rec.step(7, '如果不用 accept，直接写 ex.visit(*n)，指望编译器按类型挑重载：', { hl: { n0: 'cur', vis: 'new' } });
    TOP.forEach(function (id) {
      m.cur = id;
      m.d1 = '没有虚调用：*n 是 Node&';
      m.d2 = 'visit(const Node&)（编译期定死）';
      m.out += '[?]';
      rec.step(8, '重载在编译期按静态类型选。*n 的静态类型永远是 Node&，所以不管运行时是 ' + BY[id].kind + ' 还是别的，都选中兜底的 visit(const Node&)。', { hl: hlOf(id, { d1: 'warn', d2: 'warn' }) });
    });
    m.cur = null;
    rec.step(7, '全部输出 [?]。运行时按类型选函数只能靠虚函数，accept 就是用一次虚调用「进到」元素类里，让 *this 的静态类型变成具体类型。', { hl: { vis: 'warn' }, note: '重载不是分派：它不看运行时的类型' });
  }

  DSViz.register('dp-visitor', {
    title: '双分派：accept 选元素类型，visit 选访问者类型',
    width: 740,
    height: 410,
    code: [
      'for (auto& n : cue.items) n->accept(v);',
      'void Br::accept(Visitor& v) const { v.visit(*this); }       // 每个元素类都一样',
      'void AssExporter::visit(const Text& t) { out += t.s; }',
      'void AssExporter::visit(const Bold& b) { out += "{\\b1}"; b.visitKids(*this); out += "{\\b0}"; }',
      'void CharCounter::visit(const Text& t) { n += utf8Len(t.s); }',
      'void CharCounter::visit(const Bold& b) { b.visitKids(*this); }',
      'for (auto& n : cue.items) ex.visit(*n);   // 只用重载',
      'void visit(const Node&) { out += "[?]"; }   // 兜底'
    ],
    init: function () { var m = {}; reset(m, 'AssExporter', 'out ='); return m; },
    view: view,
    start: function (m, rec) { assWalk(m, {}, rec); },
    actions: [{ label: '导出 ASS', run: assWalk }, { label: '统计字数', run: countWalk }, { label: '只用重载', run: overloadOnly }]
  });
})();
