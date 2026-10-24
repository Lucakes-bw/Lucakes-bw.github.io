/* 《数据链路层：MAC 与 ARP》：交换机怎样学习 MAC 地址 */
(function () {
  'use strict';
  var HOSTS = ['h1', 'h2', 'h3', 'h4'];
  function mac(i) { return '02:…:0' + (i + 1); }
  var SW = { x: 30, y: 110, w: 120, h: 60 };

  function init() { return { table: [] }; }       // table: [{ host: 下标, port: 端口 }]

  function view(m, x) {
    var boxes = [{ id: 'sw', x: SW.x, y: SW.y, w: SW.w, h: SW.h, text: '交换机', hl: x.sw }];
    var arrows = [], hl = x.hl || {};
    HOSTS.forEach(function (h, i) {
      boxes.push({ id: h, x: 270, y: 30 + i * 62, w: 170, h: 40, text: h + '  ' + mac(i), sub: '端口 ' + (i + 1), hl: hl[h] });
      if (x.to && x.to.indexOf(i) >= 0) arrows.push({ from: 'sw', to: h, hl: hl[h] === 'ok' ? 'ok' : 'cur' });
    });
    m.table.forEach(function (e, k) {
      boxes.push({ id: 't' + e.host, x: 520, y: 52 + k * 46, w: 180, h: 36, text: mac(e.host) + ' → 端口 ' + e.port, hl: x.thl === e.host ? 'new' : null });
    });
    var texts = [{ id: 'th', x: 520, y: 34, text: 'MAC 地址表' + (m.table.length ? '' : '（空）'), cls: 'muted' }];
    if (x.frame) texts.push({ id: 'fr', x: SW.x, y: SW.y + SW.h + 34, text: x.frame, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function send(m, src, dst, rec) {
    var bcast = dst < 0;
    var frame = '帧：' + mac(src) + ' → ' + (bcast ? 'ff:ff:ff:ff:ff:ff' : mac(dst));
    var h = {}; h[HOSTS[src]] = 'cur';
    rec.step(0, HOSTS[src] + ' 发出一个帧，目的 MAC 是' + (bcast ? '广播地址 ff:ff:ff:ff:ff:ff' : ' ' + HOSTS[dst] + ' 的 ' + mac(dst)) + '。交换机从端口 ' + (src + 1) + ' 收到它。',
      { hl: h, sw: 'cur', frame: frame });
    var old = m.table.filter(function (e) { return e.host === src; })[0];
    if (!old) {
      m.table.push({ host: src, port: src + 1 });
      rec.step(0, '学习：交换机看帧的源 MAC，记下「' + mac(src) + ' 在端口 ' + (src + 1) + '」。', { hl: h, sw: 'cur', thl: src, frame: frame });
    } else {
      rec.step(0, '源 MAC ' + mac(src) + ' 已经在表中，刷新它的计时（长时间不出现的条目会被删除，Linux 网桥默认 300 秒）。', { hl: h, sw: 'cur', thl: src, frame: frame });
    }
    var known = !bcast && m.table.some(function (e) { return e.host === dst; });
    if (known) {
      var hh = {}; hh[HOSTS[src]] = 'cur'; hh[HOSTS[dst]] = 'ok';
      rec.step(0, '查表：目的 MAC 在端口 ' + (dst + 1) + '，只从这个端口转发。其他主机根本收不到这个帧。', { hl: hh, sw: 'cur', to: [dst], frame: frame });
    } else {
      var to = [], hf = {}; hf[HOSTS[src]] = 'cur';
      HOSTS.forEach(function (_, i) { if (i !== src) { to.push(i); hf[HOSTS[i]] = (i === dst) ? 'ok' : 'dim'; } });
      rec.step(0, (bcast ? '广播帧：' : '查表：目的 MAC 不在表中，') + '从除了端口 ' + (src + 1) + ' 以外的所有端口发出（泛洪）。' +
        (bcast ? '每台主机都收下并处理它，ARP 请求就是这样的广播。' : HOSTS[dst] + ' 收下；其他主机发现目的 MAC 不是自己，丢弃。'),
        { hl: hf, sw: 'cur', to: to, frame: frame });
    }
  }

  var NAMES = { h1: 0, h2: 1, h3: 2, h4: 3 };
  function parseHost(s, allowB) {
    s = String(s).trim().toLowerCase();
    if (allowB && (s === '广播' || s === 'b' || s === 'ff')) return -1;
    return NAMES[s];
  }

  DSViz.register('switch-learn', {
    title: '交换机的 MAC 地址学习',
    height: 290,
    init: init,
    view: view,
    start: function (m, rec) { rec.step(0, '交换机刚启动，MAC 地址表是空的。点「运行示例」，或者自己选择源和目的发送一个帧（目的可以填「广播」）。'); },
    actions: [{
      label: '运行示例',
      run: function (m, p, rec) {
        m.table = [];
        rec.step(0, 'MAC 地址表是空的。h1 要 ping h2，先要知道 h2 的 MAC 地址，于是发出 ARP 请求（广播）。');
        send(m, 0, -1, rec);
        send(m, 1, 0, rec);
        send(m, 0, 1, rec);
        send(m, 2, 3, rec);
        rec.step(0, '一段时间后，交换机学会了所有正在通信的主机的位置，帧几乎都只发往一个端口。这就是实验中 h3 只能抓到 ARP 请求、抓不到 h1 和 h2 之间的 ping 的原因。');
      }
    }, {
      label: '发送',
      fields: [{ key: 'src', label: '源', value: 'h3', size: 3 }, { key: 'dst', label: '目的', value: 'h1', size: 4 }],
      check: function (m, p) {
        var s = parseHost(p.src, false), d = parseHost(p.dst, true);
        if (s == null) return '源填 h1 到 h4';
        if (d == null) return '目的填 h1 到 h4，或者「广播」';
        return s === d ? '源和目的不能相同' : null;
      },
      run: function (m, p, rec) { send(m, parseHost(p.src, false), parseHost(p.dst, true), rec); }
    }, {
      label: '清空地址表',
      run: function (m, p, rec) { m.table = []; rec.step(0, 'MAC 地址表被清空（相当于交换机重启，或者所有条目都超时了）。'); }
    }]
  });
})();
