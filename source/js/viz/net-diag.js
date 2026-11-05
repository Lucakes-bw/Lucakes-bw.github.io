/* 《网络排障》一章的逐步演示
 *
 * nd-dead   对端死了：不发数据时永远不知道；keepalive 探测；发送时靠重传超时；TCP_USER_TIMEOUT
 * nd-bdp    带宽时延积：窗口不够大，管道就装不满
 * nd-pmtu   MTU 黑洞：大包被丢弃，「需要分片」的 ICMP 又被防火墙挡住
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- 对端死了 ---------- */

  function deadView(m, x) {
    var boxes = [], texts = [], arrows = [], hl = x.hl || {};
    boxes.push(box('cli', 20, 30, 150, 40, '客户端', hl.cli || null, x.cliSub || '连接 ESTABLISHED'));
    boxes.push(box('net', 245, 30, 150, 40, x.cut ? '网络断了' : '网络', x.cut ? 'warn' : null, x.cut ? '包被悄悄丢弃' : null));
    boxes.push(box('srv', 470, 30, 150, 40, '服务器', x.cut ? 'dim' : null, x.cut ? '死机 / 断电' : null));
    arrows.push({ from: 'cli', to: 'net', hl: hl.a1 });
    arrows.push({ from: 'net', to: 'srv', hl: x.cut ? null : hl.a2 });
    texts.push(txt('lg', 20, 112, '时间线（从网络断开算起）', 'muted'));
    (x.log || []).forEach(function (e, i) {
      texts.push(txt('t' + i, 20, 136 + i * 22, e[0], 'muted'));
      texts.push(txt('e' + i, 110, 136 + i * 22, e[1], i === x.log.length - 1 ? null : 'muted'));
    });
    return { kind: 'free', boxes: boxes, texts: texts, arrows: arrows, vars: x.vars || {} };
  }

  function deadRun(mode, rec) {
    var log = [];
    function step(line, t, ev, msg, extra) {
      log.push([t, ev]);
      rec.step(line, msg, Object.assign({ cut: true, log: log.slice() }, extra || {}));
    }
    rec.step(1, '连接已建立。网络突然断了（或者服务器死机、断电），没有任何包告诉客户端。', { cut: true, log: [] });
    if (mode === 'idle') {
      step(2, '0 秒', '客户端阻塞在 recv，等对方发数据', '客户端不发送任何数据。TCP 不会主动发包，也就永远不知道对方已经不在了。');
      step(2, '1 小时', '仍然阻塞在 recv', '一小时、一天以后还是这样。服务器那边如果还活着，它也会一直保留这个连接：这就是半开连接。', { cliSub: '仍然 ESTABLISHED' });
    } else if (mode === 'ka') {
      step(3, '10 秒', '空闲 10 秒，发出第 1 个 keepalive 探测', 'TCP_KEEPIDLE = 10：连接空闲 10 秒后，内核发一个不带数据的探测包。', { hl: { a1: 'cur' } });
      step(3, '15 秒', '没有回应，第 2 个探测', 'TCP_KEEPINTVL = 5：没有回应，5 秒后再探测。', { hl: { a1: 'cur' } });
      step(3, '20 秒', '没有回应，第 3 个探测', '仍然没有回应，再发第 3 个探测。', { hl: { a1: 'cur' } });
      step(4, '25 秒', 'recv 返回 -1：Connection timed out', 'TCP_KEEPCNT = 3：连续 3 个探测都没有回应，内核关闭连接，recv 返回 ETIMEDOUT。（实测 25.4 秒）', { hl: { cli: 'warn' }, cliSub: '连接已关闭' });
    } else if (mode === 'send') {
      var t = 0, rto = 0.2;
      step(5, '0 秒', 'send 成功：数据进了发送缓冲区', 'send 只是把数据放进内核的发送缓冲区，立即返回成功。内核发出数据，等待确认。', { hl: { a1: 'cur' } });
      for (var i = 1; i <= 6; i++) {
        t += rto;
        step(5, t.toFixed(1) + ' 秒', '第 ' + i + ' 次重传，下一次等 ' + (rto * 2).toFixed(1) + ' 秒', '没有确认，超时重传，每次的等待时间（RTO）翻倍。', { hl: { a1: 'cur' } });
        rto *= 2;
      }
      step(5, '……', '最多 120 秒一次，共 15 次（tcp_retries2）', 'RTO 最大 120 秒。重传 15 次以后放弃，加起来约 15 分钟。这期间程序的 send 仍然成功，直到发送缓冲区满了才阻塞。', {});
      step(4, '约 15 分钟', 'send 返回 -1：Connection timed out', '连接被关闭。之后的 send、recv 返回 ETIMEDOUT。', { hl: { cli: 'warn' }, cliSub: '连接已关闭' });
    } else {
      step(5, '0 秒', 'send 成功，内核开始重传', '同样在重传，但设置了 TCP_USER_TIMEOUT = 10000 毫秒。', { hl: { a1: 'cur' } });
      step(6, '约 10 秒', 'send 返回 -1：Connection timed out', '已发出的数据超过 10 秒没有得到确认，内核直接关闭连接，不再等满 15 次重传。（实测约 11 秒）', { hl: { cli: 'warn' }, cliSub: '连接已关闭' });
    }
  }

  DSViz.register('nd-dead', {
    title: '对端死了：客户端多久才能发现',
    height: 330,
    width: 640,
    code: [
      '网络断开 / 对端死机，没有 FIN 也没有 RST',
      '不发数据：什么也不会发生',
      'keepalive：空闲一段时间后发探测包',
      '探测都没有回应：关闭连接，返回 ETIMEDOUT',
      '有数据要发：超时重传，RTO 翻倍',
      'TCP_USER_TIMEOUT：数据多久没被确认就放弃'
    ],
    init: function () { return {}; },
    view: deadView,
    start: function (m, rec) { deadRun('ka', rec); },
    actions: [
      { label: '空闲', run: function (m, p, rec) { deadRun('idle', rec); } },
      { label: '空闲 + keepalive', run: function (m, p, rec) { deadRun('ka', rec); } },
      { label: '一直在发送', run: function (m, p, rec) { deadRun('send', rec); } },
      { label: '发送 + USER_TIMEOUT', run: function (m, p, rec) { deadRun('ut', rec); } }
    ]
  });

  /* ---------- 带宽时延积 ---------- */

  // 单程 5 格：数据走 5 格到接收方，确认再走 5 格回来；往返 10 格。每格最多发一个包
  var HOP = 5;

  function bdpInit() { return {}; }

  function bdpView(m, x) {
    var boxes = [], texts = [], s = x.s;
    texts.push(txt('w', 20, 20, '窗口 ' + s.win + ' 个包；管道每个方向 ' + HOP + ' 格（往返 ' + 2 * HOP + ' 格）', 'muted'));
    boxes.push(box('snd', 20, 40, 90, 100, '发送方', null, '在途 ' + s.inflight.length));
    boxes.push(box('rcv', 530, 40, 90, 100, '接收方', null, '已收 ' + s.delivered));
    for (var i = 0; i < HOP; i++) {
      var d = s.fwd[i], a = s.back[i];
      boxes.push(box('f' + i, 125 + i * 80, 46, 70, 34, d != null ? '数据 ' + d : '', d != null ? 'cur' : 'dim'));
      boxes.push(box('b' + i, 125 + i * 80, 100, 70, 34, a != null ? '确认 ' + a : '', a != null ? 'ok' : 'dim'));
    }
    texts.push(txt('l1', 125, 38, '→ 数据', 'muted'));
    texts.push(txt('l2', 125, 150, '← 确认', 'muted'));
    texts.push(txt('st', 20, 180, '第 ' + s.tick + ' 格时间：共收到 ' + s.delivered + ' 个包；吞吐量 ≈ ' + (s.tick ? (s.delivered / s.tick).toFixed(2) : '0') + ' 包/格（上限 1）', null));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function bdpRun(win, rec) {
    var s = { win: win, fwd: [], back: [], inflight: [], next: 1, delivered: 0, tick: 0 };
    for (var i = 0; i < HOP; i++) { s.fwd.push(null); s.back.push(null); }
    function snap(line, msg) { rec.step(line, msg, { s: JSON.parse(JSON.stringify(s)), vars: { 窗口: win, 时间: s.tick } }); }
    snap(1, '发送方最多可以有 ' + win + ' 个包在途（发出了但还没确认）。管道一个来回是 10 格时间，要一直有包在发，窗口至少要 10 个包：这就是带宽时延积。');
    for (var t = 1; t <= 22; t++) {
      s.tick = t;
      var arrivedAck = s.back[0];
      for (var k = 0; k < HOP - 1; k++) s.back[k] = s.back[k + 1];
      var arrivedData = s.fwd[HOP - 1];
      for (var j = HOP - 1; j > 0; j--) s.fwd[j] = s.fwd[j - 1];
      s.fwd[0] = null;
      s.back[HOP - 1] = null;
      if (arrivedAck != null) s.inflight.splice(s.inflight.indexOf(arrivedAck), 1);
      if (arrivedData != null) { s.delivered++; s.back[HOP - 1] = arrivedData; }
      var sent = false;
      if (s.inflight.length < win) { s.fwd[0] = s.next; s.inflight.push(s.next); s.next++; sent = true; }
      if (t <= 12 || t === 22) snap(sent ? 2 : 3, sent ? '发出数据 ' + (s.next - 1) + '。' + (arrivedAck != null ? '确认 ' + arrivedAck + ' 回来了，窗口腾出一个位置。' : '')
        : '在途的包已经有 ' + win + ' 个，窗口满了，只能等确认回来。管道中有空着的格子：带宽被浪费了。');
    }
    snap(4, win >= 2 * HOP ? '窗口不小于带宽时延积，每一格时间都在发送，吞吐量接近上限。' : '窗口只有带宽时延积的 ' + Math.round(win * 100 / (2 * HOP)) + '%，吞吐量也只有上限的这么多：每个往返时间最多送达一个窗口的数据。');
  }

  DSViz.register('nd-bdp', {
    title: '带宽时延积：窗口不够大，管道就装不满',
    height: 200,
    width: 640,
    code: [
      '设定窗口：最多多少数据在途',
      '在途的数据少于窗口：发送',
      '窗口满了：等确认回来',
      '吞吐量 ≈ 窗口 ÷ 往返时间'
    ],
    init: bdpInit,
    view: bdpView,
    start: function (m, rec) { bdpRun(4, rec); },
    actions: [
      { label: '窗口 4', run: function (m, p, rec) { bdpRun(4, rec); } },
      { label: '窗口 10', run: function (m, p, rec) { bdpRun(10, rec); } }
    ]
  });

  /* ---------- MTU 黑洞 ---------- */

  function pmtuView(m, x) {
    var boxes = [], texts = [], arrows = [], hl = x.hl || {};
    boxes.push(box('c', 20, 40, 120, 44, '客户端 c', hl.c));
    boxes.push(box('r', 260, 40, 120, 44, '路由器 r', hl.r, '到 c 的链路 MTU 1400'));
    boxes.push(box('s', 500, 40, 120, 44, '服务器 s', hl.s, x.pmtu ? '路径 MTU ' + x.pmtu : 'MTU 1500'));
    arrows.push({ from: 'r', to: 'c', hl: hl.rc, label: hl.rcl });
    arrows.push({ from: 's', to: 'r', hl: hl.sr, label: hl.srl });
    if (x.icmp) boxes.push(box('icmp', 230, 110, 240, 30, x.icmp, x.icmpBlocked ? 'warn' : 'new'));
    if (x.fw) boxes.push(box('fw', 500, 110, 120, 30, '丢弃 ICMP', 'warn', null));
    (x.log || []).forEach(function (e, i) { texts.push(txt('l' + i, 20, 172 + i * 20, e, i === x.log.length - 1 ? null : 'muted')); });
    return { kind: 'free', boxes: boxes, texts: texts, arrows: arrows, vars: x.vars || {} };
  }

  function pmtuRun(mode, rec) {
    var log = [];
    function st(line, ev, msg, extra) { log.push(ev); rec.step(line, msg, Object.assign({ log: log.slice(), fw: mode === 'black' }, extra || {})); }
    st(1, '握手、请求：都是小包，正常', '三次握手和请求都是小包，顺利通过。双方的 MSS 都是 1460（按自己的网卡 MTU 1500 计算）。', { hl: { c: 'ok', s: 'ok' } });
    if (mode === 'clamp') {
      st(5, '路由器把 SYN 中的 MSS 改成 1360', '路由器上设置了 MSS 钳制：转发 SYN 时，把其中的 MSS 改成 1400 − 40 = 1360。服务器以为客户端只能收 1360 字节的段。', { hl: { r: 'cur' } });
      st(5, '服务器只发 1400 字节的包，顺利通过', '服务器从一开始就不会发出超过 1400 字节的包，根本不需要 ICMP。', { hl: { sr: 'ok', rc: 'ok', srl: '1400', rcl: '1400', c: 'ok' } });
      return;
    }
    st(2, '服务器发出 1500 字节的包，带 DF 标志', 'TCP 的包都带「不要分片」（DF）标志。响应很大，服务器按 MSS 1460 发出 1500 字节的包。', { hl: { sr: 'cur', srl: '1500 DF' } });
    st(3, '路由器：1500 > 1400，又不许分片，丢弃', '路由器到客户端的链路 MTU 只有 1400，这个包放不下，又不许分片，只能丢弃。', { hl: { r: 'warn' } });
    if (mode === 'ok') {
      st(3, '路由器回复 ICMP「需要分片，MTU 1400」', '路由器向服务器发一个 ICMP 消息：需要分片，下一跳的 MTU 是 1400。', { icmp: 'ICMP 需要分片（MTU 1400）', hl: { r: 'cur' } });
      st(4, '服务器把这条路径的 MTU 记为 1400，重发', '服务器收到后，记住到客户端的路径 MTU 是 1400，用更小的段重发。这就是路径 MTU 发现（PMTUD）。', { pmtu: 1400, hl: { sr: 'ok', rc: 'ok', srl: '1400', rcl: '1400', s: 'cur' } });
      st(4, '响应顺利送达', '之后的包都不超过 1400 字节，传输正常。', { pmtu: 1400, hl: { c: 'ok' } });
    } else {
      st(3, '路由器回复 ICMP……被防火墙丢弃', '路由器发出了 ICMP，但服务器前面的防火墙「为了安全」丢弃了所有 ICMP。服务器什么也没收到。', { icmp: 'ICMP 需要分片（被丢弃）', icmpBlocked: true, hl: { r: 'cur' } });
      st(2, '服务器超时，重传同样 1500 字节的包', '服务器只知道没有确认，以为是普通的丢包，重传同样大小的包，又被丢弃……', { hl: { sr: 'warn', srl: '1500 DF', s: 'warn' } });
      st(3, '……一直重传，直到超时', '小包（握手、请求、确认）都正常，只有大的响应过不去：连接建立了，请求发出去了，然后卡住。这就是 MTU 黑洞。', { hl: { c: 'warn', s: 'warn' } });
    }
  }

  DSViz.register('nd-pmtu', {
    title: 'MTU 黑洞：大包过不去，ICMP 又回不来',
    height: 300,
    width: 640,
    code: [
      '握手和请求（小包）',
      '服务器按 MSS 发出大包，带 DF',
      '路由器：包比下一跳的 MTU 大，丢弃并回复 ICMP',
      '服务器收到 ICMP：降低路径 MTU，重发',
      'MSS 钳制：握手时就把 MSS 改小'
    ],
    init: function () { return {}; },
    view: pmtuView,
    start: function (m, rec) { pmtuRun('ok', rec); },
    actions: [
      { label: 'ICMP 正常', run: function (m, p, rec) { pmtuRun('ok', rec); } },
      { label: 'ICMP 被挡住', run: function (m, p, rec) { pmtuRun('black', rec); } },
      { label: 'MSS 钳制', run: function (m, p, rec) { pmtuRun('clamp', rec); } }
    ]
  });
})();
