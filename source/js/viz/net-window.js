/* 《可靠传输》：滑动窗口、累积确认、SACK 与快速重传（每个报文段一个确认，逐个事件推进） */
(function () {
  'use strict';
  var N = 10;                                    // 一共 10 个报文段，编号 1～10

  function fill(v) { var a = []; for (var i = 0; i <= N; i++) a.push(v); return a; }
  function init(win) {
    // snd：0 未发送，1 已发送未确认，2 已确认；rcv：0 未收到，1 已收到
    return { snd: fill(0), rcv: fill(0), sacked: fill(0), base: 1, next: 1, win: win || 4 };
  }
  function firstGap(m) { for (var i = 1; i <= N; i++) if (!m.rcv[i]) return i; return N + 1; }
  function vars(m, extra) {
    var v = { '窗口': m.win + ' 段', '期待的下一段': firstGap(m) > N ? '全部收到' : firstGap(m) };
    for (var k in extra || {}) v[k] = extra[k];
    return v;
  }

  function view(m, x) {
    var hl = x.hl || {}, sc = [], rc = [], gap = firstGap(m);
    for (var i = 1; i <= N; i++) {
      var s = m.snd[i], inWin = i >= m.base && i < m.base + m.win;
      var h = s === 2 ? 'ok' : s === 1 ? (m.sacked[i] ? 'new' : 'cur') : inWin ? 'empty' : 'dim';
      sc.push({ v: i, hl: hl['s' + i] || h });
      rc.push({ v: m.rcv[i] ? i : '', hl: hl['r' + i] || (m.rcv[i] ? (i < gap ? 'ok' : 'warn') : 'empty') });
    }
    var ptrs = [{ name: '窗口', at: m.base - 1 }];
    return {
      kind: 'multi', vars: x.vars,
      parts: [
        { kind: 'array', x0: 120, cellW: 52, top: 70, cells: sc, noIndex: true, ptrs: m.base <= N ? ptrs : [] },
        { kind: 'array', x0: 120, cellW: 52, top: 170, cells: rc, noIndex: true },
        { kind: 'free', texts: [
          { id: 't1', x: 12, y: 95, text: '发送方' },
          { id: 't2', x: 12, y: 195, text: '接收方' },
          { id: 't3', x: 120, y: 240, text: x.note || '', cls: 'muted' }
        ] }
      ]
    };
  }

  function H() { var h = {}; for (var i = 0; i < arguments.length; i += 2) h[arguments[i]] = arguments[i + 1]; return h; }

  function run(m, rec) {
    var f = init(m.win); Object.keys(f).forEach(function (k) { m[k] = f[k]; });
    var LOST = m.win + 2, lostOnce = false, dup = 0, queue = [];
    rec.step(0, '发送窗口是 ' + m.win + ' 段：最多可以有 ' + m.win + ' 段已发出但还没被确认。虚线框是窗口内可以发送的，灰色的在窗口外，暂时不能发。第 ' + LOST + ' 段会在途中丢失。', { vars: vars(m) });

    function trySend() {
      var sent = [];
      while (m.next < m.base + m.win && m.next <= N) {
        var i = m.next++;
        m.snd[i] = 1;
        if (i === LOST && !lostOnce) { lostOnce = true; sent.push(i + '（途中丢失）'); }
        else { queue.push(i); sent.push(String(i)); }
      }
      if (sent.length) {
        var h = {}; sent.forEach(function (s) { h['s' + parseInt(s, 10)] = 'cur'; });
        rec.step(0, '窗口内有空位，发出第 ' + sent.join('、') + ' 段，不必等前一段的确认。', { hl: h, vars: vars(m) });
      }
    }

    trySend();
    var guard = 0, timeout = false;
    for (;;) {
    while (queue.length && guard++ < 60) {
      var seg = queue.shift();
      m.rcv[seg] = 1;
      var ack = firstGap(m), sack = [];
      for (var k = ack; k <= N; k++) if (m.rcv[k]) sack.push(k);
      if (ack > m.base) {                                  // 新的确认：窗口滑动
        var old = m.base;
        for (var j = m.base; j < ack; j++) m.snd[j] = 2;
        m.base = ack; dup = 0;
        rec.step(0, '第 ' + seg + ' 段到达，接收方回复确认号 ' + ack + '：' + (ack - 1) + ' 及以前全部收到（累积确认）。窗口左边从 ' + old + ' 滑到 ' + ack + '。',
          { hl: H('r' + seg, 'new'), vars: vars(m), note: 'ACK ' + ack });
        trySend();
      } else {                                             // 重复确认
        dup++;
        sack.forEach(function (s) { m.sacked[s] = 1; });
        rec.step(0, '第 ' + seg + ' 段到达，但第 ' + ack + ' 段还没到，数据不连续。接收方只能再次回复确认号 ' + ack + '（第 ' + dup + ' 个重复确认），并用 SACK 说明已经收到了 ' + sack.join('、') + '。',
          { hl: H('r' + seg, 'new', 'r' + ack, 'warn'), vars: vars(m, { '重复确认': dup }), note: 'ACK ' + ack + '  SACK ' + sack.join(',') });
        if (dup === 3) {
          queue.unshift(ack);
          rec.step(0, '发送方收到了 3 个重复确认，判断第 ' + ack + ' 段丢失了，不等超时，立即重发它（快速重传）。SACK 中已经收到的段不需要重发。',
            { hl: H('s' + ack, 'warn'), vars: vars(m, { '重复确认': dup }) });
        }
      }
    }
      if (m.base > N || timeout) break;
      // 没有新的数据可发，重复确认又凑不够 3 个：只能等超时
      timeout = true;
      rec.step(0, '窗口已满，不能再发新的段，重复确认只有 ' + dup + ' 个，凑不够 3 个。发送方什么也做不了，只能等重传计时器超时（至少 200 毫秒，比快速重传慢得多），然后重发第 ' + m.base + ' 段。',
        { hl: H('s' + m.base, 'warn'), vars: vars(m, { '重复确认': dup }) });
      queue.push(m.base);
    }
    rec.step(0, '全部 ' + N + ' 段都已送达并被确认。' + (timeout
      ? '这次是靠超时重传恢复的：窗口太小时，后面的段不够多，凑不出 3 个重复确认。'
      : '丢失的那一段只多花了一次重发，没有等待超时，其他段也没有重发。'), { vars: vars(m) });
  }

  DSViz.register('tcp-window', {
    title: '滑动窗口与快速重传',
    height: 260,
    init: function () { return init(4); },
    view: view,
    start: function (m, rec) { rec.step(0, '上面一行是发送方的 10 个报文段，下面一行是接收方收到的。点「运行」，可以改变窗口大小（3～5）。', { vars: vars(m) }); },
    actions: [{
      label: '运行',
      fields: [{ key: 'w', label: '窗口（段）', value: 4, size: 2 }],
      check: function (m, p) { return [3, 4, 5].indexOf(p.w) >= 0 ? null : '窗口取 3、4 或 5'; },
      run: function (m, p, rec) { m.win = p.w; run(m, rec); }
    }]
  });
})();
