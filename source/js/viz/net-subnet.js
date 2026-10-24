/* 《IP 地址与子网》：子网计算，逐位展示网络部分和主机部分 */
(function () {
  'use strict';
  var ROWS = ['地址', '掩码', '网络地址', '广播地址'];
  var X0 = 112, CW = 16, GAP = 12, TOP = 34, ROWH = 62;

  function toInt(a) { return a.reduce(function (s, o) { return s * 256 + o; }, 0); }
  function toDot(n) { return [24, 16, 8, 0].map(function (s) { return Math.floor(n / Math.pow(2, s)) % 256; }).join('.'); }
  function bits(n) { var b = []; for (var i = 31; i >= 0; i--) b.push(Math.floor(n / Math.pow(2, i)) % 2); return b; }

  function parse(s) {
    var m = /^\s*(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})\s*\/\s*(\d{1,2})\s*$/.exec(String(s));
    if (!m) return null;
    var oct = [+m[1], +m[2], +m[3], +m[4]], len = +m[5];
    if (oct.some(function (o) { return o > 255; }) || len > 32) return null;
    return { addr: toInt(oct), len: len };
  }

  function calc(a, len) {
    var host = Math.pow(2, 32 - len);
    var net = Math.floor(a / host) * host;
    return { mask: Math.pow(2, 32) - host, net: net, bc: net + host - 1, size: host };
  }

  // 每一行 4 个八位组，每个八位组一个 array 场景；show 指定显示到第几行
  function view(m, x) {
    var parts = [], texts = [], show = x.show || 0;
    var c = calc(m.addr, m.len);
    var vals = [m.addr, c.mask, c.net, c.bc];
    for (var r = 0; r <= show && r < 4; r++) {
      var b = bits(vals[r]);
      texts.push({ id: 'l' + r, x: 14, y: TOP + r * ROWH + 25, text: ROWS[r] });
      for (var o = 0; o < 4; o++) {
        var cells = [];
        for (var k = 0; k < 8; k++) {
          var i = o * 8 + k, isNet = i < m.len, hl = null;
          if (r === 0) hl = x.split ? (isNet ? 'cur' : null) : null;
          else if (r === 1) hl = isNet ? 'ok' : 'dim';
          else if (r === 2) hl = isNet ? 'cur' : 'dim';
          else hl = isNet ? 'cur' : 'new';
          cells.push({ v: b[i], hl: hl });
        }
        parts.push({ kind: 'array', x0: X0 + o * (8 * CW + GAP), cellW: CW, top: TOP + r * ROWH, noIndex: true, cells: cells });
      }
    }
    parts.push({ kind: 'free', texts: texts });
    return { kind: 'multi', parts: parts, vars: x.vars };
  }

  function run(m, rec) {
    var c = calc(m.addr, m.len), hostBits = 32 - m.len;
    var addr = toDot(m.addr) + '/' + m.len;
    rec.step(0, '把 ' + toDot(m.addr) + ' 写成 32 个二进制位，每 8 位一组，正好对应点分十进制的 4 个数。', { show: 0, vars: { '地址': toDot(m.addr) } });
    rec.step(0, '/' + m.len + ' 表示前 ' + m.len + ' 位是网络部分（高亮），后 ' + hostBits + ' 位是主机部分。同一个子网中的主机，网络部分完全相同。', { show: 0, split: true, vars: { '地址': addr } });
    rec.step(0, '子网掩码：网络部分全为 1，主机部分全为 0，写成点分十进制就是 ' + toDot(c.mask) + '。/' + m.len + ' 和 ' + toDot(c.mask) + ' 是同一个意思的两种写法。', { show: 1, split: true, vars: { '地址': addr, '掩码': toDot(c.mask) } });
    rec.step(0, '地址和掩码按位与（AND），主机部分全部变成 0，得到网络地址 ' + toDot(c.net) + '。路由表中记录的就是它。', { show: 2, split: true, vars: { '地址': addr, '掩码': toDot(c.mask), '网络地址': toDot(c.net) } });
    var v = { '网络地址': toDot(c.net) + '/' + m.len, '广播地址': toDot(c.bc) };
    if (m.len <= 30) {
      v['可用主机'] = toDot(c.net + 1) + ' ～ ' + toDot(c.bc - 1);
      v['主机数'] = c.size - 2;
      rec.step(0, '主机部分全部变成 1，得到广播地址 ' + toDot(c.bc) + '。主机部分全 0（网络地址）和全 1（广播地址）不能分配给主机，所以 ' + hostBits + ' 位主机部分可以容纳 2^' + hostBits + ' − 2 = ' + (c.size - 2) + ' 台主机。', { show: 3, split: true, vars: v });
    } else {
      v = { '网络地址': toDot(c.net) + '/' + m.len, '地址范围': m.len === 32 ? toDot(c.net) : toDot(c.net) + ' ～ ' + toDot(c.bc) };
      rec.step(0, m.len === 32 ? '/32 表示单独一个地址，没有主机部分，常用于路由表中指向一台主机的路由。' : '/31 只有 2 个地址，按规定（RFC 3021）两个都可以分配，用于两台路由器之间的点对点链路。', { show: 3, split: true, vars: v });
    }
  }

  DSViz.register('subnet-calc', {
    title: '子网计算',
    height: 290,
    init: function () { return parse('192.168.1.130/26'); },
    view: view,
    start: function (m, rec) { rec.step(0, '输入一个「地址/前缀长度」，比如 192.168.1.130/26、172.17.0.3/16，点「计算」逐步查看。', { show: 0, vars: { '地址': '192.168.1.130/26' } }); },
    actions: [{
      label: '计算',
      fields: [{ key: 'a', label: '地址/前缀', value: '192.168.1.130/26', size: 18 }],
      check: function (m, p) { return parse(p.a) ? null : '格式：a.b.c.d/n，每个数 0～255，n 为 0～32'; },
      run: function (m, p, rec) { var q = parse(p.a); m.addr = q.addr; m.len = q.len; run(m, rec); }
    }]
  });
})();
