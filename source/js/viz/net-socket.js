/* 《TCP Socket 入门》：客户端和服务器的系统调用，以及它们引起的报文 */
(function () {
  'use strict';
  var LX = 10, RX = 500, BW = 210, BH = 24, TOP = 46, ROW = 27;

  // rows: [{ c: 客户端此时的调用, s: 服务器此时的调用, dir: 'cs' | 'sc' | null, seg: 报文 }]
  function view(m) {
    var boxes = [], arrows = [], last = m.rows.length - 1;
    m.rows.forEach(function (r, i) {
      var y = TOP + i * ROW, cur = i === last;
      boxes.push({ id: 'c' + i, x: LX, y: y, w: BW, h: BH, text: r.c || '', hl: r.c ? (cur ? 'new' : null) : 'empty' });
      boxes.push({ id: 's' + i, x: RX, y: y, w: BW, h: BH, text: r.s || '', hl: r.s ? (cur ? 'new' : null) : 'empty' });
      if (r.dir) arrows.push({ from: r.dir === 'cs' ? 'c' + i : 's' + i, to: r.dir === 'cs' ? 's' + i : 'c' + i, label: r.seg, hl: cur ? 'cur' : null });
    });
    return { kind: 'free', boxes: boxes, arrows: arrows,
      texts: [{ id: 'hc', x: LX, y: 30, text: '客户端 echo_client', cls: 'muted' }, { id: 'hs', x: RX, y: 30, text: '服务器 echo_server', cls: 'muted' }] };
  }

  var STEPS = [
    { row: { s: 'socket() → 3' }, msg: '服务器创建一个套接字，得到文件描述符 3。此时它只是一个「空」的 TCP 套接字，还没有地址。' },
    { row: { s: 'bind(3, 0.0.0.0:9000)' }, msg: 'bind 给它指定地址和端口：本机所有地址的 9000 端口。' },
    { row: { s: 'listen(3, 16)' }, msg: 'listen 把它变成监听套接字（LISTEN 状态），内核开始替它接受连接，最多 16 个排队等待 accept（第 2.5 篇的 backlog）。' },
    { row: { s: 'accept(3) 阻塞…' }, msg: '服务器调用 accept，等待连接。没有连接时，进程在这里睡眠。' },
    { row: { c: 'socket() → 3' }, msg: '客户端也创建一个套接字。客户端通常不需要 bind，内核会自动分配一个临时端口。' },
    { row: { c: 'connect(3, 10.0.2.2:9000)', dir: 'cs', seg: 'SYN' }, msg: 'connect 发起三次握手：内核发出 SYN，connect 阻塞等待。' },
    { row: { c: 'connect 等待…', dir: 'sc', seg: 'SYN+ACK' }, msg: '服务器的内核回复 SYN+ACK。注意，这一切都由内核完成，服务器程序还在 accept 中睡眠，什么都不知道。' },
    { row: { c: 'connect 返回 0', dir: 'cs', seg: 'ACK' }, msg: '客户端的内核回复 ACK，握手完成，connect 返回。连接进入服务器的全连接队列。' },
    { row: { s: 'accept 返回 4' }, msg: '服务器的 accept 从队列中取出这个连接，返回一个新的描述符 4。监听套接字 3 继续用于接受其他连接，和这个客户端的通信都通过 4 进行。' },
    { row: { c: 'write(3, "hello\\n")', dir: 'cs', seg: '数据 6 字节' }, msg: '客户端写入 6 字节，内核把它们装进一个报文段发出。write 返回时，数据只是进入了发送缓冲区。' },
    { row: { s: 'read(4) 返回 6' }, msg: '服务器的 read 读到这 6 个字节。' },
    { row: { s: 'write(4, "hello\\n")', dir: 'sc', seg: '数据 6 字节' }, msg: '服务器把它们原样写回。' },
    { row: { c: 'read(3) 返回 6' }, msg: '客户端读到回声，读够了发出的 6 个字节，打印出来。' },
    { row: { c: 'close(3)', dir: 'cs', seg: 'FIN' }, msg: '标准输入结束，客户端 close，内核发出 FIN。' },
    { row: { s: 'read(4) 返回 0' }, msg: '服务器的 read 返回 0：对方关闭了连接（读到了文件结束）。' },
    { row: { s: 'close(4)', dir: 'sc', seg: 'FIN' }, msg: '服务器关闭描述符 4，发出自己的 FIN，然后回到 accept，等待下一个客户端。监听套接字 3 一直开着。' }
  ];

  DSViz.register('socket-calls', {
    title: '套接字的系统调用与报文',
    height: 490,
    init: function () { return { rows: [] }; },
    view: view,
    start: function (m, rec) { rec.step(0, '点「运行」，逐步查看回声服务器和客户端各自调用了什么，以及每个调用引起了哪些报文。'); },
    actions: [{
      label: '运行',
      run: function (m, p, rec) {
        m.rows = [];
        STEPS.forEach(function (st) { m.rows.push(st.row); rec.step(0, st.msg); });
      }
    }]
  });
})();
