/**
 * pktview —— 逐字节拆开一个真实的数据包
 *
 * 文章中写 <div class="pktview" data-pkt="名字"></div>，包的字节来自 window.PKTS[名字]（source/js/viz/net-pkts.js，
 * 由 projects/net-pkts/export.py 从真实的抓包生成）。也可以直接写 data-hex="十六进制"。
 * 加上 data-paste 会多出一个输入框，读者可以粘贴自己用 tcpdump -xx 抓到的包。
 *
 * 解析在浏览器中完成：以太网 → IPv4 → TCP / UDP / ICMP → DNS / HTTP / TLS。
 * 左边是十六进制，右边是字段；把鼠标移到任意一个字节或字段上，另一边对应的部分会高亮。
 */
(function () {
  'use strict';

  /* ---------------- 工具 ---------------- */

  function h(tag, cls, parent, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    if (parent) parent.appendChild(e);
    return e;
  }
  function hex(b, o, n) {
    var s = '';
    for (var i = 0; i < n && o + i < b.length; i++) s += (b[o + i] < 16 ? '0' : '') + b[o + i].toString(16);
    return s;
  }
  function u8(b, o) { return b[o]; }
  function u16(b, o) { return (b[o] << 8) | b[o + 1]; }
  function u24(b, o) { return (b[o] << 16) | (b[o + 1] << 8) | b[o + 2]; }
  function u32(b, o) { return ((b[o] << 24) >>> 0) + (b[o + 1] << 16) + (b[o + 2] << 8) + b[o + 3]; }
  function ip4(b, o) { return [b[o], b[o + 1], b[o + 2], b[o + 3]].join('.'); }
  function mac(b, o) { var a = []; for (var i = 0; i < 6; i++) a.push(hex(b, o + i, 1)); return a.join(':'); }
  function ascii(b, o, n) { var s = ''; for (var i = 0; i < n; i++) s += String.fromCharCode(b[o + i]); return s; }
  function x(v, w) { var s = v.toString(16); while (s.length < w) s = '0' + s; return '0x' + s; }

  /* ---------------- 解析器 ----------------
     每一层：{ id, name, off, len, fields: [字段] }
     字段：{ n 名字, o 偏移, l 长度, v 值, d 说明, kids: [字段] } */

  function Ctx(b) { this.b = b; this.layers = []; }
  Ctx.prototype.layer = function (id, name, off, len) {
    var L = { id: id, name: name, off: off, len: len, fields: [] };
    this.layers.push(L);
    return L;
  };
  function F(list, n, o, l, v, d, kids) {
    var f = { n: n, o: o, l: l, v: v, d: d || '' };
    if (kids) f.kids = kids;
    list.push(f);
    return f;
  }

  var ETHERTYPE = { 0x0800: 'IPv4', 0x86dd: 'IPv6', 0x0806: 'ARP' };
  var IPPROTO = { 1: 'ICMP', 6: 'TCP', 17: 'UDP' };

  function eth(c, o) {
    var b = c.b, L = c.layer('eth', '以太网', o, 14), t = u16(b, o + 12);
    F(L.fields, '目的 MAC', o, 6, mac(b, o), '下一跳的网卡地址。这个包要先交给网关（路由器），所以这里是网关的 MAC，而不是最终服务器的。每经过一个路由器，以太网头部都会换一次。');
    F(L.fields, '源 MAC', o + 6, 6, mac(b, o + 6), '发送这个帧的网卡的地址。');
    F(L.fields, '类型', o + 12, 2, x(t, 4) + ' ' + (ETHERTYPE[t] || '?'), '上一层是什么协议。0x0800 表示里面装的是 IPv4 包，接收方据此把数据交给 IP 层处理。');
    if (t === 0x0800) ipv4(c, o + 14);
    else rest(c, o + 14, ETHERTYPE[t] || '数据');
  }

  function ipv4(c, o) {
    var b = c.b, ihl = (b[o] & 15) * 4, total = u16(b, o + 2), proto = b[o + 9];
    var L = c.layer('ip', 'IPv4', o, ihl), fl = u16(b, o + 6);
    F(L.fields, '版本 + 首部长度', o, 1, '4 / ' + ihl + ' 字节', '高 4 位是版本号 4；低 4 位是首部长度，以 4 字节为单位：' + (b[o] & 15) + ' × 4 = ' + ihl + ' 字节。没有选项时就是 20。');
    F(L.fields, '服务类型 / ECN', o + 1, 1, x(b[o + 1], 2), 'DSCP 用来标记优先级，ECN 用来不丢包地通知拥塞。普通流量都是 0。');
    F(L.fields, '总长度', o + 2, 2, total + ' 字节', 'IP 首部加上数据的总长度。' + total + ' = IP 首部 ' + ihl + ' + 里面的 ' + (total - ihl) + ' 字节。');
    F(L.fields, '标识', o + 4, 2, x(u16(b, o + 4), 4), '分片时，同一个原始包的所有分片带有相同的标识，接收方据此把它们拼回去。');
    F(L.fields, '标志 + 片偏移', o + 6, 2, (fl & 0x4000 ? 'DF ' : '') + (fl & 0x2000 ? 'MF ' : '') + '偏移 ' + (fl & 0x1fff), 'DF（Don\'t Fragment）= ' + (fl & 0x4000 ? 1 : 0) + '：不允许路由器把它分片，太大时直接丢弃并回复 ICMP。现代系统都设置 DF，靠路径 MTU 发现来避免分片。');
    F(L.fields, 'TTL', o + 8, 1, String(b[o + 8]), '生存时间：每经过一个路由器减 1，减到 0 就被丢弃，防止包在环路中永远转下去。' + (b[o + 8] === 64 ? 'Linux 发出的包初始值是 64。' : b[o + 8] < 64 ? '初始值多半是 64，说明它在路上经过了 ' + (64 - b[o + 8]) + ' 个路由器。' : ''));
    F(L.fields, '协议', o + 9, 1, proto + ' ' + (IPPROTO[proto] || '?'), '里面装的是什么：6 是 TCP，17 是 UDP，1 是 ICMP。');
    F(L.fields, '首部校验和', o + 10, 2, x(u16(b, o + 10), 4), '只校验 IP 首部。因为 TTL 每一跳都会变，每个路由器都要重新计算。');
    F(L.fields, '源 IP', o + 12, 4, ip4(b, o + 12), '发送方的 IP 地址。' + (b[o + 12] === 172 && b[o + 13] >= 16 && b[o + 13] < 32 || b[o + 12] === 10 || b[o + 12] === 192 && b[o + 13] === 168 ? '这是私有地址，出公网时会被 NAT 换掉。' : ''));
    F(L.fields, '目的 IP', o + 16, 4, ip4(b, o + 16), '最终目的地的 IP 地址。和 MAC 不同，它在整个路途中不变（NAT 除外）。');
    if (ihl > 20) F(L.fields, '选项', o + 20, ihl - 20, hex(b, o + 20, ihl - 20), 'IP 选项，很少使用。');
    var end = Math.min(b.length, o + total);
    if (proto === 6) tcp(c, o + ihl, end);
    else if (proto === 17) udp(c, o + ihl, end);
    else if (proto === 1) icmp(c, o + ihl, end);
    else rest(c, o + ihl, IPPROTO[proto] || '数据', end);
  }

  var TCPOPT = { 0: '选项结束', 1: 'NOP 填充', 2: 'MSS', 3: '窗口扩大', 4: 'SACK 允许', 5: 'SACK', 8: '时间戳' };
  function tcp(c, o, end) {
    var b = c.b, hl = (b[o + 12] >> 4) * 4, fl = u16(b, o + 12) & 0x1ff;
    var L = c.layer('tcp', 'TCP', o, hl);
    var sp = u16(b, o), dp = u16(b, o + 2);
    F(L.fields, '源端口', o, 2, String(sp), sp > 1023 ? '客户端的临时端口，由内核随机挑选（Linux 默认在 32768～60999 之间）。' : '服务端口。');
    F(L.fields, '目的端口', o + 2, 2, String(dp), PORTS[dp] ? dp + ' 是 ' + PORTS[dp] + ' 的端口。端口决定了数据交给对方的哪个程序。' : '端口决定了数据交给对方的哪个程序。');
    var names = ['FIN', 'SYN', 'RST', 'PSH', 'ACK', 'URG', 'ECE', 'CWR', 'AE'], on = [];
    for (var i = 0; i < names.length; i++) if (fl & (1 << i)) on.push(names[i]);
    var syn = fl & 2;
    F(L.fields, '序号', o + 4, 4, String(u32(b, o + 4)), syn ? '初始序号（ISN），随机选取。SYN 本身也占一个序号，所以对方会确认 ISN + 1。' : '这个报文中第一个数据字节的编号。TCP 给每个字节编号，接收方据此排序、去重、发现丢失。');
    F(L.fields, '确认号', o + 8, 4, String(u32(b, o + 8)), fl & 16 ? '期待对方发来的下一个字节的编号，表示这之前的字节都已经收到。' : 'ACK 标志没有设置，这个字段没有意义，所以是 0。');
    var flagKids = [];
    var FD = {
      SYN: '同步：请求建立连接，并携带初始序号。', ACK: '确认号字段有效。连接建立后，所有报文都带 ACK。',
      PSH: '推送：让接收方尽快把数据交给应用程序。', FIN: '我的数据发完了，请求关闭这个方向。',
      RST: '重置：异常终止连接。', URG: '紧急指针有效，几乎不用。', ECE: 'ECN 回显：通知对方网络出现了拥塞。', CWR: '已经减小了拥塞窗口。', AE: '精确 ECN。'
    };
    on.forEach(function (n) { flagKids.push({ n: n, o: o + 13, l: 1, v: '1', d: FD[n] }); });
    F(L.fields, '首部长度 + 标志', o + 12, 2, hl + ' 字节 · ' + (on.join(' ') || '无'),
      '高 4 位是首部长度，以 4 字节为单位：' + (hl / 4) + ' × 4 = ' + hl + ' 字节（固定部分 20 + 选项 ' + (hl - 20) + '）。后面的 9 位是标志：' + (on.join('、') || '无') + '。', flagKids);
    var win = u16(b, o + 14);
    F(L.fields, '窗口', o + 14, 2, String(win), syn ? '接收窗口：我还能接收多少字节。SYN 中的窗口不做扩大，就是 ' + win + ' 字节。' : '接收窗口。它要乘以握手时协商的扩大因子：如果双方都用了 wscale 7，实际窗口是 ' + win + ' × 128 = ' + win * 128 + ' 字节。');
    F(L.fields, '校验和', o + 16, 2, x(u16(b, o + 16), 4), '覆盖 TCP 首部、数据和一个「伪首部」（源和目的 IP 等）。抓包时看到的值可能是错的：网卡会代为计算（checksum offload）。');
    F(L.fields, '紧急指针', o + 18, 2, String(u16(b, o + 18)), '只在 URG 标志有效时使用，现在几乎没有程序用它。');
    if (hl > 20) {
      var kids = [], p = o + 20;
      while (p < o + hl) {
        var k = b[p];
        if (k === 0 || k === 1) { kids.push({ n: TCPOPT[k], o: p, l: 1, v: String(k), d: k ? '1 字节的填充，让后面的选项按 4 字节对齐。' : '选项结束。' }); p++; continue; }
        var ol = b[p + 1] || 2, v = '', d = '';
        if (k === 2) { v = String(u16(b, p + 2)); d = '最大报文段长度：我一个报文最多能收 ' + v + ' 字节数据。1460 = 以太网 MTU 1500 − IP 头 20 − TCP 头 20。'; }
        else if (k === 3) { v = String(b[p + 2]) + '（× ' + (1 << b[p + 2]) + '）'; d = '窗口扩大因子：窗口字段只有 16 位，最大 65535；之后的窗口值都要乘以 2^' + b[p + 2] + ' = ' + (1 << b[p + 2]) + '。只能在 SYN 中协商。'; }
        else if (k === 4) { v = '是'; d = '我支持选择确认（SACK）：丢包时可以告诉对方具体收到了哪些段，对方只重传缺的部分。'; }
        else if (k === 8) { v = u32(b, p + 2) + ' / ' + u32(b, p + 6); d = '时间戳：前一个数是我的时钟（TSval），后一个是回显对方的时钟（TSecr）。用来测量往返时间，以及防止序号绕回。'; }
        else { v = hex(b, p + 2, ol - 2); d = '其他选项。'; }
        kids.push({ n: TCPOPT[k] || '选项 ' + k, o: p, l: ol, v: v, d: d });
        p += ol;
      }
      F(L.fields, '选项', o + 20, hl - 20, (hl - 20) + ' 字节', '固定的 20 字节之后是选项。' + (syn ? 'SYN 中的选项最多：双方在握手时协商 MSS、窗口扩大、SACK、时间戳，连接建立后就不能再改了。' : '建立连接后的报文通常只带时间戳，前面用两个 NOP 补齐到 4 字节对齐。'), kids);
    }
    var po = o + hl;
    if (po >= end) { L.note = '没有数据'; return; }
    if (b[po] >= 0x14 && b[po] <= 0x17 && b[po + 1] === 3) tls(c, po, end);
    else if (/^(GET|POST|PUT|HEAD|DELETE|OPTIONS|PATCH|HTTP\/)/.test(ascii(b, po, Math.min(8, end - po)))) http(c, po, end);
    else rest(c, po, '数据', end);
  }

  var PORTS = { 53: 'DNS', 80: 'HTTP', 443: 'HTTPS', 22: 'SSH', 25: 'SMTP', 123: 'NTP', 67: 'DHCP', 68: 'DHCP' };

  function udp(c, o, end) {
    var b = c.b, L = c.layer('udp', 'UDP', o, 8), sp = u16(b, o), dp = u16(b, o + 2), len = u16(b, o + 4);
    F(L.fields, '源端口', o, 2, String(sp), PORTS[sp] ? sp + ' 是 ' + PORTS[sp] + ' 的端口，这是服务器发回的。' : '发送程序的端口，这里是内核随机挑选的临时端口。回复会发回这个端口。');
    F(L.fields, '目的端口', o + 2, 2, String(dp), PORTS[dp] ? dp + ' 是 ' + PORTS[dp] + ' 的端口。' : '接收程序的端口。');
    F(L.fields, '长度', o + 4, 2, len + ' 字节', 'UDP 头部 8 字节加上数据 ' + (len - 8) + ' 字节。');
    F(L.fields, '校验和', o + 6, 2, x(u16(b, o + 6), 4), '检查头部和数据在途中是否出错，出错就直接丢弃，不会通知任何人。UDP 头部就这 8 个字节，没有序号、没有确认、没有窗口。');
    if (sp === 53 || dp === 53) dns(c, o + 8, end);
    else rest(c, o + 8, '数据', end);
  }

  function icmp(c, o, end) {
    var b = c.b, L = c.layer('icmp', 'ICMP', o, end - o);
    var T = { 0: '回显应答', 8: '回显请求（ping）', 3: '目的不可达', 11: '超时（TTL 减到 0）' };
    F(L.fields, '类型', o, 1, b[o] + ' ' + (T[b[o]] || ''), 'ICMP 消息的种类。');
    F(L.fields, '代码', o + 1, 1, String(b[o + 1]), '类型下的细分原因，例如目的不可达中的 3 是端口不可达。');
    F(L.fields, '校验和', o + 2, 2, x(u16(b, o + 2), 4), '');
    if (end > o + 4) F(L.fields, '其余部分', o + 4, end - o - 4, (end - o - 4) + ' 字节', 'ping 中是标识、序号和数据；错误消息中是引发错误的那个包的开头。');
  }

  /* DNS：名字可能用指针压缩，指针以 11 开头，后 14 位是相对 DNS 报文开头的偏移 */
  var RRTYPE = { 1: 'A', 2: 'NS', 5: 'CNAME', 6: 'SOA', 15: 'MX', 16: 'TXT', 28: 'AAAA', 41: 'OPT', 65: 'HTTPS' };
  function dnsName(b, base, p) {
    var labels = [], jumped = false, len = 0, guard = 0;
    while (p < b.length && guard++ < 64) {
      var n = b[p];
      if (n === 0) { if (!jumped) len++; break; }
      if ((n & 0xc0) === 0xc0) { if (!jumped) len += 2; p = base + (((n & 63) << 8) | b[p + 1]); jumped = true; continue; }
      labels.push(ascii(b, p + 1, n));
      if (!jumped) len += n + 1;
      p += n + 1;
    }
    return { name: labels.join('.') || '.', len: len, ptr: jumped };
  }
  function dns(c, o, end) {
    var b = c.b, L = c.layer('dns', 'DNS', o, end - o), fl = u16(b, o + 2), qr = fl >> 15;
    F(L.fields, '查询 ID', o, 2, x(u16(b, o), 4), '客户端随机选的编号，响应会原样带回，客户端据此把响应和查询配对。');
    var fk = [];
    fk.push({ n: 'QR', o: o + 2, l: 1, v: String(qr), d: qr ? '1：这是响应。' : '0：这是查询。' });
    if (fl & 0x0400) fk.push({ n: 'AA', o: o + 2, l: 1, v: '1', d: '权威回答：回答者就是这个域名的权威服务器。' });
    fk.push({ n: 'RD', o: o + 2, l: 1, v: String((fl >> 8) & 1), d: '期望递归：「请帮我查到底」。dig 默认设置。' });
    if (qr) fk.push({ n: 'RA', o: o + 3, l: 1, v: String((fl >> 7) & 1), d: '服务器支持递归查询。' });
    fk.push({ n: 'AD', o: o + 3, l: 1, v: String((fl >> 5) & 1), d: '数据经过了 DNSSEC 验证。' });
    if (qr) fk.push({ n: 'RCODE', o: o + 3, l: 1, v: String(fl & 15) + (fl & 15 ? '' : ' NOERROR'), d: '响应码：0 成功，3 NXDOMAIN（域名不存在）。' });
    F(L.fields, '标志', o + 2, 2, x(fl, 4) + ' ' + (qr ? '响应' : '查询'), '一组标志位，展开看每一位的含义。', fk);
    var cnt = [u16(b, o + 4), u16(b, o + 6), u16(b, o + 8), u16(b, o + 10)];
    F(L.fields, '问题数', o + 4, 2, String(cnt[0]), '问题部分有几条，通常是 1。');
    F(L.fields, '回答数', o + 6, 2, String(cnt[1]), '回答部分有几条记录。');
    F(L.fields, '权威记录数', o + 8, 2, String(cnt[2]), '');
    F(L.fields, '附加记录数', o + 10, 2, String(cnt[3]), cnt[3] ? '附加部分通常是 EDNS 的 OPT 伪记录，用来声明能接收更大的 UDP 报文。' : '');
    var p = o + 12;
    for (var i = 0; i < cnt[0] && p < end; i++) {
      var nm = dnsName(b, o, p), qt = u16(b, p + nm.len);
      F(L.fields, '问题：' + nm.name, p, nm.len + 4, (RRTYPE[qt] || qt) + ' IN', '要查询的名字和记录类型。', [
        { n: '名字', o: p, l: nm.len, v: nm.name, d: '名字被拆成若干标签，每个标签前面是 1 字节的长度：' + nm.name.split('.').map(function (s) { return s.length + ' ' + s; }).join('、') + '，最后以长度 0 结束。' },
        { n: '类型', o: p + nm.len, l: 2, v: (RRTYPE[qt] || '') + ' (' + qt + ')', d: 'A 记录 = IPv4 地址，AAAA = IPv6 地址。' },
        { n: '类', o: p + nm.len + 2, l: 2, v: 'IN', d: 'Internet，几乎总是 1。' }
      ]);
      p += nm.len + 4;
    }
    var sec = ['回答', '权威', '附加'];
    for (var s = 0; s < 3; s++) {
      for (var j = 0; j < cnt[s + 1] && p + 10 < end + 1; j++) {
        var rn = dnsName(b, o, p), q = p + rn.len, t = u16(b, q), rl = u16(b, q + 8), ttl = u32(b, q + 4);
        var rd = t === 1 ? ip4(b, q + 10) : t === 5 ? dnsName(b, o, q + 10).name : hex(b, q + 10, Math.min(rl, 16));
        var title = t === 41 ? 'OPT（EDNS）' : sec[s] + '：' + rn.name + ' → ' + rd;
        F(L.fields, title, p, rn.len + 10 + rl, RRTYPE[t] || String(t), t === 41 ? 'EDNS 伪记录：类字段被借用来表示能接收的 UDP 报文大小。' : '一条资源记录。', t === 41 ? null : [
          { n: '名字', o: p, l: rn.len, v: rn.name, d: rn.ptr ? '压缩指针 ' + hex(b, p, 2) + '：以二进制 11 开头，后面是偏移 ' + (u16(b, p) & 0x3fff) + '，指向前面问题部分里的 example.com，免得再写一遍。' : '' },
          { n: '类型', o: q, l: 2, v: RRTYPE[t] || String(t), d: '' },
          { n: '类', o: q + 2, l: 2, v: 'IN', d: '' },
          { n: 'TTL', o: q + 4, l: 4, v: ttl + ' 秒', d: '这条记录可以缓存多久。解析器的缓存会倒数，所以每次查到的值都可能更小。' },
          { n: '数据长度', o: q + 8, l: 2, v: String(rl), d: '' },
          { n: t === 1 ? '地址' : '数据', o: q + 10, l: rl, v: rd, d: t === 1 ? '4 字节的 IPv4 地址，这就是我们要的答案。' : '' }
        ]);
        p = q + 10 + rl;
      }
    }
  }

  var HDRS = {
    host: 'Host：要访问的网站。一个 IP 上可能有很多网站，服务器靠它区分。HTTP/1.1 中必须有。',
    'user-agent': '客户端程序的名字和版本。',
    accept: '我能接受哪些类型的内容，*/* 表示什么都行。',
    connection: '连接用完后是否关闭。',
    'content-length': '正文的字节数，接收方据此知道正文在哪里结束。',
    'content-type': '正文的类型，例如 text/html。'
  };
  function http(c, o, end) {
    var b = c.b, L = c.layer('http', 'HTTP', o, end - o), p = o, first = true;
    while (p < end) {
      var q = p;
      while (q + 1 < end && !(b[q] === 13 && b[q + 1] === 10)) q++;
      var line = ascii(b, p, q - p), n = q + 2 <= end ? q + 2 - p : q - p;
      if (!line) { F(L.fields, '空行', p, n, '\\r\\n', '一个空行表示头部结束。之后如果有正文，就是正文。'); p += n; break; }
      if (first) {
        var parts = line.split(' ');
        F(L.fields, /^HTTP/.test(line) ? '状态行' : '请求行', p, n, line, /^HTTP/.test(line) ? '版本、状态码和原因短语。' : '方法 ' + parts[0] + '、路径 ' + parts[1] + '、版本 ' + parts[2] + '，以 \\r\\n（0d 0a）结尾。HTTP/1.1 是纯文本协议，右边的 ASCII 栏直接就能读。');
        first = false;
      } else {
        var k = line.split(':')[0];
        F(L.fields, k, p, n, line.slice(k.length + 1).trim(), HDRS[k.toLowerCase()] || '一个头部字段：名字、冒号、值，以 \\r\\n 结尾。');
      }
      p += n;
    }
    if (p < end) F(L.fields, '正文', p, end - p, (end - p) + ' 字节', '');
  }

  /* TLS */
  var CT = { 20: 'ChangeCipherSpec', 21: 'Alert', 22: 'Handshake', 23: 'Application Data' };
  var HST = { 1: 'Client Hello', 2: 'Server Hello', 4: 'New Session Ticket', 8: 'Encrypted Extensions', 11: 'Certificate', 15: 'Certificate Verify', 20: 'Finished' };
  var EXT = {
    0: 'server_name（SNI）', 5: 'status_request', 10: 'supported_groups', 11: 'ec_point_formats', 13: 'signature_algorithms',
    16: 'ALPN', 22: 'encrypt_then_mac', 23: 'extended_master_secret', 35: 'session_ticket', 43: 'supported_versions',
    45: 'psk_key_exchange_modes', 49: 'post_handshake_auth', 51: 'key_share', 21: 'padding', 65281: 'renegotiation_info'
  };
  var EXTD = {
    0: '要访问的域名，明文！服务器要在加密开始之前知道出示哪个网站的证书。所以途中的人虽然看不到内容，却能知道你访问的是哪个网站。',
    10: '支持哪些椭圆曲线 / 群，用于密钥交换。',
    13: '支持哪些签名算法，服务器用其中一种签名，证明自己持有证书的私钥。',
    16: '应用层协议协商：我会说 HTTP/2（h2）和 HTTP/1.1，服务器从中选一个。',
    43: '支持的 TLS 版本。外面的版本号为了兼容旧设备一直写 1.2，真正的 TLS 1.3 写在这里。',
    51: '密钥交换的公开值（x25519 等）。TLS 1.3 在第一条消息中就把它发出去，所以只要一个往返就能算出共享密钥。',
    45: '恢复会话时，用预共享密钥（PSK）的方式。',
    23: '扩展主密钥，修补 TLS 1.2 的一个漏洞。',
    35: '会话票据，下次连接时可以恢复会话、省掉部分握手。'
  };
  var GROUPS = { 0x1d: 'x25519', 0x17: 'secp256r1', 0x18: 'secp384r1', 0x19: 'secp521r1', 0x1e: 'x448', 0x11ec: 'X25519MLKEM768' };
  var SUITES = { 0x1301: 'TLS_AES_128_GCM_SHA256', 0x1302: 'TLS_AES_256_GCM_SHA384', 0x1303: 'TLS_CHACHA20_POLY1305_SHA256', 0xc02b: 'ECDHE-ECDSA-AES128-GCM', 0xc02f: 'ECDHE-RSA-AES128-GCM', 0xc02c: 'ECDHE-ECDSA-AES256-GCM', 0xc030: 'ECDHE-RSA-AES256-GCM' };
  var VERS = { 0x0301: 'TLS 1.0', 0x0302: 'TLS 1.1', 0x0303: 'TLS 1.2', 0x0304: 'TLS 1.3' };

  function tls(c, o, end) {
    var b = c.b, p = o;
    while (p + 5 <= end) {
      var ct = b[p], rl = u16(b, p + 3), rend = Math.min(end, p + 5 + rl);
      var L = c.layer('tls', 'TLS ' + (ct === 22 && HST[b[p + 5]] ? HST[b[p + 5]] : CT[ct] || '记录'), p, rend - p);
      F(L.fields, '记录类型', p, 1, ct + ' ' + (CT[ct] || ''), 'TLS 记录层：22 是握手消息，23 是加密的应用数据。');
      F(L.fields, '记录版本', p + 1, 2, x(u16(b, p + 1), 4) + ' ' + (VERS[u16(b, p + 1)] || ''), '为了兼容老旧的中间设备，这里一直写旧版本号，不代表真正使用的版本。');
      F(L.fields, '记录长度', p + 3, 2, rl + ' 字节', '');
      if (ct === 22 && (b[p + 5] === 1 || b[p + 5] === 2)) hello(L, b, p + 5, rend);
      else F(L.fields, ct === 23 ? '加密数据' : '内容', p + 5, rend - p - 5, (rend - p - 5) + ' 字节', ct === 23 ? '已经加密，途中的人只能看到长度。' : '');
      p = rend;
    }
  }
  function hello(L, b, p, end) {
    var t = b[p], client = t === 1, len = u24(b, p + 1), q = p + 4;
    F(L.fields, '握手类型', p, 1, t + ' ' + HST[t], client ? '客户端说的第一句话：我支持哪些版本、算法、扩展，以及我的密钥交换公开值。' : '服务器的回应：从客户端的列表中选定版本和算法，附上自己的公开值。');
    F(L.fields, '握手长度', p + 1, 3, len + ' 字节', '');
    F(L.fields, '版本', q, 2, x(u16(b, q), 4) + ' ' + VERS[u16(b, q)], '固定写 TLS 1.2（0x0303）。真正的版本在 supported_versions 扩展中。');
    F(L.fields, '随机数', q + 2, 32, hex(b, q + 2, 6) + '…', '32 字节的随机数，参与密钥的计算，保证每次连接的密钥都不同。');
    var sl = b[q + 34];
    F(L.fields, '会话 ID', q + 34, 1 + sl, sl + ' 字节', 'TLS 1.3 不再用它，但为了让中间设备以为这是一次 TLS 1.2 的会话恢复，仍然填 32 个随机字节。');
    q += 35 + sl;
    if (client) {
      var cl = u16(b, q), kids = [];
      for (var i = 0; i < cl; i += 2) { var cs = u16(b, q + 2 + i); kids.push({ n: SUITES[cs] || x(cs, 4), o: q + 2 + i, l: 2, v: x(cs, 4), d: cs >> 8 === 0x13 ? 'TLS 1.3 的套件：只规定对称加密和哈希，密钥交换另外协商。' : 'TLS 1.2 及以前的套件。' }); }
      F(L.fields, '密码套件', q, 2 + cl, (cl / 2) + ' 个', '我支持的加密算法组合，按偏好排序。前 3 个是 TLS 1.3 的。', kids);
      q += 2 + cl;
      var ml = b[q];
      F(L.fields, '压缩方法', q, 1 + ml, 'null', '压缩已经被废弃（CRIME 攻击），只剩下「不压缩」。');
      q += 1 + ml;
    } else {
      var sc = u16(b, q);
      F(L.fields, '选定的套件', q, 2, (SUITES[sc] || x(sc, 4)), '服务器从客户端的列表中选定的一个。');
      F(L.fields, '压缩方法', q + 2, 1, 'null', '');
      q += 3;
    }
    if (q + 2 > end) return;
    var el = u16(b, q), eend = Math.min(end, q + 2 + el), ek = [];
    var r = q + 2;
    while (r + 4 <= eend) {
      var et = u16(b, r), elen = u16(b, r + 2), v = elen + ' 字节';
      if (et === 0) v = ascii(b, r + 9, u16(b, r + 7));
      else if (et === 16) { var a = [], s = r + 6; while (s < r + 4 + elen) { a.push(ascii(b, s + 1, b[s])); s += b[s] + 1; } v = a.join(', '); }
      else if (et === 43) { var vs = []; for (var k = client ? 1 : 0; k < (client ? 1 + b[r + 4] : 2); k += 2) vs.push(VERS[u16(b, r + 4 + k)] || x(u16(b, r + 4 + k), 4)); v = vs.join(', '); }
      else if (et === 10) { var gs = []; for (var g = 0; g < u16(b, r + 4); g += 2) gs.push(GROUPS[u16(b, r + 6 + g)] || x(u16(b, r + 6 + g), 4)); v = gs.slice(0, 3).join(', ') + (gs.length > 3 ? ' …' : ''); }
      else if (et === 51) { var gg = u16(b, r + (client ? 6 : 4)); v = (GROUPS[gg] || x(gg, 4)) + ' 公开值 ' + u16(b, r + (client ? 8 : 6)) + ' 字节'; }
      ek.push({ n: EXT[et] || '扩展 ' + et, o: r, l: 4 + elen, v: v, d: EXTD[et] || '' });
      r += 4 + elen;
    }
    F(L.fields, '扩展', q, eend - q, ek.length + ' 个', 'TLS 的新功能几乎都通过扩展实现，包括 TLS 1.3 本身。', ek);
  }

  function rest(c, o, name, end) {
    end = end == null ? c.b.length : end;
    if (o >= end) return;
    var L = c.layer('data', name, o, end - o);
    F(L.fields, name, o, end - o, (end - o) + ' 字节', '');
  }

  function parse(b) {
    var c = new Ctx(b);
    try {
      if (b[0] >> 4 === 4 && (b[0] & 15) >= 5) ipv4(c, 0);   // tcpdump -x 不带以太网头部
      else eth(c, 0);
    } catch (e) { /* 截断的包：解析到哪里算哪里 */ }
    var last = c.layers[c.layers.length - 1];
    var covered = last ? last.off + last.len : 0;
    if (covered < b.length) {
      var L = c.layer('pad', '尾部', covered, b.length - covered);
      F(L.fields, '填充', covered, b.length - covered, (b.length - covered) + ' 字节', '以太网帧最短 60 字节，不够时补 0。');
    }
    return c.layers;
  }

  /* 读者粘贴的十六进制：支持 tcpdump -xx / -XX 的输出、Wireshark 的 hex stream、空格分隔的字节 */
  function readHex(text) {
    var out = '', lines = text.split(/\n/), re = /^\s*(?:0x)?[0-9a-f]{4,8}:\s+(.*)$/i;
    var dump = lines.some(function (l) { return re.test(l); });   // tcpdump 的格式：只取带偏移的行，跳过时间戳那一行
    lines.forEach(function (line) {
      var m = line.match(re);
      if (dump && !m) return;
      var part = m ? m[1].split(/\s{2,}/)[0] : line;
      out += part.replace(/[^0-9a-f]/gi, '');
    });
    if (out.length % 2) out = out.slice(0, -1);
    return out.toLowerCase();
  }

  /* ---------------- 界面 ---------------- */

  function mount(host) {
    var key = host.getAttribute('data-pkt');
    var def = key ? (window.PKTS || {})[key] : { title: host.getAttribute('data-title') || '数据包', hex: host.getAttribute('data-hex') };
    if (!def || !def.hex) { host.textContent = '找不到数据包：' + key; return; }
    host.innerHTML = '';
    var head = h('div', 'pv-head', host);
    h('span', 'pv-title', head, def.title);
    var size = h('span', 'pv-size', head);
    var legend = h('div', 'pv-legend', host);
    var body = h('div', 'pv-body', host);
    var dump = h('div', 'pv-dump', body);
    var tree = h('div', 'pv-tree', body);
    var info = h('div', 'pv-info', host);
    var pasteBox = null;
    if (host.hasAttribute('data-paste')) {
      pasteBox = h('details', 'pv-paste', host);
      h('summary', null, pasteBox, '拆你自己抓的包');
      h('p', null, pasteBox, '运行 sudo tcpdump -i eth0 -xx -c 1 port 53（网卡名换成你自己的），然后 dig example.com；把输出中 0x0000: 开头的几行粘贴到这里（也可以粘贴 Wireshark 的 Copy as Hex Stream）。数据只在你的浏览器中解析。');
      var ta = h('textarea', null, pasteBox);
      ta.rows = 5; ta.spellcheck = false; ta.placeholder = '0x0000:  4500 0039 b346 0000 4011 1958 ac11 0003 ...';
      var row = h('div', 'pv-paste-row', pasteBox);
      var go = h('button', 'dv-btn dv-go', row, '解析');
      var back = h('button', 'dv-btn', row, '恢复示例');
      go.onclick = function () { var hx = readHex(ta.value); if (hx.length >= 40) render(hx, '你粘贴的包'); };
      back.onclick = function () { render(def.hex, def.title); };
    }

    var bytesEl = [], rowsEl = [], owner = [], selected = null;

    function render(hx, title) {
      head.firstChild.textContent = title;
      var b = [];
      for (var i = 0; i < hx.length; i += 2) b.push(parseInt(hx.substr(i, 2), 16));
      var layers = parse(b);
      size.textContent = b.length + ' 字节';
      dump.innerHTML = ''; tree.innerHTML = ''; legend.innerHTML = '';
      bytesEl = []; rowsEl = []; owner = new Array(b.length); selected = null;

      // 每个字节归属最深的那个字段
      function claim(f, depth) {
        for (var i = f.o; i < f.o + f.l && i < b.length; i++) if (!owner[i] || owner[i].depth <= depth) owner[i] = { f: f, depth: depth };
        (f.kids || []).forEach(function (k) { k.L = f.L; claim(k, depth + 1); });
      }
      layers.forEach(function (L) { L.fields.forEach(function (f) { f.L = L; claim(f, 0); }); });

      // 图例：每一层的颜色和字节范围
      layers.forEach(function (L) {
        var chip = h('button', 'pv-chip pv-L-' + L.id, legend);
        h('span', 'pv-dot', chip);
        h('span', null, chip, L.name);
        h('span', 'pv-chip-n', chip, L.len + 'B');
        chip.title = '偏移 ' + L.off + '～' + (L.off + L.len - 1);
        chip.onclick = function () { select({ n: L.name, o: L.off, l: L.len, v: L.len + ' 字节', d: LAYER_DESC[L.id] || '', L: L }, true); };
      });

      // 十六进制：偏移 | 16 个字节 | ASCII
      for (var r = 0; r < b.length; r += 16) {
        var line = h('div', 'pv-row', dump);
        h('span', 'pv-off', line, ('000' + r.toString(16)).slice(-4));
        var hexs = h('span', 'pv-hex', line), asc = h('span', 'pv-asc', line);
        for (var j = r; j < r + 16; j++) {
          if (j >= b.length) { h('span', 'pv-b pv-empty', hexs, '  '); continue; }
          var L0 = owner[j] ? owner[j].f.L.id : 'pad';
          var sp = h('span', 'pv-b pv-L-' + L0, hexs, hex(b, j, 1));
          var ch = b[j] >= 32 && b[j] < 127 ? String.fromCharCode(b[j]) : '·';
          var sa = h('span', 'pv-a pv-L-' + L0, asc, ch);
          sp._i = sa._i = j;
          bytesEl[j] = [sp, sa];
        }
      }

      // 字段树
      layers.forEach(function (L) {
        var sec = h('details', 'pv-layer pv-L-' + L.id, tree);
        sec.open = L.id !== 'eth' || layers.length <= 3;
        var sm = h('summary', null, sec);
        h('span', 'pv-dot', sm);
        h('span', 'pv-lname', sm, L.name);
        h('span', 'pv-lsize', sm, L.len + ' 字节' + (L.note ? ' · ' + L.note : ''));
        L.fields.forEach(function (f) { addRow(sec, f, 0); });
      });
      function addRow(parent, f, depth) {
        var rowEl = h('div', 'pv-f' + (depth ? ' pv-kid' : ''), parent);
        rowEl.style.paddingLeft = (10 + depth * 14) + 'px';
        h('span', 'pv-fn', rowEl, f.n);
        h('span', 'pv-fv', rowEl, f.v);
        rowEl._f = f; f._row = rowEl;
        rowsEl.push(rowEl);
        (f.kids || []).forEach(function (k) { addRow(parent, k, depth + 1); });
      }
      var first = layers.filter(function (L) { return L.id !== 'eth'; })[0];
      showInfo(null);
      if (first) info.dataset.hint = '1';
    }

    var LAYER_DESC = {
      eth: '链路层：只负责把帧送到同一个网络中的下一跳。', ip: '网络层：负责跨越多个网络，把包从源主机送到目的主机。',
      tcp: '传输层：在两个程序之间提供可靠的字节流。', udp: '传输层：只加端口和校验和，其余什么都不管。',
      dns: '应用层：域名解析。', http: '应用层：网页的请求和响应，纯文本。', tls: 'TLS：在 TCP 之上加密。', icmp: '网络层的控制消息。'
    };

    function mark(f, cls, on) {
      if (!f) return;
      for (var i = f.o; i < f.o + f.l; i++) if (bytesEl[i]) { bytesEl[i][0].classList.toggle(cls, on); bytesEl[i][1].classList.toggle(cls, on); }
      if (f._row) f._row.classList.toggle(cls, on);
    }
    function showInfo(f) {
      info.innerHTML = '';
      if (!f) {
        h('span', 'pv-hint', info, '把鼠标移到（或点击）任意一个字节或字段上，查看它的含义。');
        return;
      }
      var top = h('div', 'pv-info-top', info);
      h('span', 'pv-info-n pv-L-' + f.L.id, top, f.n);
      h('span', 'pv-info-v', top, f.v);
      h('span', 'pv-info-o', top, '偏移 ' + f.o + (f.l > 1 ? '～' + (f.o + f.l - 1) : '') + ' · ' + f.l + ' 字节');
      if (f.d) h('div', 'pv-info-d', info, f.d);
    }
    var hover = null;
    function preview(f) {
      if (hover === f) return;
      mark(hover, 'is-hover', false);
      hover = f;
      mark(hover, 'is-hover', true);
      showInfo(f || selected);
    }
    function select(f, scroll) {
      mark(selected, 'is-sel', false);
      selected = selected === f && !scroll ? null : f;
      mark(selected, 'is-sel', true);
      showInfo(selected);
      if (selected && scroll && bytesEl[selected.o]) {
        var el = bytesEl[selected.o][0].parentNode.parentNode;
        dump.scrollTop = Math.max(0, el.offsetTop - dump.offsetTop - 20);
      }
      if (selected && selected._row) {
        var d = selected._row.parentNode;
        if (d.tagName === 'DETAILS' && !d.open) d.open = true;
        if (!scroll) return;
        tree.scrollTop = Math.max(0, selected._row.offsetTop - tree.offsetTop - 30);
      }
    }
    function byteField(t) { return t && t._i != null && owner[t._i] ? owner[t._i].f : null; }

    dump.addEventListener('mouseover', function (e) { var f = byteField(e.target); if (f) preview(f); });
    dump.addEventListener('mouseleave', function () { preview(null); });
    dump.addEventListener('click', function (e) {
      var f = byteField(e.target);
      if (!f) return;
      select(f, false);
      if (f._row) tree.scrollTop = Math.max(0, f._row.offsetTop - tree.offsetTop - 30);
    });
    tree.addEventListener('mouseover', function (e) { var r = e.target.closest('.pv-f'); if (r) preview(r._f); });
    tree.addEventListener('mouseleave', function () { preview(null); });
    tree.addEventListener('click', function (e) { var r = e.target.closest('.pv-f'); if (r) select(r._f, true); });

    render(def.hex, def.title);
  }

  window.PktView = {
    parse: parse,
    readHex: readHex,
    mountAll: function () {
      var list = document.querySelectorAll('.pktview');
      for (var i = 0; i < list.length; i++) mount(list[i]);
    }
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { window.PktView.mountAll(); });
  else setTimeout(function () { window.PktView.mountAll(); }, 0);
})();
