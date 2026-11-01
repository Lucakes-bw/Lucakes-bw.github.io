/* C 语言《输入与输出》：scanf 和输入缓冲区（scanf-buf）
   缓冲区画成一排字符，已经被读走的变淡，读取位置用下面的 ↑ 标出 */
(function () {
  'use strict';
  var X0 = 120, CW = 40;

  function show(ch) { return ch === '\n' ? '\\n' : ch; }

  function view(m, x) {
    var boxes = [], texts = [];
    texts.push({ id: 'h', x: 20, y: 64, text: '输入缓冲区' });
    if (!m.buf.length) texts.push({ id: 'e', x: X0, y: 64, text: '（空，scanf 会停下来等用户输入）', cls: 'muted' });
    m.buf.forEach(function (c, i) {
      var hl = i < m.pos ? 'dim' : (x.look === i ? (x.bad ? 'warn' : 'cur') : null);
      if (x.take && i >= x.take[0] && i < x.take[1]) hl = 'ok';
      boxes.push({ id: 'k' + c.id, x: X0 + i * CW, y: 44, w: CW, h: 34, text: show(c.ch), hl: hl });
    });
    if (m.pos <= m.buf.length && m.buf.length) texts.push({ id: 'p', x: X0 + m.pos * CW + CW / 2, y: 98, text: '↑ 读取位置', anchor: 'middle', cls: 'muted' });
    texts.push({ id: 'u', x: 20, y: 140, text: '屏幕上', cls: 'muted' });
    texts.push({ id: 'o', x: X0, y: 140, text: m.screen || '' });
    return { kind: 'free', boxes: boxes, texts: texts, vars: m.vars };
  }

  function type(m, s) { for (var i = 0; i < s.length; i++) m.buf.push({ id: m.seq++, ch: s[i] }); }
  function reset(m) { m.buf = []; m.pos = 0; m.seq = 0; m.screen = ''; m.vars = {}; }
  function rest(m) { return m.buf.slice(m.pos).map(function (c) { return show(c.ch); }).join(''); }

  function trap(m, p, rec) {
    reset(m);
    m.vars = { tries: 0 };
    m.screen = '请输入一个正整数: ';
    rec.step(4, '没有修复的版本（没有下面代码中的第 9～11 行）。scanf("%d") 发现缓冲区是空的，停下来等用户输入。');
    type(m, 'abc\n');
    m.screen = '请输入一个正整数: abc⏎';
    rec.step(4, '用户输入 abc 并回车，这 4 个字符（包括换行符）进入缓冲区。');
    for (var t = 1; t <= 3; t++) {
      m.vars = { tries: t - 1, r: 0 };
      rec.step(4, (t === 1 ? '' : '第 ' + t + ' 次：') + '%d 要读一个数字，看到的第一个字符是 a，不是数字：读取失败，返回 0。a 没有被取走，读取位置没有动。', { look: m.pos, bad: true });
      m.vars = { tries: t, r: 0 };
      m.screen = '输入无效';
      rec.step(8, '打印「输入无效」，tries = ' + t + '，回到循环开头再调用 scanf。' + (t === 3 ? '缓冲区里还是 abc，用户根本没有机会输入 7。' : ''));
    }
    m.vars = { tries: 5, r: 0 };
    m.screen = '循环结束，tries = 5';
    rec.step(0, '每次都卡在同一个 a 上，直到 tries 达到 5 才退出；没有 tries 的限制，就是死循环。', { look: m.pos, bad: true });
  }

  function fixed(m, p, rec) {
    reset(m);
    type(m, 'abc\n');
    m.vars = { tries: 0 };
    rec.step(4, '修复后的版本。缓冲区中同样是 abc 和换行符。');
    m.vars = { tries: 0, r: 0 };
    rec.step(4, 'scanf("%d") 看到 a，读取失败，返回 0。', { look: 0, bad: true });
    for (var i = 0; i < 4; i++) {
      m.pos = i + 1;
      rec.step(11, 'getchar() 取走一个字符「' + show(m.buf[i].ch) + '」' + (i === 3 ? '，是换行符，循环结束：这一行剩下的字符都丢掉了。' : '，不是换行符，继续。'), { take: [i, i + 1] });
    }
    m.vars = { tries: 1 };
    rec.step(4, '缓冲区空了，scanf 停下来，等用户重新输入。');
    type(m, '7\n');
    rec.step(4, '用户输入 7 并回车。');
    m.pos += 1;
    m.vars = { tries: 1, r: 1, x: 7 };
    rec.step(5, '%d 读到 7，返回 1，x = 7 > 0，跳出循环。注意换行符仍然留在缓冲区里，下一次读取会先遇到它。', { take: [m.pos - 1, m.pos] });
  }

  function charTrap(m, p, rec) {
    reset(m);
    m.screen = '请输入数量: ';
    rec.step(0, '陷阱四：scanf("%d", &n); 之后再 scanf("%c", &choice);');
    type(m, '3\n');
    m.screen = '请输入数量: 3⏎';
    rec.step(0, '用户输入 3 并回车。');
    m.pos = 1;
    m.vars = { n: 3 };
    rec.step(0, 'scanf("%d") 读走 3。它在数字之后遇到换行符就停下，换行符留在缓冲区里。', { take: [0, 1] });
    m.pos = 2;
    m.vars = { n: 3, choice: 10 };
    m.screen = '确认吗（y/n）: （没有等用户输入）';
    rec.step(0, 'scanf("%c") 不跳过空白字符，直接读到了留下的换行符，choice 的编码是 10。程序根本没有等用户输入 y。', { take: [1, 2], look: 1, bad: true });
    reset(m);
    type(m, '3\n');
    m.pos = 1; m.vars = { n: 3 };
    rec.step(0, '修复：把格式写成 " %c"，开头的空格表示「跳过任意多个空白字符」。还是从留下换行符的状态开始。', { look: 1 });
    m.pos = 2;
    rec.step(0, '空格跳过了换行符。缓冲区空了，scanf 停下来等待。');
    type(m, 'y\n');
    m.pos = 3;
    m.vars = { n: 3, choice: 'y' };
    m.screen = '确认吗（y/n）: y⏎';
    rec.step(0, '用户输入 y，%c 读到 y。', { take: [2, 3] });
  }

  DSViz.register('scanf-buf', {
    title: 'scanf 和输入缓冲区',
    height: 160,
    code: [
      'int x, tries = 0;',
      'while (tries < 5) {',
      '    printf("请输入一个正整数: ");',
      '    int r = scanf("%d", &x);',
      '    if (r == 1 && x > 0) break;',
      '    if (r == EOF) return 1;',
      '    printf("输入无效\\n");',
      '    ++tries;',
      '    // 修复：丢弃这一行剩下的字符',
      '    int ch;',
      '    while ((ch = getchar()) != \'\\n\' && ch != EOF) {}',
      '}'
    ],
    init: function () { var m = {}; reset(m); return m; },
    view: view,
    start: function (m, rec) { trap(m, {}, rec); },
    actions: [
      { label: '陷阱三：没有丢弃', run: trap },
      { label: '修复后', run: fixed },
      { label: '陷阱四：%c 读到换行', run: charTrap }
    ]
  });
})();
