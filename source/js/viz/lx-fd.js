/* 文件 IO 一章：文件描述符表 → 打开文件表 → inode
   fd-table（《文件描述符与系统调用》）、redir-file 和 redir-pipe（《重定向与管道的原理》） */
(function () {
  'use strict';
  var SIZES = { 'a.txt': 12, '/etc/hostname': 7, 'b.txt': 0 };
  var TOP = 46, WIDTH = 720;

  function init() {
    return {
      procs: [{ name: 'shell', fds: ['tty', 'tty', 'tty'] }],
      entries: [{ id: 'tty', node: 'tty', off: null, ref: 3 }],
      nodes: [{ id: 'tty', label: '终端' }],
      seq: 0
    };
  }
  function reset(m) { var f = init(); Object.keys(f).forEach(function (k) { m[k] = f[k]; }); }

  function entry(m, id) { return m.entries.filter(function (e) { return e.id === id; })[0]; }
  function node(m, id) { return m.nodes.filter(function (n) { return n.id === id; })[0]; }
  function fdKey(k, fd) { return 'f' + k + '_' + fd; }
  function entryName(m, id) { var e = entry(m, id); return e.name || node(m, e.node).label; }

  function view(m, x) {
    var boxes = [], arrows = [], hl = x.hl || {}, n = m.procs.length;
    // 多个进程时，箭头会穿过其他进程的描述符表，所以不画描述符的箭头，改为在格子中写出它指向哪一项
    var colW = n === 1 ? 0 : 124, fdW = n === 1 ? 92 : 116, enX = n === 1 ? 230 : 20 + n * colW + 16;
    var ndW = 110, ndX = WIDTH - 20 - ndW, enW = Math.min(230, ndX - 50 - enX);
    var texts = [];
    m.procs.forEach(function (p, k) {
      var px = 20 + k * colW;
      texts.push({ id: 'p' + k, x: px, y: 26, text: n === 1 ? '文件描述符表（' + p.name + '）' : p.name, cls: 'muted' });
      p.fds.forEach(function (e, fd) {
        var key = fdKey(k, fd);
        var text = 'fd ' + fd + (n > 1 && e != null ? ' → ' + entryName(m, e) : '');
        boxes.push({ id: key, x: px, y: TOP + fd * 40, w: fdW, h: 32, text: text, hl: e == null ? 'empty' : hl[key] });
        if (e != null && n === 1) arrows.push({ from: key, to: 'e' + e, hl: hl[key] });
      });
    });
    m.entries.forEach(function (e, k) {
      var text = (n > 1 || e.name ? entryName(m, e.id) + ' · ' : '') + (e.note ? e.note + ' · ' : '') + (e.off == null ? '' : '偏移 ' + e.off + ' · ') + '引用 ' + e.ref;
      boxes.push({ id: 'e' + e.id, x: enX, y: TOP + k * 48, w: enW, h: 36, text: text, hl: hl['e' + e.id] });
      arrows.push({ from: 'e' + e.id, to: 'n' + e.node, hl: hl['e' + e.id] });
    });
    m.nodes.forEach(function (nd, k) {
      boxes.push({ id: 'n' + nd.id, x: ndX, y: TOP + k * 48, w: ndW, h: 36, text: nd.label, hl: hl['n' + nd.id] });
    });
    texts.push({ id: 'h2', x: enX, y: 26, text: '打开文件表（内核）', cls: 'muted' });
    texts.push({ id: 'h3', x: ndX, y: 26, text: n === 1 ? 'inode（文件）' : '文件', cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function H() {          // H('f0_3', 'new', 'eo1', 'cur') → { f0_3: 'new', eo1: 'cur' }
    var h = {};
    for (var i = 0; i < arguments.length; i += 2) h[arguments[i]] = arguments[i + 1];
    return { hl: h };
  }
  function lowestFd(p) {
    for (var i = 0; i < p.fds.length; i++) if (p.fds[i] == null) return i;
    return p.fds.length;
  }
  function trim(p) { while (p.fds.length > 3 && p.fds[p.fds.length - 1] == null) p.fds.pop(); }
  function unref(m, id) {          // 引用计数减 1，变成 0 时删除这一项；返回是否被删除
    var e = entry(m, id);
    if (--e.ref > 0) return false;
    m.entries = m.entries.filter(function (x) { return x !== e; });
    if (!m.entries.some(function (x) { return x.node === e.node; })) m.nodes = m.nodes.filter(function (x) { return x.id !== e.node; });
    return true;
  }

  function doOpen(m, k, name, label) {
    var p = m.procs[k], fd = lowestFd(p);
    if (!node(m, name)) m.nodes.push({ id: name, label: label || name });
    var id = 'o' + (++m.seq);
    m.entries.push({ id: id, node: name, off: 0, ref: 1 });
    p.fds[fd] = id;
    return { fd: fd, id: id };
  }
  function doClose(m, k, fd) {
    var p = m.procs[k], id = p.fds[fd];
    p.fds[fd] = null;
    trim(p);
    return unref(m, id);
  }
  function doDup2(m, k, oldfd, newfd) {
    var p = m.procs[k], old = p.fds[newfd];
    p.fds[newfd] = p.fds[oldfd];
    entry(m, p.fds[oldfd]).ref++;
    if (old != null) unref(m, old);
  }
  function doFork(m, k, name) {
    var p = m.procs[k];
    m.procs.push({ name: name, fds: p.fds.slice() });
    p.fds.forEach(function (id) { if (id != null) entry(m, id).ref++; });
    return m.procs.length - 1;
  }
  function doExit(m, k) {
    var gone = false;
    m.procs[k].fds.forEach(function (id) { if (id != null && unref(m, id)) gone = true; });
    m.procs.splice(k, 1);
    return gone;
  }

  /* ---------- 《文件描述符与系统调用》 ---------- */

  function stepOpen(m, name, rec, line) {
    var r = doOpen(m, 0, name);
    rec.step(line, 'open("' + name + '") 在打开文件表中新建一项（偏移 0），再分配最小的空闲描述符 ' + r.fd + ' 指向它。',
      Object.assign(H(fdKey(0, r.fd), 'new', 'e' + r.id, 'new', 'n' + name, 'cur'), { vars: { '返回值': r.fd } }));
    return r.fd;
  }
  function stepRead(m, fd, n, rec, line) {
    var e = entry(m, m.procs[0].fds[fd]);
    var size = SIZES[e.node] == null ? 100 : SIZES[e.node];
    var got = Math.max(0, Math.min(n, size - e.off));
    rec.step(line, 'read(' + fd + ', buf, ' + n + ')：沿着 fd ' + fd + ' 找到打开文件表中的一项，从它记录的偏移 ' + e.off + ' 开始读。', H(fdKey(0, fd), 'cur', 'e' + e.id, 'cur'));
    e.off += got;
    rec.step(line, '读到 ' + got + ' 个字节，偏移变成 ' + e.off + '。偏移记录在打开文件表中，不在描述符里，也不在文件里。',
      Object.assign(H(fdKey(0, fd), 'cur', 'e' + e.id, 'new'), { vars: { '返回值': got } }));
  }
  function stepClose(m, fd, rec, line) {
    var id = m.procs[0].fds[fd];
    rec.step(line, 'close(' + fd + ')：释放 fd ' + fd + '，打开文件表中那一项的引用计数减 1。', H(fdKey(0, fd), 'del', 'e' + id, 'del'));
    var gone = doClose(m, 0, fd);
    rec.step(line, gone ? '引用计数变成 0，这一项被删除。fd ' + fd + ' 空了出来，下一次 open 会优先使用它。'
      : '还有其他描述符引用这一项（引用 ' + entry(m, id).ref + '），它保留下来。', { vars: { '返回值': 0 } });
  }
  function checkFd(m, fd) {
    if (typeof fd !== 'number' || fd < 0 || fd % 1 !== 0) return '描述符是一个非负整数';
    if (m.procs[0].fds[fd] == null) return 'fd ' + fd + ' 没有打开（read / close 会返回 -1，errno 为 EBADF）';
    return null;
  }

  DSViz.register('fd-table', {
    title: '文件描述符、打开文件表和 inode',
    height: 300,
    code: [
      'int a = open("a.txt", O_RDONLY);',
      'int b = open("a.txt", O_RDONLY);',
      'read(a, buf, 5);',
      'read(b, buf, 5);',
      'close(a);',
      'int c = open("/etc/hostname", O_RDONLY);'
    ],
    init: function () { var m = init(); m.procs[0].name = '进程'; return m; },
    view: view,
    start: function (m, rec) {
      rec.step(0, '进程启动时，0、1、2 三个描述符已经打开，都指向终端（它们共用打开文件表中的同一项，引用 3）。点「运行示例」逐步执行右边的代码，也可以自己 open、read、close。');
    },
    actions: [{
      label: '运行示例',
      run: function (m, p, rec) {
        reset(m); m.procs[0].name = '进程';
        rec.step(0, '从进程刚启动的状态开始。');
        var a = stepOpen(m, 'a.txt', rec, 1);
        stepOpen(m, 'a.txt', rec, 2);
        rec.step(2, '同一个文件打开了两次：两个描述符指向打开文件表中的两项，各有各的偏移，最终指向同一个 inode。');
        stepRead(m, a, 5, rec, 3);
        stepRead(m, 4, 5, rec, 4);
        stepClose(m, a, rec, 5);
        stepOpen(m, '/etc/hostname', rec, 6);
        rec.step(6, '新打开的文件拿到了 3：open 总是返回当前最小的空闲描述符。第 3.3 篇的重定向就利用了这一点。');
      }
    }, {
      label: 'open',
      fields: [{ key: 'name', label: '文件', value: 'b.txt', size: 12 }],
      check: function (m, p) {
        if (!String(p.name).trim()) return '请输入文件名';
        return lowestFd(m.procs[0]) >= 6 ? '演示中最多打开到 fd 5' : null;
      },
      run: function (m, p, rec) { stepOpen(m, String(p.name), rec, 0); }
    }, {
      label: 'read',
      fields: [{ key: 'fd', label: 'fd', value: 3, size: 2 }, { key: 'n', label: '字节数', value: 4, size: 3 }],
      check: function (m, p) {
        var err = checkFd(m, p.fd);
        if (err) return err;
        if (m.procs[0].fds[p.fd] === 'tty') return '演示中不从终端读取，请选择一个打开的文件';
        return typeof p.n === 'number' && p.n > 0 ? null : '字节数是正整数';
      },
      run: function (m, p, rec) { stepRead(m, p.fd, p.n, rec, 0); }
    }, {
      label: 'close',
      fields: [{ key: 'fd', label: 'fd', value: 3, size: 2 }],
      check: function (m, p) { return checkFd(m, p.fd); },
      run: function (m, p, rec) { stepClose(m, p.fd, rec, 0); }
    }]
  });

  /* ---------- 《重定向与管道的原理》：命令 > 文件 ---------- */

  DSViz.register('redir-file', {
    title: 'ls > out.txt 是怎样实现的',
    height: 270,
    code: [
      'pid_t pid = fork();',
      'if (pid == 0) {                  // 子进程',
      '    int fd = open("out.txt", ...);',
      '    dup2(fd, STDOUT_FILENO);',
      '    close(fd);',
      '    execvp("ls", ...);',
      '}',
      'waitpid(pid, &status, 0);'
    ],
    init: init,
    view: view,
    start: function (m, rec) {
      rec.step(0, 'Shell 的 0、1、2 都指向终端。点「运行」，看 Shell 怎样把 ls 的标准输出接到 out.txt。');
    },
    actions: [{
      label: '运行',
      run: function (m, p, rec) {
        reset(m);
        rec.step(0, 'Shell 的 0、1、2 都指向终端。');
        var c = doFork(m, 0, '子进程');
        rec.step(1, 'fork 复制出一个子进程。子进程的描述符表是父进程的副本，指向打开文件表中的同一项，所以终端的引用计数从 3 变成 6。', H(fdKey(c, 0), 'new', fdKey(c, 1), 'new', fdKey(c, 2), 'new', 'etty', 'cur'));
        var r = doOpen(m, c, 'out.txt');
        rec.step(3, '子进程 open("out.txt")，得到最小的空闲描述符 3。', Object.assign(H(fdKey(c, 3), 'new', 'e' + r.id, 'new', 'nout.txt', 'cur'), { vars: { fd: 3 } }));
        rec.step(4, 'dup2(3, 1)：让 fd 1 指向 fd 3 所指的那一项。fd 1 原来指向终端，先断开（终端的引用减 1）。', H(fdKey(c, 1), 'cur', fdKey(c, 3), 'cur'));
        doDup2(m, c, 3, 1);
        rec.step(4, '现在 fd 1 和 fd 3 指向同一项（引用 2）。以后写 fd 1，数据就进入 out.txt。', H(fdKey(c, 1), 'new', 'e' + r.id, 'new'));
        doClose(m, c, 3);
        rec.step(5, 'close(3)：fd 3 已经没用了，关闭它。out.txt 那一项的引用变回 1，只剩 fd 1 指向它。', H(fdKey(c, 1), 'new'));
        m.procs[c].name = 'ls';
        rec.step(6, 'execvp 把子进程换成 ls 程序。exec 不改变描述符表，ls 一启动，它的标准输出就已经是 out.txt 了，ls 本身完全不知道发生了重定向。', H(fdKey(c, 1), 'cur'));
        entry(m, r.id).note = '已写入';
        rec.step(6, 'ls 照常向 fd 1 写出结果，数据进入了 out.txt，这一项的偏移随之增加。', H(fdKey(c, 1), 'cur', 'e' + r.id, 'new', 'nout.txt', 'new'));
        doExit(m, c);
        rec.step(8, 'ls 结束，内核关闭它所有的描述符：out.txt 那一项的引用变成 0，被删除；终端的引用回到 3。Shell 的描述符表从头到尾没有变过。', H('etty', 'cur'));
      }
    }]
  });

  /* ---------- 《重定向与管道的原理》：命令1 | 命令2 ---------- */

  function pipeDemo(m, rec, forget) {
    reset(m);
    m.nodes.push({ id: 'pipe', label: '管道缓冲区' });
    rec.step(0, 'Shell 要运行 ls | wc -l。');
    m.entries.push({ id: 'r', node: 'pipe', name: '读端', off: null, ref: 1 });
    m.entries.push({ id: 'w', node: 'pipe', name: '写端', off: null, ref: 1 });
    m.procs[0].fds[3] = 'r';
    m.procs[0].fds[4] = 'w';
    rec.step(1, 'pipe(p)：内核创建一个管道缓冲区，返回两个描述符：p[0] = 3 是读端，p[1] = 4 是写端。', Object.assign(H(fdKey(0, 3), 'new', fdKey(0, 4), 'new', 'er', 'new', 'ew', 'new', 'npipe', 'new'), { vars: { 'p[0]': 3, 'p[1]': 4 } }));
    var L = doFork(m, 0, '左（ls）');
    rec.step(2, 'fork 出左边的子进程，它继承了全部 5 个描述符，读端和写端的引用都变成 2。', H(fdKey(L, 3), 'new', fdKey(L, 4), 'new'));
    doDup2(m, L, 4, 1);
    rec.step(3, '左边：dup2(p[1], 1)，标准输出改为管道的写端。', H(fdKey(L, 1), 'new', 'ew', 'cur'));
    doClose(m, L, 3); doClose(m, L, 4);
    rec.step(4, '左边：关闭 3 和 4。它只需要通过 fd 1 写管道。', H(fdKey(L, 1), 'cur'));
    var R = doFork(m, 0, '右（wc）');
    rec.step(7, 'fork 出右边的子进程，同样继承了 Shell 的 3 和 4。', H(fdKey(R, 3), 'new', fdKey(R, 4), 'new'));
    doDup2(m, R, 3, 0);
    rec.step(8, '右边：dup2(p[0], 0)，标准输入改为管道的读端。', H(fdKey(R, 0), 'new', 'er', 'cur'));
    doClose(m, R, 3); doClose(m, R, 4);
    rec.step(9, '右边：关闭 3 和 4。它只需要通过 fd 0 读管道。', H(fdKey(R, 0), 'cur'));
    if (!forget) {
      doClose(m, 0, 3); doClose(m, 0, 4);
      rec.step(12, 'Shell 自己不读也不写管道，关闭 3 和 4。现在写端只被左边的 fd 1 引用，读端只被右边的 fd 0 引用。', H('ew', 'cur', 'er', 'cur'));
    } else {
      rec.step(12, '（错误）Shell 忘了关闭自己的 3 和 4。写端的引用是 2：左边的 fd 1 和 Shell 的 fd 4。', H(fdKey(0, 4), 'warn', 'ew', 'warn'));
    }
    m.procs[L].name = 'ls'; m.procs[R].name = 'wc -l';
    rec.step(5, '两个子进程分别 exec 成 ls 和 wc。ls 向 fd 1 写，数据进入管道；wc 从 fd 0 读，从管道中取出数据。', H('npipe', 'cur', fdKey(1, 1), 'cur', fdKey(2, 0), 'cur'));
    doExit(m, 1);
    if (!forget) {
      rec.step(13, 'ls 写完后退出，它的描述符全部关闭：写端的引用变成 0，被删除。', H('er', 'cur'));
      rec.step(14, '管道没有写端了，wc 读完缓冲区中剩下的数据后，read 返回 0（文件结束），wc 输出行数并退出。', H(fdKey(1, 0), 'ok', 'er', 'ok'));
      doExit(m, 1);
      rec.step(14, 'wc 退出，读端也被删除。Shell 的 waitpid 返回，一切恢复原样。', H('etty', 'cur'));
    } else {
      rec.step(13, 'ls 退出了，但写端的引用还是 1：Shell 的 fd 4 仍然指向它。', H(fdKey(0, 4), 'warn', 'ew', 'warn'));
      rec.step(14, '只要还有一个写端没有关闭，内核就认为「可能还会有数据」，wc 的 read 会一直等待，永远读不到文件结束。wc 不退出，Shell 的 waitpid 也一直等下去：程序卡住了。', H(fdKey(1, 0), 'warn', 'ew', 'warn'));
    }
  }

  DSViz.register('redir-pipe', {
    title: 'ls | wc -l 是怎样实现的',
    height: 280,
    code: [
      'pipe(p);',
      'if (fork() == 0) {            // 左：ls',
      '    dup2(p[1], STDOUT_FILENO);',
      '    close(p[0]); close(p[1]);',
      '    execvp("ls", ...);',
      '}',
      'if (fork() == 0) {            // 右：wc -l',
      '    dup2(p[0], STDIN_FILENO);',
      '    close(p[0]); close(p[1]);',
      '    execvp("wc", ...);',
      '}',
      'close(p[0]); close(p[1]);',
      'waitpid(left, NULL, 0);',
      'waitpid(right, &status, 0);'
    ],
    init: init,
    view: view,
    start: function (m, rec) {
      rec.step(0, '点「正确版本」，看 Shell 怎样用一个管道连接两个进程；再点「忘了关闭」，看看为什么少两行 close 程序就会卡住。');
    },
    actions: [
      { label: '正确版本', run: function (m, p, rec) { pipeDemo(m, rec, false); } },
      { label: '忘了关闭', run: function (m, p, rec) { pipeDemo(m, rec, true); } }
    ]
  });
})();
