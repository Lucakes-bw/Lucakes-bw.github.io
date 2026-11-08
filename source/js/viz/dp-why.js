/* 设计模式专栏《为什么需要设计模式》：同一个需求，if-else 版和多态版各要改哪些地方（dp-why） */
(function () {
  'use strict';
  var LX = 10, RX = 390, NAME_W = 118, CELL = 58, GAP = 4, ROW_H = 30, Y0 = 44, DY = 42;
  var OPS = ['header', 'row', 'footer'];
  var FMTS = ['csv', 'json'];

  function view(m, x) {
    var hl = (x && x.hl) || {}, boxes = [], texts = [];
    texts.push({ id: 'tl', x: LX, y: 22, text: 'if-else 版：每个操作一个函数，函数里按格式分支' });
    texts.push({ id: 'tr', x: RX, y: 22, text: '多态版：每种格式一个类，类里实现每个操作' });

    // 左边：每行是一个函数，格子是函数里的分支
    m.ops.forEach(function (op, r) {
      var y = Y0 + r * DY, fn = 'print' + op[0].toUpperCase() + op.slice(1);
      if (op === 'extension') fn = 'extension';
      boxes.push({ id: 'Lf' + op, x: LX, y: y, w: NAME_W, h: ROW_H, text: fn + '()', hl: hl['Lf' + op] });
      m.fmts.forEach(function (f, c) {
        var key = 'L' + op + f, empty = op === 'footer' && f === 'csv';
        boxes.push({ id: key, x: LX + NAME_W + GAP + c * (CELL + GAP), y: y, w: CELL, h: ROW_H, text: empty ? '' : f,
          hl: hl[key] || (empty ? 'empty' : null) });
      });
    });

    // 右边：每行是一个类，格子是类里的函数
    m.fmts.forEach(function (f, r) {
      var y = Y0 + r * DY, cls = f === 'csv' ? 'Csv' : f === 'json' ? 'Json' : 'Html';
      boxes.push({ id: 'Rc' + f, x: RX, y: y, w: NAME_W, h: ROW_H, text: cls + 'Formatter', hl: hl['Rc' + f] });
      m.ops.forEach(function (op, c) {
        var key = 'R' + f + op;
        boxes.push({ id: key, x: RX + NAME_W + GAP + c * (CELL + GAP), y: y, w: CELL, h: ROW_H, text: op === 'extension' ? 'ext' : op, hl: hl[key] });
      });
    });
    var ty = Y0 + 3 * DY + 16;
    boxes.push({ id: 'Rtable', x: RX, y: ty, w: NAME_W + GAP + 4 * (CELL + GAP) - GAP, h: ROW_H,
      text: 'makeFormatter 的表：' + m.fmts.join('、'), hl: hl.Rtable });
    if (m.ifaceExt) boxes.push({ id: 'Riface', x: RX, y: ty + 40, w: NAME_W + GAP + 4 * (CELL + GAP) - GAP, h: ROW_H,
      text: 'Formatter 接口：+ virtual extension() = 0', hl: hl.Riface });

    if (x && x.tally) {
      texts.push({ id: 'cl', x: LX, y: 232, text: x.tally[0], cls: 'muted' });
      texts.push({ id: 'cr', x: RX, y: m.ifaceExt ? 278 : 232, text: x.tally[1], cls: 'muted' });
    }
    return { kind: 'free', boxes: boxes, texts: texts };
  }

  function play(m, p, rec) {
    m.ops = OPS.slice(); m.fmts = FMTS.slice(); m.ifaceExt = false;
    rec.step(1, '两份代码功能完全相同：都支持 CSV 和 JSON，每种格式都要回答「开头写什么、每行写什么、结尾写什么」。左边按「操作」切分成三个函数，右边按「格式」切分成两个类。');
    rec.step(2, '新需求：再支持一种格式 HTML。');
    m.fmts.push('html');
    rec.step(2, '左边：printHeader 里加一个 html 分支，输出 <table> 和表头。', { hl: { Lheaderhtml: 'new', Lfheader: 'cur', Lrowhtml: 'dim', Lfooterhtml: 'dim', Rchtml: 'dim', Rhtmlheader: 'dim', Rhtmlrow: 'dim', Rhtmlfooter: 'dim' } });
    rec.step(2, '左边：printRow 里也要加一个 html 分支，输出 <tr><td>…', { hl: { Lheaderhtml: 'ok', Lrowhtml: 'new', Lfrow: 'cur', Lfooterhtml: 'dim', Rchtml: 'dim', Rhtmlheader: 'dim', Rhtmlrow: 'dim', Rhtmlfooter: 'dim' } });
    rec.step(2, '左边：printFooter 里还要再加一个，输出 </table>。这是最容易漏掉的一处：漏了以后程序照样编译、照样运行，只是 HTML 少了结尾标签。', { hl: { Lheaderhtml: 'ok', Lrowhtml: 'ok', Lfooterhtml: 'warn', Lffooter: 'cur', Rchtml: 'dim', Rhtmlheader: 'dim', Rhtmlrow: 'dim', Rhtmlfooter: 'dim' } });
    rec.step(2, '右边：新写一个 HtmlFormatter 类，和 HTML 有关的三段代码都在这一个类里。漏写 footer 的话，HtmlFormatter 仍是抽象类，创建它的那一行编译不过。', { hl: { Rchtml: 'new', Rhtmlheader: 'new', Rhtmlrow: 'new', Rhtmlfooter: 'new', Lheaderhtml: 'dim', Lrowhtml: 'dim', Lfooterhtml: 'dim' } });
    rec.step(2, '右边：在 makeFormatter 的表里加一行 {"html", ...}。', { hl: { Rtable: 'new' } });
    rec.step(2, '加一种格式：左边改了 3 个已有的函数，分散在三个地方；右边一个已有的类都没改，只是新增了代码。',
      { hl: { Lheaderhtml: 'warn', Lrowhtml: 'warn', Lfooterhtml: 'warn', Rchtml: 'ok', Rhtmlheader: 'ok', Rhtmlrow: 'ok', Rhtmlfooter: 'ok' }, tally: ['改动已有函数：3 个', '改动已有类：0 个（新增 1 个类 + 表里 1 行）'] });
    rec.step(3, '再来一个需求，方向不一样：每种格式都要提供自己的文件扩展名（.csv / .json / .html），这是一个新的「操作」。');
    m.ops.push('extension');
    rec.step(3, '左边：新写一个 extension 函数，三个分支都在里面。已有的函数一个都不用动。', { hl: { Lfextension: 'new', Lextensioncsv: 'new', Lextensionjson: 'new', Lextensionhtml: 'new' } });
    m.ifaceExt = true;
    rec.step(3, '右边：接口要加一个纯虚函数，三个已有的类各加一个实现，一共改 4 处。',
      { hl: { Riface: 'warn', Rcsvextension: 'warn', Rjsonextension: 'warn', Rhtmlextension: 'warn' } });
    rec.step(3, '加一个操作：左边改动 0 个已有函数，右边改动 4 个已有的类和接口。没有哪种写法对所有变化都方便，设计就是判断「哪个方向的变化更常见」，让那个方向的修改只需要新增代码。',
      { hl: { Lfextension: 'ok', Lextensioncsv: 'ok', Lextensionjson: 'ok', Lextensionhtml: 'ok', Riface: 'warn', Rcsvextension: 'warn', Rjsonextension: 'warn', Rhtmlextension: 'warn' },
        tally: ['改动已有函数：0 个（新增 1 个函数）', '改动已有类：3 个 + 接口'] });
  }

  DSViz.register('dp-why', {
    title: '同一个需求，两种写法各要改哪里',
    width: 760,
    height: 300,
    code: [
      '// 现有功能：CSV、JSON',
      '需求 1：再支持一种格式 HTML',
      '需求 2：每种格式提供文件扩展名 extension()'
    ],
    init: function () { return { ops: OPS.slice(), fmts: FMTS.slice(), ifaceExt: false }; },
    view: view,
    start: function (m, rec) { play(m, {}, rec); },
    actions: [{ label: '重新演示', run: play }]
  });
})();
