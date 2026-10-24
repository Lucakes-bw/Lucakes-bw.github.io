/* 《TCP 连接管理》：握手、挥手、RST 的时序和双方的状态 */
(function () {
  'use strict';
  var LX = 20, RX = 530, BW = 170, BH = 26, TOP = 50, ROW = 32;

  // rows: [{ c: 客户端状态, s: 服务器状态, dir: 'cs' | 'sc' | null, seg: 报文说明 }]
  function view(m, x) {
    var boxes = [], arrows = [], last = m.rows.length - 1;
    m.rows.forEach(function (r, i) {
      var y = TOP + i * ROW, cur = i === last;
      boxes.push({ id: 'c' + i, x: LX, y: y, w: BW, h: BH, text: r.c, hl: cur && r.chg !== 's' ? 'new' : null });
      boxes.push({ id: 's' + i, x: RX, y: y, w: BW, h: BH, text: r.s, hl: cur && r.chg !== 'c' ? 'new' : null });
      if (r.dir) arrows.push({ from: r.dir === 'cs' ? 'c' + i : 's' + i, to: r.dir === 'cs' ? 's' + i : 'c' + i, label: r.seg, hl: cur ? 'cur' : null });
    });
    return { kind: 'free', boxes: boxes, arrows: arrows, vars: x.vars,
      texts: [{ id: 'hc', x: LX, y: 30, text: '客户端 10.0.0.1', cls: 'muted' }, { id: 'hs', x: RX, y: 30, text: '服务器 10.0.0.2:8000', cls: 'muted' }] };
  }

  function make(steps) {
    return function (m, p, rec) {
      m.rows = [];
      steps.forEach(function (st) {
        if (st.row) m.rows.push(st.row);
        rec.step(0, st.msg, { vars: st.vars });
      });
    };
  }

  var X = 100, Y = 300;    // 双方的初始序号（真实的值是随机的大数）
  var HS = [
    { row: { c: 'CLOSED', s: 'LISTEN' }, msg: '服务器调用 listen，进入 LISTEN 状态，等待连接。客户端还没有连接，状态是 CLOSED。' },
    { row: { c: 'SYN_SENT', s: 'SYN_RCVD', dir: 'cs', seg: 'SYN seq=' + X }, msg: '第一次握手：客户端调用 connect，发送 SYN，带上自己的初始序号 ' + X + '，进入 SYN_SENT。服务器收到后进入 SYN_RCVD。' },
    { row: { c: 'ESTABLISHED', s: 'SYN_RCVD', dir: 'sc', seg: 'SYN+ACK seq=' + Y + ' ack=' + (X + 1), chg: 'c' }, msg: '第二次握手：服务器回复 SYN+ACK：ack=' + (X + 1) + ' 确认收到了客户端的 SYN（SYN 占一个序号），seq=' + Y + ' 是服务器自己的初始序号。客户端收到后，connect 返回，进入 ESTABLISHED。' },
    { row: { c: 'ESTABLISHED', s: 'ESTABLISHED', dir: 'cs', seg: 'ACK ack=' + (Y + 1), chg: 's' }, msg: '第三次握手：客户端回复 ACK，确认收到了服务器的 SYN。服务器进入 ESTABLISHED，accept 返回。双方都确认了对方能收、自己能发，并交换了初始序号。' }
  ];
  var DATA = [
    { row: { c: 'ESTABLISHED', s: 'ESTABLISHED', dir: 'cs', seg: 'seq=' + (X + 1) + ' 数据 6 字节' }, msg: '客户端发送 6 字节的数据 hello\\n，序号从 ' + (X + 1) + ' 开始。' },
    { row: { c: 'ESTABLISHED', s: 'ESTABLISHED', dir: 'sc', seg: 'ACK ack=' + (X + 7) }, msg: '服务器确认：ack=' + (X + 7) + '，表示 ' + (X + 6) + ' 以前的字节都收到了，下一个期待 ' + (X + 7) + '。' }
  ];

  DSViz.register('tcp-conn', {
    title: 'TCP 连接的建立与关闭',
    height: 380,
    init: function () { return { rows: [] }; },
    view: view,
    start: function (m, rec) { rec.step(0, '选择一个场景，逐步查看双方发送的报文和状态的变化。序号用 ' + X + ' 和 ' + Y + ' 代替真实的随机数。'); },
    actions: [{
      label: '四次挥手',
      run: make(HS.concat(DATA, [
        { row: { c: 'FIN_WAIT_1', s: 'CLOSE_WAIT', dir: 'cs', seg: 'FIN seq=' + (X + 7) }, msg: '客户端调用 close，发送 FIN，进入 FIN_WAIT_1：「我没有数据要发了」。服务器收到后进入 CLOSE_WAIT，它的 read 会返回 0。' },
        { row: { c: 'FIN_WAIT_2', s: 'CLOSE_WAIT', dir: 'sc', seg: 'ACK ack=' + (X + 8), chg: 'c' }, msg: '服务器先确认这个 FIN。客户端进入 FIN_WAIT_2，等待服务器关闭。服务器可能还有数据要发，此时连接是「半关闭」的：服务器到客户端的方向仍然可以传送数据。' },
        { row: { c: 'TIME_WAIT', s: 'LAST_ACK', dir: 'sc', seg: 'FIN seq=' + (Y + 1) }, msg: '服务器处理完，也调用 close，发送自己的 FIN，进入 LAST_ACK。客户端收到后进入 TIME_WAIT。' },
        { row: { c: 'TIME_WAIT', s: 'CLOSED', dir: 'cs', seg: 'ACK ack=' + (Y + 2), chg: 's' }, msg: '客户端确认服务器的 FIN，服务器收到后进入 CLOSED。客户端还要在 TIME_WAIT 中等待一段时间（Linux 是 60 秒）才会 CLOSED。' }
      ]))
    }, {
      label: '合并成三次',
      run: make(HS.concat(DATA, [
        { row: { c: 'FIN_WAIT_1', s: 'CLOSE_WAIT', dir: 'cs', seg: 'FIN seq=' + (X + 7) }, msg: '客户端发送 FIN。这次服务器读到文件结束后立即调用 close。' },
        { row: { c: 'TIME_WAIT', s: 'LAST_ACK', dir: 'sc', seg: 'FIN+ACK seq=' + (Y + 1) + ' ack=' + (X + 8) }, msg: '服务器关闭得很快，对客户端 FIN 的确认还没有单独发出，就和自己的 FIN 合在了一个报文中。实测抓到的就是这种情况：挥手只用了三个报文。' },
        { row: { c: 'TIME_WAIT', s: 'CLOSED', dir: 'cs', seg: 'ACK ack=' + (Y + 2), chg: 's' }, msg: '客户端确认，服务器 CLOSED，客户端进入 TIME_WAIT。' }
      ]))
    }, {
      label: '端口没有监听',
      run: make([
        { row: { c: 'CLOSED', s: '（8001 无人监听）' }, msg: '客户端要连接 8001 端口，但服务器上没有程序在监听它。' },
        { row: { c: 'SYN_SENT', s: '（8001 无人监听）', dir: 'cs', seg: 'SYN seq=' + X, chg: 'c' }, msg: '客户端照常发送 SYN。' },
        { row: { c: 'CLOSED', s: '（8001 无人监听）', dir: 'sc', seg: 'RST+ACK ack=' + (X + 1), chg: 'c' }, msg: '服务器的内核回复 RST（重置）。客户端的 connect 立即失败，错误是 Connection refused。这和 UDP 回复 ICMP 端口不可达的作用相同。' }
      ])
    }, {
      label: '未读数据就关闭',
      run: make(HS.concat([
        { row: { c: 'ESTABLISHED', s: 'ESTABLISHED', dir: 'cs', seg: 'seq=' + (X + 1) + ' 数据 12 字节' }, msg: '客户端发送 12 字节的请求。' },
        { row: { c: 'ESTABLISHED', s: 'ESTABLISHED', dir: 'sc', seg: 'ACK ack=' + (X + 13) }, msg: '服务器的内核确认收到。注意，确认是内核做的，服务器程序还没有读取这些数据。' },
        { row: { c: 'CLOSED', s: 'CLOSED', dir: 'sc', seg: 'RST+ACK seq=' + (Y + 1) }, msg: '服务器程序没有读取数据就调用了 close。接收缓冲区中还有没读的数据，说明对方的请求没有被处理，内核不发 FIN，而是发 RST，直接中止连接。客户端随后的 recv 得到 Connection reset by peer。' }
      ]))
    }]
  });
})();
