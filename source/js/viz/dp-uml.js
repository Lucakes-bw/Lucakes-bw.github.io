/* 设计模式专栏《UML 类图》：读一段 C++ 代码，一步步画出它的类图（dp-uml，需要 front-matter 写 uml_viz: true） */
(function () {
  'use strict';

  var POS = {
    Logger:      { x: 10,  y: 50,  w: 160, ops: ['+ info(msg: string)'] },
    Player:      { x: 270, y: 20,  w: 270 },
    Decoder:     { x: 600, y: 20,  w: 200, ops: ['+ decode(i: int) : Frame'] },
    Renderer:    { x: 285, y: 215, w: 240, stereo: 'interface', ops: ['*+ draw(f: const Frame&) : void'] },
    SdlRenderer: { x: 10,  y: 215, w: 236, ops: ['+ draw(f: const Frame&) : void'] },
    Frame:       { x: 620, y: 210, w: 150, attrs: ['+ pts : int64_t'] }
  };
  var RELS = {
    realize:  { from: 'SdlRenderer', to: 'Renderer', type: 'realize', tdx: -7 },
    useLog:   { from: 'Player', to: 'Logger', type: 'depend', fdx: -8, label: '使用' },
    decoder:  { from: 'Player', to: 'Decoder', type: 'compose', fdx: -38, tm: '1' },
    renderer: { from: 'Player', to: 'Renderer', type: 'aggregate', tm: '1' },
    queue:    { from: 'Player', to: 'Frame', type: 'compose', fdx: 30, tm: '*' },
    retFrame: { from: 'Decoder', to: 'Frame', type: 'depend', tdx: 5, label: '返回' }
  };

  function view(m, x) {
    var hl = (x && x.hl) || {}, classes = [], rels = [];
    m.classes.forEach(function (id) {
      var c = Object.assign({ id: id }, POS[id]);
      if (id === 'Player') { c.attrs = m.attrs.slice(); c.ops = m.ops.slice(); }
      c.hl = hl[id] || (x && x.dim ? 'dim' : null);
      classes.push(c);
    });
    m.rels.forEach(function (k) {
      rels.push(Object.assign({ key: k, hl: hl[k] || (x && x.dim ? 'dim' : null) }, RELS[k]));
    });
    return { kind: 'uml', classes: classes, rels: rels };
  }

  function draw(m, p, rec) {
    m.classes = []; m.rels = []; m.attrs = []; m.ops = [];
    rec.step(1, '从一段播放器代码画出类图。先看第一个类 Renderer。');
    m.classes.push('Renderer');
    rec.step(3, 'Renderer 只有纯虚函数（外加一个虚析构函数），不能创建对象，只规定「能做什么」，这就是接口。类名上方写 «interface»，纯虚函数用斜体。', { hl: { Renderer: 'cur' } });
    m.classes.push('SdlRenderer'); m.rels.push('realize');
    rec.step(5, 'SdlRenderer 以 public 方式继承接口并实现了 draw：实现关系，画虚线加空心三角，三角指向接口。', { hl: { SdlRenderer: 'new', realize: 'cur' } });
    m.classes.push('Decoder');
    rec.step(8, 'Decoder 是一个普通的类，有一个公有操作 decode，参数写成「名字: 类型」，返回值写在最后的冒号后面。', { hl: { Decoder: 'new' } });
    m.classes.push('Player');
    rec.step(10, '主角 Player。先画一个空的框，属性栏和操作栏随后一行一行填进去。', { hl: { Player: 'new' } });
    m.ops.push('+ Player(r: shared_ptr<Renderer>)', '+ play(url: string, log: Logger&)');
    rec.step(13, '两个公有成员函数写进操作栏，前面的 + 表示 public。', { hl: { Player: 'cur' } });
    m.classes.push('Logger'); m.rels.push('useLog');
    rec.step(13, 'Logger 只出现在 play 的参数里：函数里用一下，用完就不管了，Player 并不保存它。这是依赖关系，画虚线箭头，指向被依赖的 Logger。', { hl: { Logger: 'new', useLog: 'cur' } });
    m.attrs.push('- decoder_ : unique_ptr<Decoder>'); m.rels.push('decoder');
    rec.step(15, 'decoder_ 是 unique_ptr，由 Player 在构造函数里自己创建、独占，Player 销毁时 Decoder 也跟着销毁。这是组合关系：实线，实心菱形画在整体 Player 一端。', { hl: { Player: 'cur', decoder: 'cur' } });
    m.attrs.push('- renderer_ : shared_ptr<Renderer>'); m.rels.push('renderer');
    rec.step(16, 'renderer_ 是 shared_ptr，从构造函数的参数传进来：外面的代码也持有它，Player 销毁后它还活着。这是聚合关系：空心菱形。', { hl: { Player: 'cur', renderer: 'cur' } });
    m.attrs.push('- queue_ : vector<Frame>'); m.classes.push('Frame'); m.rels.push('queue');
    rec.step(17, 'queue_ 是 vector<Frame>，Frame 按值存在 Player 里，也是组合；数量是 0 到多个，在 Frame 一端标上多重性 *。', { hl: { Player: 'cur', Frame: 'new', queue: 'cur' } });
    m.rels.push('retFrame');
    rec.step(8, '回头看 decode：它返回一个 Frame，但 Decoder 并不保存 Frame。只是用到，所以是依赖。', { hl: { Decoder: 'cur', retFrame: 'cur' } });
    rec.step(18, '画完了。读图的顺序：先找三角（继承体系），再找菱形（谁拥有谁），最后看虚线箭头（谁用到谁）。');
  }

  DSViz.register('dp-uml', {
    title: '从代码画出类图',
    width: 820,
    height: 300,
    code: [
      'class Renderer {                      // 接口',
      'public:',
      '    virtual void draw(const Frame& f) = 0;',
      '};',
      'class SdlRenderer : public Renderer { /* 重写 draw */ };',
      'class Decoder {',
      'public:',
      '    Frame decode(int i);',
      '};',
      'class Player {',
      'public:',
      '    explicit Player(std::shared_ptr<Renderer> r);',
      '    void play(const std::string& url, Logger& log);',
      'private:',
      '    std::unique_ptr<Decoder> decoder_;',
      '    std::shared_ptr<Renderer> renderer_;',
      '    std::vector<Frame> queue_;',
      '};'
    ],
    init: function () { return { classes: [], rels: [], attrs: [], ops: [] }; },
    view: view,
    start: function (m, rec) { draw(m, {}, rec); },
    actions: [{ label: '重新画一遍', run: draw }]
  });
})();
