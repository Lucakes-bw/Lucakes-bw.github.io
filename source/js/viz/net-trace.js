/* 计算机网络《路由与 ICMP》：traceroute 一跳一跳地探测（traceroute）
   h1 —— r1 —— r2 —— h2，探测包是一个写着 TTL 的小方块，走到哪里就画在哪里 */
(function () {
  'use strict';
  var NODES = [
    { id: 'h1', x: 30, label: 'h1', ip: '10.0.1.10' },
    { id: 'r1', x: 210, label: 'r1', ip: '10.0.1.1' },
    { id: 'r2', x: 390, label: 'r2', ip: '10.0.12.2' },
    { id: 'h2', x: 570, label: 'h2', ip: '10.0.2.10' }
  ];
  var W = 120, Y = 60;

  function view(m, x) {
    var boxes = [], arrows = [], texts = [];
    NODES.forEach(function (n, i) {
      boxes.push({ id: n.id, x: n.x, y: Y, w: W, h: 40, text: n.label, sub: n.ip, hl: x.at === i ? (x.bad ? 'warn' : 'cur') : (m.found[i] ? 'ok' : null) });
      if (i < 3) arrows.push({ from: n.id, to: NODES[i + 1].id });
    });
    if (x.pkt != null) {
      var px = x.pkt.pos * 180 + (x.pkt.mid ? 135 : 45);
      boxes.push({ id: 'pkt', x: px, y: 134, w: 90, h: 30, text: x.pkt.text, hl: x.pkt.hl || 'new' });
    }
    if (x.reply) {
      boxes.push({ id: 'rep', x: x.reply.x, y: 186, w: 260, h: 30, text: x.reply.text, hl: x.reply.hl || 'cmp' });
    }
    texts.push({ id: 'res', x: 30, y: 250, text: 'traceroute 的输出：' + (m.lines.length ? m.lines.join('　') : '（还没有）'), cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  DSViz.register('traceroute', {
    title: 'traceroute：用递增的 TTL 逐跳探测',
    height: 262,
    init: function () { return { found: [false, false, false, false], lines: [] }; },
    view: view,
    start: function (m, rec) {
      m.found = [true, false, false, false]; m.lines = [];
      rec.step(0, 'h1 要找出到 h2（10.0.2.10）的路径。中间有两台路由器，h1 事先并不知道。', { at: 0 });
      for (var ttl = 1; ttl <= 3; ttl++) {
        rec.step(0, '发一个 TTL = ' + ttl + ' 的 UDP 包，目的端口是不常用的 ' + (33433 + ttl) + '。',
          { at: 0, pkt: { pos: 0, mid: true, text: 'TTL = ' + ttl }, vars: { TTL: ttl } });
        for (var hop = 1; hop < ttl; hop++) {
          rec.step(0, NODES[hop].label + ' 转发这个包，TTL 减 1，变成 ' + (ttl - hop) + '。',
            { at: hop, pkt: { pos: hop, mid: true, text: 'TTL = ' + (ttl - hop) }, vars: { TTL: ttl } });
        }
        if (ttl < 3) {
          rec.step(0, NODES[ttl].label + ' 把 TTL 减到 0，丢弃这个包。', { at: ttl, bad: true, pkt: { pos: ttl, text: 'TTL = 0', hl: 'del' }, vars: { TTL: ttl } });
          m.found[ttl] = true; m.lines.push(ttl + '  ' + NODES[ttl].ip);
          rec.step(0, NODES[ttl].label + ' 发回 ICMP「TTL 超时」，源地址就是它自己：第 ' + ttl + ' 跳是 ' + NODES[ttl].ip + '。',
            { at: ttl, reply: { x: 30, text: '← ICMP 超时，来自 ' + NODES[ttl].ip }, vars: { TTL: ttl } });
        } else {
          rec.step(0, '这次包到达了 h2。h2 上没有程序监听端口 33436。', { at: 3, pkt: { pos: 3, text: 'TTL = 1' }, vars: { TTL: ttl } });
          m.found[3] = true; m.lines.push('3  10.0.2.10');
          rec.step(0, 'h2 回复 ICMP「端口不可达」。traceroute 收到它，就知道已经到达终点。',
            { at: 3, reply: { x: 30, text: '← ICMP 端口不可达，来自 10.0.2.10', hl: 'ok' }, vars: { TTL: ttl } });
        }
      }
      rec.step(0, '三次探测得到了完整的路径。每个超时消息中附带了原来那个包的 IP 头，traceroute 据此知道它回答的是哪一次探测。');
    }
  });
})();
