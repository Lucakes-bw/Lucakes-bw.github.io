/* 设计模式专栏《SOLID 原则（下）》的两个演示：
 *   dp-isp：改一个接口，哪些源文件要重新编译（数据来自 projects/dp-lab/04-solid2/isp/measure.sh 的实测）
 *   dp-dip：依赖倒置前后，源码依赖的方向和运行时调用的方向 */
(function () {
  'use strict';

  /* ---------------- dp-isp ---------------- */
  var FILES = ['main', 'office_machine', 'simple_printer', 'print_invoice', 'print_report', 'print_label', 'print_receipt', 'scan_contract', 'fax_order'];
  // 每个源文件包含了哪些接口头文件（拆分版）
  var SPLIT_USES = {
    main: ['printer.h', 'scanner.h', 'fax.h'], office_machine: ['printer.h', 'scanner.h', 'fax.h'], simple_printer: ['printer.h'],
    print_invoice: ['printer.h'], print_report: ['printer.h'], print_label: ['printer.h'], print_receipt: ['printer.h'],
    scan_contract: ['scanner.h'], fax_order: ['fax.h']
  };
  var FW = 150, FH = 26, COLX = [10, 400];

  function ispView(m, x) {
    x = x || {};
    var boxes = [], texts = [], hl = x.hl || {};
    texts.push({ id: 'tf', x: COLX[0], y: 20, text: '胖接口：所有源文件都包含 device.h' });
    texts.push({ id: 'ts', x: COLX[1], y: 20, text: '小接口：每个源文件只包含用得到的' });
    boxes.push({ id: 'Hdevice', x: COLX[0], y: 34, w: 330, h: 26, text: 'device.h：print + scan + fax', hl: hl.Hdevice });
    ['printer.h', 'scanner.h', 'fax.h'].forEach(function (h, i) {
      boxes.push({ id: 'H' + h, x: COLX[1] + i * 112, y: 34, w: 106, h: 26, text: h, hl: hl['H' + h] });
    });
    FILES.forEach(function (f, i) {
      var c = i % 2, r = Math.floor(i / 2), y = 76 + r * (FH + 6);
      boxes.push({ id: 'F' + f, x: COLX[0] + c * (FW + 30), y: y, w: FW + 20, h: FH, text: f + '.cpp', hl: hl['F' + f] });
      boxes.push({ id: 'S' + f, x: COLX[1] + c * (FW + 30), y: y, w: FW + 20, h: FH, text: f + '.cpp', sub: '', hl: hl['S' + f] });
    });
    if (x.tally) {
      texts.push({ id: 'cf', x: COLX[0], y: 252, text: x.tally[0], cls: 'muted' });
      texts.push({ id: 'cs', x: COLX[1], y: 252, text: x.tally[1], cls: 'muted' });
    }
    return { kind: 'free', boxes: boxes, texts: texts };
  }

  function ispPlay(m, p, rec) {
    rec.step(1, '两个项目功能完全相同：9 个源文件，其中 4 个只用到打印，1 个只用到扫描，1 个只用到传真。');
    rec.step(2, '需求变化：传真要支持重试次数，fax() 多一个参数 int retries。先看胖接口的项目：device.h 改了。', { hl: { Hdevice: 'cur' } });
    var hl = { Hdevice: 'cur' };
    FILES.forEach(function (f) { hl['F' + f] = 'warn'; });
    rec.step(3, '所有包含 device.h 的源文件都要重新编译：9 个全部重编，连 4 个只会打印的、1 个只会扫描的也逃不掉。', { hl: hl, tally: ['重新编译 9 / 9 个源文件', ''] });
    var hl2 = Object.assign({}, hl, { 'Hfax.h': 'cur' });
    FILES.forEach(function (f) { if (SPLIT_USES[f].indexOf('fax.h') >= 0) hl2['S' + f] = 'warn'; else hl2['S' + f] = null; });
    rec.step(4, '小接口的项目：只有 fax.h 改了。只有包含 fax.h 的 3 个源文件（main、office_machine、fax_order）重新编译，打印和扫描的 6 个源文件不受影响。',
      { hl: hl2, tally: ['重新编译 9 / 9 个源文件', '重新编译 3 / 9 个源文件'] });
  }

  DSViz.register('dp-isp', {
    title: '给 fax() 加一个参数：哪些源文件要重新编译',
    width: 760,
    height: 264,
    code: [
      '// 两个项目：9 个源文件',
      'virtual void fax(const Doc& d, const std::string& number, int retries) = 0;',
      '// 胖接口：device.h 被所有源文件包含',
      '// 小接口：fax.h 只被 3 个源文件包含'
    ],
    init: function () { return {}; },
    view: ispView,
    start: function (m, rec) { ispPlay(m, {}, rec); },
    actions: [{ label: '重新演示', run: ispPlay }]
  });

  /* ---------------- dp-dip ---------------- */
  function dipView(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    boxes.push({ id: 'zbiz', x: 10, y: 30, w: m.inverted ? 478 : 220, h: 170, text: '', hl: 'empty' });
    boxes.push({ id: 'zlow', x: 520, y: 30, w: 230, h: 170, text: '', hl: 'empty' });
    texts.push({ id: 'lbiz', x: 20, y: 22, text: '高层：业务规则', cls: 'muted' });
    texts.push({ id: 'llow', x: 530, y: 22, text: '低层：存储细节', cls: 'muted' });
    boxes.push({ id: 'svc', x: 30, y: 60, w: 170, h: 40, text: 'OrderService', hl: hl.svc });
    boxes.push({ id: 'file', x: 540, y: 60, w: 190, h: 40, text: 'FileOrderStore', hl: hl.file });
    if (m.inverted) {
      boxes.push({ id: 'iface', x: 268, y: 60, w: 196, h: 40, text: '«interface» OrderStore', hl: hl.iface });
      arrows.push({ from: 'svc', to: 'iface', label: '依赖', hl: hl.a1 });
      arrows.push({ from: 'file', to: 'iface', label: '实现', hl: hl.a2 });
      if (m.mem) {
        boxes.push({ id: 'mem', x: 540, y: 140, w: 190, h: 40, text: 'InMemoryOrderStore', hl: hl.mem });
        arrows.push({ from: 'mem', to: 'iface', label: '实现', hl: hl.a3 });
      }
    } else {
      arrows.push({ from: 'svc', to: 'file', label: '#include + 直接创建', hl: hl.a0 });
    }
    if (x.call) arrows.push({ from: 'svc', to: x.call, label: '', hl: 'new', curve: 70, pkt: 'save()' });
    if (x.note) texts.push({ id: 'note', x: 10, y: 226, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function dipPlay(m, p, rec) {
    m.inverted = false; m.mem = false;
    rec.step(1, '改造前：OrderService 直接 #include 并创建 FileOrderStore。源码依赖的方向是「高层 → 低层」。', { hl: { a0: 'cur' } });
    rec.step(2, '运行时，下单的调用也是从 OrderService 流向 FileOrderStore。依赖方向和调用方向相同。', { call: 'file', note: '想测试下单规则，就只能真的写文件；换成数据库，就要改 OrderService' });
    m.inverted = true;
    rec.step(3, '改造：在高层定义一个接口 OrderStore，写明业务需要什么（save）。OrderService 只依赖这个接口。', { hl: { iface: 'new', a1: 'cur' } });
    rec.step(4, 'FileOrderStore 实现这个接口。注意箭头：现在是低层依赖高层定义的接口，源码依赖的方向倒过来了，这就是「依赖倒置」。', { hl: { a2: 'cur', file: 'cur' } });
    rec.step(5, '运行时，调用仍然从 OrderService 流向 FileOrderStore：调用方向没变，变的只是源码依赖的方向。', { call: 'file', hl: { a1: 'dim', a2: 'dim' } });
    m.mem = true;
    rec.step(6, '测试时，传进去一个 InMemoryOrderStore。OrderService 一行都不用改，测试不碰磁盘：1000 个测试从几十毫秒变成 0.03 毫秒。', { call: 'mem', hl: { mem: 'new', a3: 'cur' }, note: '谁来决定用哪个实现？main（或测试代码）创建具体对象，再传给 OrderService' });
  }

  DSViz.register('dp-dip', {
    title: '依赖倒置：源码依赖倒过来，调用方向不变',
    width: 760,
    height: 240,
    code: [
      'class OrderService { FileOrderStore store_; ... };        // 改造前',
      'store_.save(order);',
      'class OrderStore { virtual void save(const Order&) = 0; };  // 高层定义的接口',
      'class FileOrderStore : public OrderStore { ... };',
      'explicit OrderService(OrderStore& store);                  // 依赖从外面注入',
      'InMemoryOrderStore fake; OrderService svc(fake);           // 测试'
    ],
    init: function () { return { inverted: false, mem: false }; },
    view: dipView,
    start: function (m, rec) { dipPlay(m, {}, rec); },
    actions: [{ label: '重新演示', run: dipPlay }]
  });
})();
