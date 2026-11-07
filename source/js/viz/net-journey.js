/* 《HTTP》：curl http://example.com/ 的全过程 —— DNS、TCP 握手、请求和响应、关闭，每个包都在图上飞一遍
   时间取自真实抓包（projects/net-pkts/ 下的 dns.pcap 和 http.pcap，分两次抓取） */
(function () {
  'use strict';
  var B = {
    pc: { x: 10, y: 120, w: 140, text: '你的电脑', sub: '172.17.0.3' },
    gw: { x: 260, y: 120, w: 130, text: '网关（NAT）', sub: '' },
    dns: { x: 530, y: 30, w: 180, text: 'DNS 1.1.1.1', sub: '' },
    srv: { x: 530, y: 210, w: 180, text: 'example.com', sub: '172.66.147.243:80' }
  };
  var DNS_MS = 67;   // DNS 查询耗时，HTTP 部分的时间加上它

  // 一个包从 a 经网关到 b：两段箭头，第二段延迟出发
  function route(a, b) { return a === 'pc' ? [['pc', 'gw'], ['gw', b]] : [[a, 'gw'], ['gw', 'pc']]; }

  function view(m, x) {
    var boxes = [], arrows = [], texts = [], hl = {};
    if (x.go) { hl[x.go[0]] = 'cur'; hl[x.go[1]] = 'cur'; hl.gw = 'cmp'; }
    Object.keys(B).forEach(function (k) {
      var b = B[k];
      boxes.push({ id: k, x: b.x, y: b.y, w: b.w, h: 44, text: b.text, sub: b.sub, hl: hl[k] || (m.done[k] ? null : 'dim') });
    });
    if (x.go) {
      var back = x.go[0] !== 'pc';
      route(x.go[0], x.go[1]).forEach(function (seg, i) {
        arrows.push({ from: seg[0], to: seg[1], hl: 'cur', label: i === 0 ? x.size : null, curve: back ? 14 : -14,
          pkt: x.tag, pktHl: x.warn ? 'warn' : back ? 'alt' : null, pktDelay: i * 380, pktDur: 380 });
      });
    }
    texts.push({ id: 'lh', x: 10, y: 298, text: '抓到的包（时间 · 方向 · 内容）', cls: 'muted' });
    m.log.slice(-5).forEach(function (l, i) { texts.push({ id: 'l' + (m.log.length - Math.min(5, m.log.length) + i), x: 10, y: 318 + i * 18, text: l }); });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function P(m, rec, from, to, ms, tag, size, line, msg, phase, extra) {
    m.done[from] = m.done[to] = true;
    m.log.push(('     ' + ms).slice(-5) + ' ms  ' + (from === 'pc' ? '→ ' : '← ') + line);
    rec.step(0, msg, Object.assign({ go: [from, to], tag: tag, size: size, vars: { '阶段': phase, '时间': ms + ' ms' } }, extra || {}));
  }

  DSViz.register('web-journey', {
    title: '访问 http://example.com/：一次请求的全部报文',
    height: 410,
    init: function () { return { done: { pc: true }, log: [] }; },
    view: view,
    start: function (m, rec) {
      rec.step(0, '点「开始」，逐个查看 curl http://example.com/ 发出和收到的每一个包。时间取自真实的抓包，从发出 DNS 查询算起。', {});
    },
    actions: [{
      label: '开始',
      run: function (m, p, rec) {
        m.done = { pc: true }; m.log = [];
        B.gw.sub = '';
        rec.step(0, 'curl 只知道名字 example.com。要发 IP 包，必须先知道它的 IP 地址。', { vars: { '阶段': '准备' } });
        P(m, rec, 'pc', 'dns', 0, 'DNS 查询', 'UDP 71 B', 'DNS 查询 A example.com',
          '第 1 个包：向 DNS 服务器 1.1.1.1 的 53 端口发一个 UDP 数据报，问 example.com 的 A 记录。它先交给网关：目的 IP 是 1.1.1.1，目的 MAC 却是网关的。网关做 NAT，把源地址 172.17.0.3 换成自己的公网地址。', 'DNS');
        P(m, rec, 'dns', 'pc', 67, 'DNS 回答', 'UDP 103 B', 'DNS 回答 104.20.23.154, 172.66.147.243',
          '67 毫秒后回答到达：两个 A 记录。UDP 没有握手，一来一回就完成了。curl 选用 172.66.147.243。', 'DNS');
        P(m, rec, 'pc', 'srv', DNS_MS + 0, 'SYN', 'TCP 74 B', 'SYN  seq=947241771',
          '开始 TCP 三次握手。SYN 中带着随机的初始序号，以及 MSS 1460、SACK、时间戳、窗口扩大 7 这几个选项。', 'TCP 握手');
        P(m, rec, 'srv', 'pc', DNS_MS + 77, 'SYN+ACK', 'TCP 74 B', 'SYN+ACK  ack=947241772',
          '77 毫秒后服务器回复 SYN+ACK：确认号是我们的序号 + 1，同时带上它自己的初始序号。这 77 毫秒就是一个往返时间（RTT）。', 'TCP 握手');
        P(m, rec, 'pc', 'srv', DNS_MS + 77, 'ACK', 'TCP 66 B', 'ACK',
          '确认服务器的 SYN。对客户端来说连接已经建立，不用等这个 ACK 到达，马上就可以发数据。', 'TCP 握手');
        P(m, rec, 'pc', 'srv', DNS_MS + 77, 'GET /', 'TCP 140 B', 'GET / HTTP/1.1  （74 字节）',
          '紧接着（0.3 毫秒后）发出 HTTP 请求：74 字节的纯文本，请求行加三个头部 Host、User-Agent、Accept。', 'HTTP');
        P(m, rec, 'srv', 'pc', DNS_MS + 79, 'ACK', 'TCP 66 B', 'ACK  ack=75',
          '服务器的内核先确认收到了这 74 字节（ack 相对序号 75）。这是 TCP 的确认，还不是 HTTP 的响应。', 'HTTP');
        P(m, rec, 'srv', 'pc', DNS_MS + 151, '200 OK', 'TCP 994 B', 'HTTP/1.1 200 OK  （928 + 5 字节，分两个报文）',
          '又过了 74 毫秒，大约一个往返时间，响应到达：状态行、头部和网页，分成 928 字节和 5 字节两个 TCP 报文。客户端每收到一个都回一个 ACK。', 'HTTP');
        P(m, rec, 'pc', 'srv', DNS_MS + 152, 'FIN', 'TCP 66 B', 'FIN',
          'curl 收完了网页，主动关闭连接：发送 FIN。', '关闭');
        P(m, rec, 'srv', 'pc', DNS_MS + 217, 'FIN', 'TCP 66 B', 'ACK，然后 FIN',
          '服务器先确认，再发出自己的 FIN。', '关闭');
        P(m, rec, 'pc', 'srv', DNS_MS + 217, 'ACK', 'TCP 66 B', 'ACK',
          '客户端确认服务器的 FIN，进入 TIME_WAIT。连接结束。', '关闭');
        rec.step(0, '一共约 ' + (DNS_MS + 217) + ' 毫秒、15 个包，其中真正的网页只有一个来回；其余是 DNS（1 个 RTT）、TCP 握手（1 个 RTT）和关闭。这就是为什么 HTTP 要复用连接，以及 HTTP/3 要把握手和加密合并。',
          { vars: { '阶段': '完成', '时间': (DNS_MS + 217) + ' ms', '往返': '约 4 个' } });
      }
    }]
  });
})();
