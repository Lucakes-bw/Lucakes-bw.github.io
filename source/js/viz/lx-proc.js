/* 《进程：fork、exec、wait》：进程树 */
(function () {
  'use strict';
  var COL = 170, ROW = 38, X0 = 20, TOP = 40, W = 140, H = 28;

  // 进程树横向排列：代数决定横坐标，先序遍历的顺序决定纵坐标
  function view(m, x) {
    var boxes = [], arrows = [], row = 0, hl = x.hl || {};
    function kids(pid) { return m.procs.filter(function (p) { return p.ppid === pid; }); }
    function walk(p, depth) {
      var label = p.pid + (p.name ? ' ' + p.name : '') + (p.state === 'Z' ? '（僵尸）' : '');
      boxes.push({ id: 'p' + p.pid, x: X0 + depth * COL, y: TOP + row * ROW, w: W, h: H, text: label,
        hl: hl[p.pid] || (p.state === 'Z' ? 'warn' : null) });
      row++;
      kids(p.pid).forEach(function (c) {
        arrows.push({ from: 'p' + p.pid, to: 'p' + c.pid, hl: hl[c.pid] });
        walk(c, depth + 1);
      });
    }
    m.procs.filter(function (p) { return !m.procs.some(function (q) { return q.pid === p.ppid; }); })
      .forEach(function (r) { walk(r, 0); });
    return { kind: 'free', boxes: boxes, arrows: arrows, vars: x.vars,
      texts: [{ id: 'h', x: X0, y: 22, text: '箭头从父进程指向子进程', cls: 'muted' }] };
  }
  function H1(pid, type) { var h = {}; h[pid] = type; return { hl: h }; }
  function find(m, pid) { return m.procs.filter(function (p) { return p.pid === pid; })[0]; }

  DSViz.register('fork-loop', {
    title: '循环中调用 fork：一共有多少个进程',
    height: 350,
    code: [
      'for (int i = 0; i < n; i++) {',
      '    fork();',
      '}',
      'printf("pid = %d，父进程 = %d\\n", ...);'
    ],
    init: function () { return { procs: [{ pid: 1000, ppid: 0 }], next: 1001 }; },
    view: view,
    start: function (m, rec) { rec.step(0, '一开始只有一个进程 1000。输入循环次数 n（1 到 3），点「运行」。'); },
    actions: [{
      label: '运行',
      fields: [{ key: 'n', label: 'n', value: 3, size: 2 }],
      check: function (m, p) { return [1, 2, 3].indexOf(p.n) >= 0 ? null : 'n 取 1、2 或 3'; },
      run: function (m, p, rec) {
        m.procs = [{ pid: 1000, ppid: 0 }]; m.next = 1001;
        rec.step(1, '进程 1000 开始执行循环，i = 0。', { vars: { i: 0, '进程数': 1 } });
        for (var i = 0; i < p.n; i++) {
          var alive = m.procs.slice();
          alive.forEach(function (q) {
            var c = { pid: m.next++, ppid: q.pid };
            m.procs.push(c);
            rec.step(2, 'i = ' + i + '：进程 ' + q.pid + ' 调用 fork，创建了子进程 ' + c.pid + '。子进程从 fork 返回处继续执行，它看到的 i 也是 ' + i + '。',
              Object.assign(H1(c.pid, 'new'), { vars: { i: i, '进程数': m.procs.length } }));
          });
          rec.step(1, '第 ' + (i + 1) + ' 轮结束：每个进程都执行了一次 fork，进程数翻倍，变成 ' + m.procs.length + ' 个。' + (i + 1 < p.n ? '所有进程都继续执行 i++。' : ''),
            { vars: { i: i + 1, '进程数': m.procs.length } });
        }
        rec.step(4, '循环结束，' + m.procs.length + ' 个进程各自执行一次 printf，所以输出 ' + m.procs.length + ' 行。n 次循环得到 2 的 n 次方个进程。',
          { vars: { '进程数': m.procs.length } });
      }
    }]
  });

  function lifeInit() {
    return { procs: [{ pid: 1, ppid: 0, name: 'systemd' }, { pid: 900, ppid: 1, name: 'bash' }, { pid: 1000, ppid: 900, name: './prog' }] };
  }

  DSViz.register('proc-life', {
    title: '僵尸进程和孤儿进程',
    height: 210,
    init: lifeInit,
    view: view,
    start: function (m, rec) { rec.step(0, 'bash（900）运行了程序 1000。点「僵尸进程」或「孤儿进程」。'); },
    actions: [{
      label: '僵尸进程',
      run: function (m, p, rec) {
        var f = lifeInit(); m.procs = f.procs;
        rec.step(0, 'bash（900）运行了程序 1000。');
        m.procs.push({ pid: 1001, ppid: 1000 });
        rec.step(0, '1000 调用 fork，创建了子进程 1001。', H1(1001, 'new'));
        find(m, 1001).state = 'Z';
        rec.step(0, '1001 执行完毕，调用 exit。它的内存、打开的文件都被释放了，但进程表中还保留着一项：退出码，等父进程来取。这时它就是僵尸进程，ps 中的状态是 Z，名字后面显示 <defunct>。');
        rec.step(0, '父进程 1000 一直没有调用 wait（比如在忙别的事），1001 就一直是僵尸。僵尸不占内存，但占用一个进程号；大量积累会耗尽进程号，导致无法创建新进程。', H1(1000, 'cur'));
        m.procs = m.procs.filter(function (q) { return q.pid !== 1001; });
        rec.step(0, '1000 调用 waitpid，取走了 1001 的退出码，内核这才彻底删除 1001。所以每个子进程都要有人 wait。', H1(1000, 'ok'));
      }
    }, {
      label: '孤儿进程',
      run: function (m, p, rec) {
        var f = lifeInit(); m.procs = f.procs;
        rec.step(0, 'bash（900）运行了程序 1000。');
        m.procs.push({ pid: 1001, ppid: 1000 });
        rec.step(0, '1000 调用 fork，创建了子进程 1001。', H1(1001, 'new'));
        find(m, 1000).state = 'Z';
        rec.step(0, '父进程 1000 先结束了，它成为僵尸，等 bash 来 wait。', H1(1000, 'del'));
        m.procs = m.procs.filter(function (q) { return q.pid !== 1000; });
        find(m, 1001).ppid = 1;
        rec.step(0, 'bash 用 wait 回收了 1000。1001 失去了父进程，成为孤儿进程，内核把它「过继」给 1 号进程。getppid() 现在返回 1。', H1(1001, 'cur'));
        find(m, 1001).state = 'Z';
        rec.step(0, '1001 结束，成为僵尸，等它的新父进程 1 号来回收。', H1(1001, 'warn'));
        m.procs = m.procs.filter(function (q) { return q.pid !== 1001; });
        rec.step(0, '1 号进程（systemd）会 wait 所有过继给它的子进程，1001 被彻底删除。孤儿本身无害；但如果 1 号进程不做这件事，僵尸就会越积越多（本文的实验容器就是这样）。', H1(1, 'ok'));
      }
    }]
  });
})();
