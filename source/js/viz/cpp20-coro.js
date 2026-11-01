/* C++20《协程》：生成器 counter() 和 main 交替执行（coro）
   左边是 main，右边是协程，下面是堆上的协程帧；正在运行的一方高亮 */
(function () {
  'use strict';

  function view(m, x) {
    var boxes = [
      { id: 'main', x: 40, y: 50, w: 260, h: 40, text: m.main, hl: m.run === 'main' ? 'cur' : 'dim' },
      { id: 'co', x: 420, y: 50, w: 260, h: 40, text: m.co, hl: m.run === 'co' ? 'cur' : 'dim' }
    ];
    var texts = [
      { id: 'tm', x: 170, y: 38, text: 'main（调用者）', anchor: 'middle' },
      { id: 'tc', x: 550, y: 38, text: '协程 counter()', anchor: 'middle' },
      { id: 'th', x: 40, y: 150, text: '堆上的协程帧', cls: 'muted' }
    ];
    if (m.frame) boxes.push({ id: 'fr', x: 150, y: 132, w: 420, h: 40, text: m.frame, hl: x.frameHl || null });
    else texts.push({ id: 'nf', x: 150, y: 157, text: m.freed ? '已经 destroy()，协程帧被释放' : '（还没有创建）', cls: 'muted' });
    var arrows = [];
    if (x.go === 'resume') arrows.push({ from: 'main', to: 'co', hl: 'new', label: 'resume()' });
    if (x.go === 'yield') arrows.push({ from: 'co', to: 'main', hl: 'ok', label: x.label || 'co_yield：暂停' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function frame(m, i, where, cur) {
    m.frame = 'i = ' + i + '　current = ' + cur + '　' + where;
  }

  DSViz.register('coro', {
    title: '生成器：main 和协程交替执行',
    height: 190,
    code: [
      'Generator<int> counter() {',
      '    for (int i = 1; i <= 3; ++i)',
      '        co_yield i;',
      '}                              // 执行完毕',
      '',
      'auto gen = counter();',
      'for (int x : gen)',
      '    std::cout << x;',
      '// gen 离开作用域：destroy()'
    ],
    init: function () { return { run: 'main', main: '', co: '', frame: null, freed: false }; },
    view: view,
    start: function (m, rec) {
      m.run = 'main'; m.main = '调用 counter()'; m.co = '还没有开始执行';
      m.frame = '在函数开头暂停（initial_suspend）';
      rec.step(6, '调用 counter() 时，函数体一行都没有执行：编译器在堆上创建了协程帧，协程立刻在开头暂停，返回一个 Generator 给 main。', { frameHl: 'new' });
      m.main = 'begin()：第一次 resume';
      rec.step(7, '范围 for 调用 begin()，其中的 resume() 把控制权交给协程。', { go: 'resume' });
      for (var i = 1; i <= 3; i++) {
        m.run = 'co';
        m.co = i === 1 ? '开始执行，i = 1' : '从暂停处恢复，++i → i = ' + i;
        frame(m, i, '运行中', i - 1 || '-');
        rec.step(i === 1 ? 2 : 2, i === 1 ? '协程第一次开始执行，进入循环，i = 1。main 停在 begin() 里等着。'
          : '协程从 co_yield 的下一行继续：局部变量 i 保存在协程帧里，还是 ' + (i - 1) + '，++i 之后是 ' + i + '。', { frameHl: 'cur' });
        m.co = 'co_yield ' + i;
        frame(m, i, '暂停在 co_yield', i);
        rec.step(3, 'co_yield ' + i + '：把 ' + i + ' 存进 promise 的 current，记下暂停的位置，然后暂停，控制权回到 main。', { go: 'yield', frameHl: 'cur' });
        m.run = 'main'; m.main = '拿到 ' + i;
        rec.step(8, 'main 读出 current，拿到 ' + i + '。此刻协程完全没有在运行，只是一块躺在堆上的协程帧。');
        m.main = '++：resume';
        rec.step(7, '范围 for 执行 ++，又调用一次 resume()。', { go: 'resume' });
      }
      m.run = 'co'; m.co = '从暂停处恢复，++i → i = 4';
      frame(m, 4, '运行中', 3);
      rec.step(2, 'i 变成 4，不满足 i <= 3，循环结束。', { frameHl: 'cur' });
      m.co = '执行完毕（final_suspend）';
      frame(m, 4, '在末尾暂停，done() == true', 3);
      rec.step(4, '协程执行到函数末尾，在 final_suspend 处暂停。协程帧还在，但 done() 已经是 true。', { go: 'yield', label: '执行完毕', frameHl: 'cur' });
      m.run = 'main'; m.main = 'done()，遍历结束';
      rec.step(7, '范围 for 发现 done() 为 true，循环结束。');
      m.frame = null; m.freed = true; m.main = 'gen 析构'; m.co = '不存在了';
      rec.step(9, 'gen 离开作用域，析构函数调用 destroy() 释放协程帧。整个过程只有一个线程，任何时刻只有一方在运行，不需要锁。');
    }
  });
})();
