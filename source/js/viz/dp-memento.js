/* 设计模式专栏《备忘录》：ColorGrader（Originator）生成不透明 Snapshot（Memento）存入 History（Caretaker）的撤销/重做栈，以及 COW 共享 LUT（dp-memento） */
(function () {
  'use strict';

  var SX = [20, 385], SY = 176, SH = 32, SW = 325;

  function initState(m) {
    m.bri = 0; m.con = 100; m.lut = 'None'; m.rev = 0; m.dirty = false;
    m.undo = []; m.redo = [];
    m.lutRefs = null; // 仅在 COW 场景显示
  }

  var uid = 0;
  function makeSnap(label, m) {
    return {
      uid: ++uid, label: label,
      bri: m.bri, con: m.con, lut: m.lut, rev: m.rev
    };
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];

    texts.push({ id: 'to', x: 20, y: 20, text: 'ColorGrader（Originator，唯一能读写 Snapshot 私有字段）', cls: 'muted' });
    boxes.push({
      id: 'grader', x: 20, y: 32, w: m.lutRefs ? 430 : 690, h: 66,
      text: 'bri=' + (m.bri >= 0 ? '+' : '') + m.bri + '  con=' + m.con + '  lut=' + m.lut,
      sub: '私有内部状态：rev=' + m.rev + '  cacheDirty=' + (m.dirty ? 'true（下帧重绘）' : 'false（缓存同步）'),
      hl: hl.grader
    });

    if (m.lutRefs) {
      texts.push({ id: 'tl', x: 470, y: 20, text: '共享不可变 256 KB LUT', cls: 'muted' });
      boxes.push({
        id: 'lutbox', x: 470, y: 32, w: 240, h: 66,
        text: m.lutRefs.name + '（256 KB）',
        sub: 'shared_ptr<const> use_count = ' + m.lutRefs.refs,
        hl: hl.lutbox
      });
    }

    texts.push({ id: 'tu', x: SX[0], y: 156, text: 'History::undoStack_（Caretaker，只见 label）', cls: 'muted' });
    texts.push({ id: 'tr', x: SX[1], y: 156, text: 'History::redoStack_（重做栈）', cls: 'muted' });

    [m.undo, m.redo].forEach(function (stack, k) {
      if (!stack.length) {
        boxes.push({ id: 'empty' + k, x: SX[k], y: SY, w: SW, h: SH, text: '（空）', hl: hl['empty' + k] });
      }
      stack.forEach(function (s, i) {
        boxes.push({
          id: 's' + s.uid, x: SX[k], y: SY + (stack.length - 1 - i) * (SH + 6), w: SW, h: SH,
          text: '[' + s.label + ']  🔒 bri=' + (s.bri >= 0 ? '+' : '') + s.bri + ' ' + s.lut + ' r' + s.rev,
          hl: hl['s' + s.uid]
        });
      });
    });

    if (x.arrow) arrows.push(x.arrow);
    if (x.note) texts.push({ id: 'note', x: 20, y: 344, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function basic(m, p, rec) {
    initState(m);
    rec.step(1, '初始状态：ColorGrader 已渲染一帧（cacheDirty=false），History 两个栈都为空。', { hl: { grader: 'cur' } });

    var s0 = makeSnap('初始', m);
    m.undo.push(s0);
    m.bri = 20; m.lut = 'TealOrange'; m.rev = 2; m.dirty = false;
    rec.step(2, '修改前先 backup(g.save("初始"))：ColorGrader 生成不透明 Snapshot 压入 undoStack_，再调色为 bri=+20, TealOrange。', {
      hl: { grader: 'cmp', ['s' + s0.uid]: 'new' },
      arrow: { from: 'grader', to: 's' + s0.uid, hl: 'new', label: 'save()' }
    });

    var s1 = makeSnap('调色 1', m);
    m.undo.push(s1);
    m.bri = -40; m.lut = 'Kodak2383'; m.rev = 4; m.dirty = false;
    rec.step(2, '再次存档 backup(g.save("调色 1"))，然后调色为 bri=-40, Kodak2383 并渲染（rev=4, cacheDirty=false）。', {
      hl: { grader: 'cmp', ['s' + s1.uid]: 'new' },
      arrow: { from: 'grader', to: 's' + s1.uid, hl: 'new', label: 'save()' }
    });

    var r1 = makeSnap('重做点', m);
    m.redo.push(r1);
    var top1 = m.undo.pop();
    m.bri = top1.bri; m.con = top1.con; m.lut = top1.lut; m.rev = top1.rev; m.dirty = true;
    rec.step(5, 'Ctrl+Z（undo）：先把当前状态存进 redoStack_，再把 undoStack_ 栈顶传给 g.restore()——ColorGrader 通过友元恢复私有字段，并自动把 cacheDirty 置 true！', {
      hl: { grader: 'ok', ['s' + r1.uid]: 'cur' },
      note: 'History 从头到尾只搬运 Snapshot，既看不到也改不了里面的 brightness_ 和 cacheDirty_'
    });

    var r2 = makeSnap('重做点', m);
    m.redo.push(r2);
    var top2 = m.undo.pop();
    m.bri = top2.bri; m.con = top2.con; m.lut = top2.lut; m.rev = top2.rev; m.dirty = true;
    rec.step(5, '再撤销一次：回到初始状态（bri=0, None, rev=0, cacheDirty=true），undoStack_ 为空。', {
      hl: { grader: 'ok', ['s' + r2.uid]: 'cur' }
    });

    var u1 = makeSnap('撤销点', m);
    m.undo.push(u1);
    var rt = m.redo.pop();
    m.bri = rt.bri; m.con = rt.con; m.lut = rt.lut; m.rev = rt.rev; m.dirty = true;
    rec.step(6, 'Ctrl+Shift+Z（redo）：从 redoStack_ 弹出快照恢复到调色 1，同时把撤销前的状态压回 undoStack_。', {
      hl: { grader: 'ok', ['s' + u1.uid]: 'new' }
    });
  }

  function branch(m, p, rec) {
    initState(m);
    var s0 = makeSnap('初始', m); m.undo.push(s0);
    m.bri = 20; m.lut = 'TealOrange'; m.rev = 2;
    var s1 = makeSnap('调色 1', m); m.undo.push(s1);
    m.bri = -40; m.lut = 'Kodak2383'; m.rev = 4;
    var r1 = makeSnap('重做点', m); m.redo.push(r1);
    m.undo.pop();
    m.bri = 20; m.lut = 'TealOrange'; m.rev = 2; m.dirty = true;
    rec.step(5, '刚撤销一次，当前处于「调色 1」，redoStack_ 里还躺着刚才被撤销的 Kodak2383。', {
      hl: { grader: 'ok', ['s' + r1.uid]: 'cur' }
    });

    var sNew = makeSnap('调色 1', m);
    m.undo.push(sNew);
    m.redo = [];
    m.bri = 35; m.con = 115; m.rev = 3; m.dirty = true;
    rec.step(3, '用户在撤销后做了新编辑（bri=+35, con=115）：backup() 压入新快照并立刻清空 redoStack_，旧时间线作废。', {
      hl: { grader: 'cmp', ['s' + sNew.uid]: 'new', empty1: 'warn' },
      note: '线性历史模型下，撤销后再编辑必须清空重做栈'
    });
  }

  function cowDemo(m, p, rec) {
    initState(m);
    m.lut = 'TealOrange'; m.rev = 1;
    m.lutRefs = { name: 'TealOrange', refs: 1 };
    rec.step(7, '加载 256 KB 的 TealOrange LUT（std::shared_ptr<const LutTable>），当前引用计数 use_count = 1。', {
      hl: { grader: 'cur', lutbox: 'cur' }
    });

    var s0 = makeSnap('加载 LUT', m); m.undo.push(s0); m.lutRefs.refs = 2;
    m.bri = 10; m.rev = 2; m.dirty = true;
    rec.step(7, '存档并调亮度 +10：Snapshot 只拷贝 shared_ptr 指针，256 KB 数据零拷贝，use_count 升到 2。', {
      hl: { grader: 'cmp', lutbox: 'ok', ['s' + s0.uid]: 'new' }
    });

    var s1 = makeSnap('亮度 +10', m); m.undo.push(s1); m.lutRefs.refs = 3;
    m.con = 120; m.rev = 3;
    rec.step(7, '再存档并调对比度 120：3 个持有者共享同一块 256 KB 内存，单次 save() 实测仅 3.6 ns。', {
      hl: { grader: 'cmp', lutbox: 'ok', ['s' + s1.uid]: 'new' }
    });

    m.lut = 'Kodak2383'; m.rev = 4;
    m.lutRefs = { name: 'Kodak2383', refs: 1 };
    rec.step(8, '切换到新 LUT（Kodak2383）：ColorGrader 换上新指针（use_count=1），历史栈里旧快照依然安全指向 TealOrange。', {
      hl: { grader: 'new', lutbox: 'new' },
      note: '1000 次调色实测：整份深拷贝 250.06 MB，COW 共享仅 2.53 MB（降至 1.0%）'
    });
  }

  function bounded(m, p, rec) {
    initState(m);
    var s0 = makeSnap('v0 (上限2)', m); m.undo.push(s0); m.bri = 10; m.rev = 1;
    var s1 = makeSnap('v1 (上限2)', m); m.undo.push(s1); m.bri = 20; m.rev = 2;
    rec.step(2, '设 History 最大深度 maxDepth = 2（底层用 std::deque），当前已存满 2 个快照 [v0, v1]。', {
      hl: { ['s' + s0.uid]: 'cur', ['s' + s1.uid]: 'new' }
    });

    m.undo.shift();
    var s2 = makeSnap('v2 (上限2)', m); m.undo.push(s2); m.bri = 30; m.rev = 3;
    rec.step(2, '压入第 3 个快照 v2：队列已满，先 pop_front() 淘汰最老的 v0，再压入 v2，内存严格受控。', {
      hl: { grader: 'cmp', ['s' + s2.uid]: 'new' },
      note: 'std::deque 支持 O(1) 头删尾插，配合 maxDepth 彻底防止内存无限增长'
    });
  }

  DSViz.register('dp-memento', {
    title: '备忘录：不透明 Snapshot 在 Originator 与 History 之间流转',
    width: 730,
    height: 360,
    code: [
      'Snapshot ColorGrader::save(string label) const { return Snapshot(label, bri_, con_, lut_, rev_); }',
      'void History::backup(Snapshot s) { undo_.push_back(std::move(s)); redo_.clear(); }',
      '  // 若撤销后产生新编辑，redo_.clear() 让旧重做分支失效',
      'void ColorGrader::restore(const Snapshot& s) { bri_ = s.bri_; ...; cacheDirty_ = true; }',
      'bool History::undo(ColorGrader& g) { redo_.push_back(g.save("重做点")); g.restore(undo_.back()); undo_.pop_back(); }',
      'bool History::redo(ColorGrader& g) { undo_.push_back(g.save("撤销点")); g.restore(redo_.back()); redo_.pop_back(); }',
      'std::shared_ptr<const LutTable> lut_;   // COW：调参存档只增引用计数，不拷贝 256 KB LUT',
      'void setLut(std::shared_ptr<const LutTable> p) { lut_ = std::move(p); }  // 切 LUT 换新指针'
    ],
    init: function () { var m = {}; initState(m); return m; },
    view: view,
    start: function (m, rec) { basic(m, {}, rec); },
    actions: [
      { label: '存档、撤销与重做', run: basic },
      { label: '撤销后新编辑', run: branch },
      { label: 'COW 共享 256 KB LUT', run: cowDemo },
      { label: '有界栈（上限 2）', run: bounded }
    ]
  });
})();
