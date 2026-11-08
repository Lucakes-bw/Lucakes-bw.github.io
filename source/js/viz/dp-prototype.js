/* 设计模式专栏《原型》：浅拷贝和深拷贝——图层是共用的还是各有一份（dp-prototype） */
(function () {
  'use strict';

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'ts', x: 20, y: 22, text: '栈上的 Document 对象', cls: 'muted' });
    texts.push({ id: 'th', x: 460, y: 22, text: '堆上的 Layer 对象', cls: 'muted' });
    boxes.push({ id: 'tpl', x: 20, y: 40, w: 260, h: 44, text: '《海报模板》 layers[0] →', hl: hl.tpl });
    boxes.push({ id: 'L1', x: 460, y: 40, w: 240, h: 44, text: '背景：' + m.c1, sub: 'use_count = ' + m.u1, hl: hl.L1 });
    arrows.push({ from: 'tpl', to: 'L1', hl: hl.a1 });
    if (m.copy) {
      boxes.push({ id: 'cp', x: 20, y: 140, w: 260, h: 44, text: '《活动海报》 layers[0] →', hl: hl.cp });
      if (m.deep) {
        boxes.push({ id: 'L2', x: 460, y: 140, w: 240, h: 44, text: '背景：' + m.c2, sub: 'use_count = 1', hl: hl.L2 });
        arrows.push({ from: 'cp', to: 'L2', hl: hl.a2 });
      } else {
        arrows.push({ from: 'cp', to: 'L1', hl: hl.a2 });
      }
    }
    if (x.note) texts.push({ id: 'note', x: 20, y: 222, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m) { m.c1 = '白色'; m.c2 = '白色'; m.u1 = 1; m.copy = false; m.deep = false; }

  function shallow(m, p, rec) {
    reset(m);
    rec.step(1, '模板文档有一个背景图层，用 shared_ptr 保存在堆上。');
    m.copy = true; m.u1 = 2;
    rec.step(2, '浅拷贝：Document 默认的拷贝构造函数逐个复制成员。layers 里存的是 shared_ptr，复制的只是指针，两个文档指向同一个图层，use_count 变成 2。', { hl: { cp: 'new', a2: 'cur' } });
    m.c1 = '红色';
    rec.step(4, '在新文档里把背景改成红色。改的是那个共用的图层……', { hl: { cp: 'cur', a2: 'cur', L1: 'cmp' } });
    rec.step(5, '……于是模板的背景也变成了红色。以后再用这个模板新建文档，背景都是红的。', { hl: { tpl: 'warn', a1: 'cur', L1: 'warn' }, note: '浅拷贝：只复制指针，被指向的对象还是同一个' });
  }

  function deep(m, p, rec) {
    reset(m);
    rec.step(1, '同样从这个模板出发。');
    m.copy = true; m.deep = true;
    rec.step(3, '深拷贝：deepCopy() 对每个图层调用 make_shared<Layer>(*l)，在堆上复制出一个新的图层，新文档指向自己的那一份。', { hl: { cp: 'new', L2: 'new', a2: 'cur' } });
    m.c2 = '红色';
    rec.step(4, '在新文档里把背景改成红色，改的是新文档自己的图层。', { hl: { cp: 'cur', a2: 'cur', L2: 'cmp' } });
    rec.step(5, '模板的背景仍然是白色。原型模式里，clone() 通常都应该是深拷贝：克隆出来的对象应该能独立修改。', { hl: { tpl: 'ok', L1: 'ok', L2: 'ok' }, note: '深拷贝：被指向的对象也复制一份' });
  }

  DSViz.register('dp-prototype', {
    title: '浅拷贝 vs 深拷贝：改了副本，原件会不会变',
    width: 740,
    height: 236,
    code: [
      'Document poster{"海报模板", {make_shared<Layer>("背景", "白色")}};',
      'Document a = poster.shallowCopy();     // return *this;',
      'Document b = poster.deepCopy();        // 每个图层 make_shared<Layer>(*l)',
      'copy.layers[0]->color = "红色";',
      'show(poster);'
    ],
    init: function () { var m = {}; reset(m); return m; },
    view: view,
    start: function (m, rec) { shallow(m, {}, rec); },
    actions: [{ label: '浅拷贝', run: shallow }, { label: '深拷贝', run: deep }]
  });
})();
