/* 《流量控制与拥塞控制》：一个往返一根柱子，看 cwnd 在慢启动、拥塞避免、丢包之后怎样变化
   为了让一个锯齿只要几十步，网络容量缩小成了 48 段（文中实测的是约 280 段），规则和真实的算法相同 */
(function () {
  'use strict';
  var CAP = 48, MAXV = 100, X0 = 56, BASE = 250, PH = 200, BW = 10;

  function y(v) { return BASE - PH * Math.min(v, MAXV) / MAXV; }

  function view(m, x) {
    var cols = [], boxes = [], texts = [];
    var w = Math.max(5, Math.min(BW * 2, 640 / Math.max(m.hist.length, 32)));
    m.hist.forEach(function (h, i) {
      var last = i === m.hist.length - 1;
      cols.push({ id: i, x: X0 + i * w + 1, y: y(h.v), w: w - 2, h: BASE - y(h.v), text: last ? h.v : '', hl: h.hl });
    });
    boxes.push({ id: 'cap', x: X0, y: y(CAP), w: 650, h: 1, text: '', hl: 'warn' });
    texts.push({ id: 'capt', x: X0 + 650, y: y(CAP) - 6, text: '网络能容纳 ' + CAP + ' 段', cls: 'muted', anchor: 'end' });
    if (m.ss < MAXV) {
      boxes.push({ id: 'ss', x: X0, y: y(m.ss), w: 650, h: 1, text: '', hl: 'empty' });
      texts.push({ id: 'sst', x: X0 + 650, y: y(m.ss) + 15, text: 'ssthresh = ' + m.ss, cls: 'muted', anchor: 'end' });
    }
    [0, 25, 50, 75, 100].forEach(function (v) { texts.push({ id: 'ax' + v, x: X0 - 8, y: y(v) + 4, text: String(v), cls: 'muted', anchor: 'end' }); });
    texts.push({ id: 'xl', x: X0, y: BASE + 20, text: '每根柱子是一个往返时间（RTT）内的 cwnd（段）', cls: 'muted' });
    return { kind: 'free', cols: cols, boxes: boxes, texts: texts, vars: x.vars };
  }

  // 每个 RTT：记录一根柱子
  function push(m, rec, v, hl, msg, phase) {
    m.hist.push({ v: v, hl: hl });
    rec.step(0, msg, { vars: { 'RTT': m.hist.length, cwnd: v, ssthresh: m.ss >= MAXV ? '∞' : m.ss, '阶段': phase } });
  }

  function slowStart(m, rec, cw, limit) {
    while (true) {
      push(m, rec, cw, 'cur', cw === 10 && m.hist.length === 0 ? '连接刚建立，cwnd = 10（Linux 的初始拥塞窗口）。这一个往返内最多发 10 段。'
        : '慢启动：每收到一个 ACK，cwnd 加 1；一个往返收到 cwnd 个 ACK，所以 cwnd 每个往返翻一倍：' + cw + '。', '慢启动');
      if (cw > CAP) return cw;
      if (cw >= limit) return cw;
      cw = Math.min(cw * 2, limit === Infinity ? cw * 2 : limit);
    }
  }

  DSViz.register('tcp-cwnd', {
    title: '拥塞窗口：慢启动、拥塞避免、快速恢复',
    height: 280,
    init: function () { return { hist: [], ss: MAXV }; },
    view: view,
    start: function (m, rec) { m.hist = []; m.ss = MAXV; rec.step(0, '选择一种算法逐步播放。为了看得清，网络容量缩小成了 48 段（文中实测约 280 段），算法的规则不变。'); },
    actions: [{
      label: 'Reno',
      run: function (m, p, rec) {
        m.hist = []; m.ss = MAXV;
        var cw = slowStart(m, rec, 10, Infinity);
        for (var round = 0; round < 2; round++) {
          m.ss = Math.floor(cw / 2);
          push(m, rec, m.ss, 'del', 'cwnd = ' + cw + ' 超过了网络能容纳的 ' + CAP + ' 段，路由器的队列满了，开始丢包。发送方收到 3 个重复的 ACK，判断丢了包：快速重传，并把 cwnd 和 ssthresh 都减半为 ' + m.ss + '（乘性减）。', '快速恢复');
          cw = m.ss;
          while (cw <= CAP) {
            cw += 1;
            push(m, rec, cw, 'ok', '拥塞避免：cwnd ≥ ssthresh，不再翻倍，每个往返只加 1 段（加性增）：' + cw + '。', '拥塞避免');
          }
        }
        rec.step(0, '这就是 Reno 的锯齿：线性爬升、减半、再爬升。减半之后要很多个往返才能爬回来，期间链路没有被填满，这就是文中 Reno 吞吐量较低的原因。', { vars: { '结论': 'AIMD 锯齿' } });
      }
    }, {
      label: 'CUBIC',
      run: function (m, p, rec) {
        m.hist = []; m.ss = MAXV;
        var cw = slowStart(m, rec, 10, Infinity);
        for (var round = 0; round < 3; round++) {
          var wmax = cw, low = Math.floor(cw * 0.7);
          m.ss = low;
          push(m, rec, low, 'del', '丢包。CUBIC 只减到原来的 70%：' + cw + ' × 0.7 ≈ ' + low + '，比 Reno 的一半温和。它记下丢包时的窗口 Wmax = ' + wmax + '。'
            + (low > CAP ? '慢启动冲得太高，减完还是超过容量，很快会再次丢包。' : ''), '丢包');
          var C = 0.01, K = Math.cbrt((wmax - low) / C), t = 0;
          cw = low;
          while (cw <= CAP && t < 40) {
            t++;
            cw = Math.max(cw, Math.round(wmax + C * Math.pow(t - K, 3)));
            var near = Math.abs(cw - wmax) <= 2;
            push(m, rec, cw, near ? 'cmp' : 'ok', near ? '接近上次丢包时的窗口 Wmax，增长变得很慢，在这里停留，小心试探：' + cw + '。'
              : cw < wmax ? '三次曲线：离 Wmax 还远，先快速回升：' + cw + '。' : '越过 Wmax 后又加速向上，探测是否有了更多带宽：' + cw + '。', '三次曲线增长');
          }
        }
        rec.step(0, 'CUBIC 大部分时间都停在 Wmax 附近，链路更满，所以文中实测 CUBIC 46.6 Mbit/s，Reno 41.8 Mbit/s。', { vars: { '结论': '在 Wmax 附近停留' } });
      }
    }, {
      label: '超时',
      run: function (m, p, rec) {
        m.hist = []; m.ss = MAXV;
        var cw = 10;
        [10, 20, 40].forEach(function (v) { push(m, rec, v, 'cur', '慢启动：' + v + '。', '慢启动'); });
        cw = 40;
        for (var i = 0; i < 3; i++) { cw++; push(m, rec, cw, 'ok', '（假设已经进入拥塞避免）每个往返加 1：' + cw + '。', '拥塞避免'); }
        m.ss = Math.floor(cw / 2);
        push(m, rec, 1, 'del', '这次不是 3 个重复 ACK，而是重传计时器超时：连 ACK 都收不到，说明网络状况很差。ssthresh 设为一半（' + m.ss + '），cwnd 直接降到 1。', '超时');
        cw = 1;
        while (cw * 2 < m.ss) { cw *= 2; push(m, rec, cw, 'cur', '从 1 重新慢启动：' + cw + '。', '慢启动'); }
        cw = m.ss; push(m, rec, cw, 'cur', '到达 ssthresh = ' + m.ss + '，慢启动结束。', '慢启动');
        for (var j = 0; j < 6; j++) { cw++; push(m, rec, cw, 'ok', '拥塞避免，线性增长：' + cw + '。', '拥塞避免'); }
        rec.step(0, '超时的代价比快速重传大得多：cwnd 从 1 重新开始。所以 TCP 尽量靠重复 ACK 和 SACK 尽早发现丢包。', { vars: { '结论': '超时 → cwnd = 1' } });
      }
    }]
  });
})();
