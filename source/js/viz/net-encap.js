/* 《分层模型与封装》：数据从应用层向下逐层加上头部，经过路由器，再在对方逐层拆开 */
(function () {
  'use strict';
  var Y = 92, H = 40, RIGHT = 700;
  var LAYERS = {
    eth: { name: '以太网', len: 14 },
    ip: { name: 'IP', len: 20 },
    tcp: { name: 'TCP', len: 32 },
    data: { name: 'HTTP 数据', len: 74 }
  };

  // 模型：parts 是从左到右的各部分，例如 ['eth', 'ip', 'tcp', 'data']
  function view(m, x) {
    var total = m.parts.reduce(function (s, p) { return s + m.len[p]; }, 0);
    // 按字节数成比例画宽度；数据很长时缩小比例，每部分至少 56 个单位宽，保证文字放得下
    var scale = Math.min(4.4, 440 / total);
    var widths = m.parts.map(function (p) { return Math.max(56, m.len[p] * scale); });
    var left = RIGHT - widths.reduce(function (s, w) { return s + w; }, 0);
    var boxes = [], at = left, hl = x.hl || {};
    m.parts.forEach(function (p, i) {
      var w = widths[i];
      boxes.push({ id: p, x: at, y: Y, w: w, h: H, text: LAYERS[p].name, sub: m.len[p] + ' 字节', hl: hl[p] });
      at += w;
    });
    var texts = [
      { id: 'where', x: 20, y: 30, text: m.where, cls: 'muted' },
      { id: 'total', x: RIGHT, y: Y - 14, text: '共 ' + total + ' 字节', cls: 'muted', anchor: 'end' }
    ];
    return { kind: 'free', boxes: boxes, arrows: [], texts: texts, vars: x.vars };
  }

  function init() {
    return { parts: ['data'], len: { eth: 14, ip: 20, tcp: 32, data: 74 }, where: '发送方 172.17.0.3' };
  }

  DSViz.register('net-encap', {
    title: '封装与解封装：一个 HTTP 请求的旅程',
    height: 210,
    init: init,
    view: view,
    start: function (m, rec) {
      rec.step(0, '数据从应用层出发。点「运行」逐步观察封装和解封装；也可以改变 HTTP 数据的长度。', { hl: { data: 'cur' }, vars: { '所在层': '应用层' } });
    },
    actions: [{
      label: '运行',
      fields: [{ key: 'n', label: 'HTTP 数据（字节）', value: 74, size: 5 }],
      check: function (m, p) { return typeof p.n === 'number' && p.n >= 1 && p.n <= 1448 ? null : '取 1 到 1448（一个以太网帧最多能装下的 TCP 数据）'; },
      run: function (m, p, rec) {
        var f = init(); Object.keys(f).forEach(function (k) { m[k] = f[k]; });
        m.len.data = p.n;
        rec.step(0, 'curl 要发送的 HTTP 请求有 ' + p.n + ' 字节：GET / HTTP/1.1、Host、User-Agent、Accept 几行。应用层只关心这些内容，不关心它们怎样到达对方。', { hl: { data: 'cur' }, vars: { '所在层': '应用层' } });
        m.parts.unshift('tcp');
        rec.step(0, '传输层（TCP）加上 32 字节的头部：源端口 49100、目的端口 80、序号、确认号、窗口等。端口决定了数据交给对方的哪个程序。这个整体叫作 TCP 段（segment）。', { hl: { tcp: 'new' }, vars: { '所在层': '传输层' } });
        m.parts.unshift('ip');
        rec.step(0, '网络层（IP）加上 20 字节的头部：源 IP 172.17.0.3、目的 IP 172.66.147.243、TTL 64、协议号 6（表示里面是 TCP）。IP 地址决定了数据送到哪台主机。这个整体叫作 IP 数据报（packet）。', { hl: { ip: 'new' }, vars: { '所在层': '网络层' } });
        m.parts.unshift('eth');
        rec.step(0, '链路层（以太网）加上 14 字节的头部：目的 MAC、源 MAC、类型 0x0800（表示里面是 IPv4）。注意目的 MAC 不是 example.com 的，而是本网络出口（网关 172.17.0.1）的。这个整体叫作帧（frame）。', { hl: { eth: 'new' }, vars: { '所在层': '链路层' } });
        rec.step(0, '帧被发送到线路上。每一层的头部都只为本层服务，下层把上层交来的全部内容当作「数据」，不去理会里面是什么。', { hl: { eth: 'cur', ip: 'cur', tcp: 'cur', data: 'cur' }, vars: { '所在层': '物理层：线路上' } });
        m.where = '路由器（网关 172.17.0.1）';
        rec.step(0, '网关收到帧，检查以太网头中的目的 MAC 是自己，拆掉以太网头。', { hl: { eth: 'del' }, vars: { '所在层': '链路层' } });
        m.parts.shift();
        rec.step(0, '网关只看 IP 头：目的地址不是自己，于是查路由表决定下一跳，并把 TTL 减 1（64 → 63）。它不会看 TCP 头和 HTTP 数据。', { hl: { ip: 'cur' }, vars: { '所在层': '网络层', TTL: 63 } });
        m.parts.unshift('eth');
        rec.step(0, '网关加上一个新的以太网头（源 MAC 是网关自己，目的 MAC 是下一跳），发往下一段线路。每经过一个路由器，以太网头都要换一次，IP 地址则从头到尾不变。', { hl: { eth: 'new' }, vars: { '所在层': '链路层', TTL: 63 } });
        m.where = '接收方 example.com（172.66.147.243）';
        rec.step(0, '经过若干个路由器后，帧到达了 example.com 的服务器。', { hl: { eth: 'cur', ip: 'cur', tcp: 'cur', data: 'cur' }, vars: { '所在层': '物理层：线路上' } });
        rec.step(0, '链路层：目的 MAC 是自己，类型是 IPv4，拆掉以太网头，交给 IP。', { hl: { eth: 'del' }, vars: { '所在层': '链路层' } });
        m.parts.shift();
        rec.step(0, '网络层：目的 IP 是自己，协议号是 6，拆掉 IP 头，交给 TCP。', { hl: { ip: 'del' }, vars: { '所在层': '网络层' } });
        m.parts.shift();
        rec.step(0, '传输层：根据目的端口 80，找到正在监听 80 端口的 Web 服务器程序，拆掉 TCP 头，把数据交给它。', { hl: { tcp: 'del' }, vars: { '所在层': '传输层' } });
        m.parts.shift();
        rec.step(0, 'Web 服务器拿到的，正是 curl 发出的那 ' + p.n + ' 字节，一个字节都没有变。每一层的头部都被对方的同一层拆掉，这就是「对等层通信」。', { hl: { data: 'ok' }, vars: { '所在层': '应用层' } });
      }
    }]
  });
})();
