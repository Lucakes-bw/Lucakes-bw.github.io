/* 《HTTPS 与 TLS》：TLS 1.3 和 TLS 1.2 的握手对比。1.3 的时间取自真实抓包（projects/net-pkts/tls.pcap） */
(function () {
  'use strict';
  var LX = 20, RX = 470, BW = 180, BH = 26, TOP = 52, ROW = 31;

  // rows: [{ c: 客户端此时知道什么, s: 服务器, dir, seg 报文, enc 是否加密, t 时间 }]
  function view(m, x) {
    var boxes = [], arrows = [], texts = [], last = m.rows.length - 1;
    m.rows.forEach(function (r, i) {
      var y = TOP + i * ROW, cur = i === last;
      boxes.push({ id: 'c' + i, x: LX, y: y, w: BW, h: BH, text: r.c, hl: cur && r.dir === 'sc' ? 'new' : null });
      boxes.push({ id: 's' + i, x: RX, y: y, w: BW, h: BH, text: r.s, hl: cur && r.dir === 'cs' ? 'new' : null });
      if (r.dir) {
        arrows.push({ from: r.dir === 'cs' ? 'c' + i : 's' + i, to: r.dir === 'cs' ? 's' + i : 'c' + i,
          label: (r.enc ? '🔒 ' : '') + r.seg, hl: cur ? 'cur' : null,
          pkt: cur ? (r.enc ? '🔒' : r.seg.split(/[ +（]/)[0]) : null, pktHl: r.enc ? 'warn' : r.dir === 'sc' ? 'alt' : null });
      }
      if (r.t != null) texts.push({ id: 't' + i, x: RX + BW + 8, y: y + 18, text: r.t + ' ms', cls: 'muted' });
    });
    texts.push({ id: 'hc', x: LX, y: 32, text: '客户端 curl', cls: 'muted' });
    texts.push({ id: 'hs', x: RX, y: 32, text: '服务器 example.com:443', cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function make(steps) {
    return function (m, p, rec) {
      m.rows = [];
      var rtt = 0;
      steps.forEach(function (st) {
        if (st.row) m.rows.push(st.row);
        if (st.rtt) rtt = st.rtt;
        rec.step(0, st.msg, { vars: Object.assign({ '往返': rtt }, st.vars || {}) });
      });
    };
  }

  var TCP = [
    { row: { c: '', s: '监听 443', dir: 'cs', seg: 'SYN', t: 0 }, rtt: 0.5, msg: 'TLS 跑在 TCP 之上，所以先要三次握手。' },
    { row: { c: '', s: '', dir: 'sc', seg: 'SYN+ACK', t: 65 }, rtt: 1, msg: '65 毫秒后收到 SYN+ACK，这是一个往返时间。' },
    { row: { c: 'TCP 已连接', s: '', dir: 'cs', seg: 'ACK', t: 65 }, msg: '客户端回复 ACK，TCP 连接建立，花了 1 个往返。' }
  ];

  DSViz.register('tls-hs', {
    title: 'TLS 握手：1.3 和 1.2 各要几个往返',
    height: 400,
    init: function () { return { rows: [] }; },
    view: view,
    start: function (m, rec) { rec.step(0, '选择一个版本，逐步查看握手。带 🔒 的报文是加密的，途中的人只能看到它的长度。'); },
    actions: [{
      label: 'TLS 1.3（实测）',
      run: make(TCP.concat([
        { row: { c: '生成 x25519 密钥对', s: '', dir: 'cs', seg: 'Client Hello（公开值、SNI）', t: 68 }, rtt: 1.5,
          msg: 'Client Hello（517 字节）：支持的版本和算法、SNI = example.com、ALPN，以及关键的 key_share：客户端「猜」服务器会选 x25519，直接把自己的公开值发了过去。' },
        { row: { c: '', s: '算出握手密钥', dir: 'sc', seg: 'Server Hello（公开值）', t: 134 }, rtt: 2,
          msg: 'Server Hello：服务器选定 TLS 1.3 和 x25519，附上自己的公开值。现在双方各有对方的公开值，用 ECDHE 各自算出相同的共享密钥，这个密钥从未在网上传过。' },
        { row: { c: '算出握手密钥', s: '', dir: 'sc', seg: '证书、签名、Finished', enc: true, t: 134 },
          msg: '服务器在同一批报文中接着发出证书、对握手内容的签名（证明它持有证书的私钥）和 Finished。它们已经用握手密钥加密了，所以 tshark 只能显示 Application Data。' },
        { row: { c: '验证证书 ✓', s: '', dir: 'cs', seg: 'Finished + HTTP 请求', enc: true, t: 139 }, rtt: 2.5,
          msg: '客户端验证证书链和签名，发出自己的 Finished，紧接着就发出加密的 HTTP 请求，不再等待。从 Client Hello 到能发请求，TLS 1.3 只用了 1 个往返。' },
        { row: { c: '', s: '', dir: 'sc', seg: 'HTTP 响应', enc: true, t: 203 }, rtt: 3,
          msg: '203 毫秒，收到加密的 HTTP 响应。TCP 1 个往返 + TLS 1 个往返 + 请求 1 个往返 = 3 个往返。', vars: { '结论': 'TLS 握手 1 RTT' } }
      ]))
    }, {
      label: 'TLS 1.2（示意）',
      run: make(TCP.map(function (s) { var c = JSON.parse(JSON.stringify(s)); delete c.row.t; return c; }).concat([
        { row: { c: '', s: '', dir: 'cs', seg: 'Client Hello' }, rtt: 1.5, msg: 'TLS 1.2 的 Client Hello 只列出支持的算法，不带密钥交换的公开值：客户端还不知道服务器会选哪种。' },
        { row: { c: '', s: '', dir: 'sc', seg: 'Server Hello + 证书 + 公开值' }, rtt: 2, msg: '服务器选定算法，发回证书和它的公开值（ServerKeyExchange）。这些都是明文，途中的人能看到证书。' },
        { row: { c: '算出密钥', s: '', dir: 'cs', seg: '公开值 + Finished' }, rtt: 2.5, msg: '客户端这时才发出自己的公开值（ClientKeyExchange）和 Finished。' },
        { row: { c: '', s: '算出密钥', dir: 'sc', seg: 'Finished', enc: true }, rtt: 3, msg: '服务器回复 Finished。到这里 TLS 1.2 用了 2 个往返。' },
        { row: { c: '', s: '', dir: 'cs', seg: 'HTTP 请求', enc: true }, rtt: 3.5, msg: '然后才能发出 HTTP 请求。' },
        { row: { c: '', s: '', dir: 'sc', seg: 'HTTP 响应', enc: true }, rtt: 4, msg: '共 4 个往返，比 TLS 1.3 多一个。TLS 1.3 的改进就是让客户端在第一条消息中就带上公开值。', vars: { '结论': 'TLS 握手 2 RTT' } }
      ]))
    }]
  });
})();
