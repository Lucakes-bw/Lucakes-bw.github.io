/* C 语言《函数》：按值传递（c-swap）和递归的调用栈（c-factorial）
   栈画成从上往下排列的栈帧：上面是高地址（先调用的），下面是低地址（后调用的） */
(function () {
  'use strict';
  var X0 = 150, FW = 420, ROW = 40;

  // frames: [{ id, name, addr, vars: [{ k, v, hl }], hl, note }]
  function view(m, x) {
    var boxes = [], texts = [], arrows = [], y = 34;
    texts.push({ id: 'hi', x: 20, y: 22, text: '高地址', cls: 'muted' });
    m.frames.forEach(function (f, i) {
      var h = 44, fw = m.fw || FW, off = f.off || 0;
      boxes.push({ id: 'f' + f.id, x: X0, y: y, w: fw, h: h, text: '', hl: f.hl });
      texts.push({ id: 'n' + f.id, x: X0 - 12, y: y + 20, text: f.name, anchor: 'end' });
      if (f.addr) texts.push({ id: 'a' + f.id, x: X0 - 12, y: y + 36, text: f.addr, anchor: 'end', cls: 'muted' });
      f.vars.forEach(function (v, j) {
        boxes.push({ id: 'v' + f.id + v.k, x: X0 + 12 + off + j * 134, y: y + 6, w: 122, h: 32, text: v.k + ' = ' + v.v, hl: v.hl });
      });
      if (f.note) texts.push({ id: 'o' + f.id, x: X0 + fw + 12, y: y + 27, text: f.note, cls: f.noteCls });
      y += h + 26;
    });
    texts.push({ id: 'lo', x: 20, y: Math.max(y + 8, 150), text: '低地址 ↓ 栈的增长方向', cls: 'muted' });
    (x.copy || []).forEach(function (c) { arrows.push({ from: c[0], to: c[1], hl: 'new', label: '复制' }); });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function frame(m, id) { return m.frames.filter(function (f) { return f.id === id; })[0]; }
  function setVar(m, id, k, v, hl) {
    m.frames.forEach(function (f) { f.vars.forEach(function (x) { x.hl = null; }); });
    var f = frame(m, id);
    f.vars.forEach(function (x) { if (x.k === k) { x.v = v; x.hl = hl || 'cur'; } });
  }

  DSViz.register('c-swap', {
    title: '按值传递：swap(x, y) 交换的是副本',
    height: 200,
    code: [
      'void swap(int a, int b)',
      '{',
      '    int t = a;',
      '    a = b;',
      '    b = t;',
      '}',
      '',
      'int main(void)',
      '{',
      '    int x = 1, y = 2;',
      '    swap(x, y);',
      '    printf("x = %d, y = %d\\n", x, y);',
      '}'
    ],
    init: function () { return { frames: [], fw: 560 }; },
    view: view,
    start: function (m, rec) {
      m.frames = [{ id: 'main', name: 'main', vars: [{ k: 'x', v: 1 }, { k: 'y', v: 2 }] }];
      rec.step(10, 'main 的栈帧中有两个局部变量 x = 1、y = 2。');
      m.frames[0].hl = 'dim';
      m.frames.push({ id: 'swap', name: 'swap', hl: 'cur', off: 150, vars: [{ k: 'a', v: 1, hl: 'new' }, { k: 'b', v: 2, hl: 'new' }, { k: 't', v: '?' }] });
      rec.step(11, '调用 swap(x, y)：在栈上为 swap 分配一个新的栈帧，把 x、y 的值复制给形参 a、b。a、b 和 x、y 是不同的内存。',
        { copy: [['vmainx', 'vswapa'], ['vmainy', 'vswapb']] });
      setVar(m, 'swap', 't', 1);
      rec.step(3, 't = a：t 变成 1。');
      setVar(m, 'swap', 'a', 2);
      rec.step(4, 'a = b：a 变成 2。');
      setVar(m, 'swap', 'b', 1);
      rec.step(5, 'b = t：b 变成 1。swap 的 a、b 确实交换了，可 main 的 x、y 一点都没有动。');
      m.frames.pop();
      m.frames[0].hl = 'cur';
      m.frames[0].vars.forEach(function (v) { v.hl = 'warn'; });
      rec.step(12, 'swap 返回，它的栈帧被释放，a、b、t 随之消失。x 仍然是 1，y 仍然是 2。要修改调用者的变量，就要把它们的地址传进去（第 1.8 篇《指针基础》）。');
    }
  });

  // 地址来自正文中实测的输出，每层相差 0x60 = 96 字节
  var ADDR = { 4: '0x16d5d6548', 3: '0x16d5d64e8', 2: '0x16d5d6488', 1: '0x16d5d6428' };

  DSViz.register('c-factorial', {
    title: '递归的调用栈：factorial(4)',
    height: 400,
    code: [
      'long long factorial(int n)',
      '{',
      '    int local = n;',
      '    long long result = (n <= 1) ? 1',
      '                     : n * factorial(n - 1);',
      '    return result;',
      '}',
      '',
      'int main(void) { factorial(4); }'
    ],
    init: function () { return { frames: [] }; },
    view: view,
    start: function (m, rec) {
      m.frames = [{ id: 'm', name: 'main', vars: [] }];
      rec.step(9, 'main 调用 factorial(4)。', { vars: { '栈帧数': 1 } });
      var n;
      for (n = 4; n >= 1; n--) {
        m.frames.forEach(function (f) { f.hl = 'dim'; f.vars.forEach(function (v) { v.hl = null; }); });
        m.frames.push({ id: 'f' + n, name: 'factorial(' + n + ')', addr: ADDR[n], hl: 'cur', vars: [{ k: 'n', v: n, hl: 'new' }, { k: 'local', v: n, hl: 'new' }, { k: 'result', v: '?' }] });
        rec.step(3, '进入 factorial(' + n + ')，栈上多了一个栈帧，它有自己的 n、local、result。local 的地址是 ' + ADDR[n] +
          (n < 4 ? '，比上一层低 0x60 = 96 字节：栈向低地址增长。' : '。'), { vars: { '栈帧数': m.frames.length } });
        if (n > 1) rec.step(5, 'n = ' + n + ' > 1，要先算出 factorial(' + (n - 1) + ')，于是这一层停在乘法这里，等下一层返回。', { vars: { '栈帧数': m.frames.length } });
      }
      var result = 1;
      for (n = 1; n <= 4; n++) {
        var f = m.frames[m.frames.length - 1];
        result = n === 1 ? 1 : result * n;
        f.vars[2].v = result; f.vars[2].hl = 'ok';
        rec.step(n === 1 ? 4 : 5, n === 1 ? 'n = 1，到达递归的出口，不再调用自己，result = 1。'
          : '下一层返回了 ' + (result / n) + '，这一层算出 result = ' + n + ' × ' + (result / n) + ' = ' + result + '。', { vars: { '栈帧数': m.frames.length } });
        m.frames.pop();
        var top = m.frames[m.frames.length - 1];
        top.hl = 'cur';
        if (top.id !== 'm') top.note = '收到 ' + result;
        rec.step(6, 'factorial(' + n + ') 返回 ' + result + '，它的栈帧被释放。' + (n < 4 ? '回到 factorial(' + (n + 1) + ') 停下的地方继续。' : '回到 main，栈恢复到调用之前的样子。'),
          { vars: { '栈帧数': m.frames.length } });
        if (top.id !== 'm') top.note = null;
      }
    }
  });
})();
