/* 《DNS》：递归解析器怎样从根开始一步步找到答案，以及缓存的作用 */
(function () {
  'use strict';
  var B = {
    cli: { x: 10, y: 100, w: 110, text: '你的电脑' },
    res: { x: 210, y: 100, w: 160, text: '递归解析器 1.1.1.1' },
    root: { x: 540, y: 20, w: 170, text: '根服务器' },
    tld: { x: 540, y: 100, w: 170, text: '.com 服务器' },
    auth: { x: 540, y: 180, w: 170, text: 'example.com 的服务器' }
  };

  function view(m, x) {
    var boxes = [], hl = x.hl || {};
    Object.keys(B).forEach(function (k) {
      var b = B[k];
      boxes.push({ id: k, x: b.x, y: b.y, w: b.w, h: 40, text: b.text, hl: hl[k] });
    });
    var arrows = x.arrow ? [{ from: x.arrow[0], to: x.arrow[1], label: x.arrow[2], hl: 'cur', curve: x.arrow[3] || 0 }] : [];
    var texts = [{ id: 'ch', x: 10, y: 196, text: '解析器的缓存：', cls: 'muted' }];
    (m.cache.length ? m.cache : ['（空）']).forEach(function (c, i) {
      texts.push({ id: 'c' + i, x: 10, y: 216 + i * 18, text: c, cls: 'muted' });
    });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  var DATA = {
    'example.com': { ns: 'elliott/hera.ns.cloudflare.com', a: '172.66.147.243 …', ttl: 300 },
    'github.com': { ns: 'ns-*.awsdns-*、dns*.nsone.net', a: '20.205.243.166', ttl: 60 }
  };

  function H(a, b) { var h = {}; h[a] = 'cur'; if (b) h[b] = 'cur'; return h; }

  function query(m, rec, name, n) {
    var tld = name.slice(name.lastIndexOf('.') + 1);
    var hasTld = m.cache.some(function (c) { return c.indexOf('.' + tld + ' 的服务器') === 0; });
    var hasName = m.cache.some(function (c) { return c.indexOf(name + ' A') === 0; });
    rec.step(0, '你的电脑问递归解析器：' + name + ' 的 IP 地址是什么？这是一个递归查询：「请你帮我查到底」。',
      { hl: H('cli', 'res'), arrow: ['cli', 'res', 'A?', -14], vars: { '查询次数': 1 } });
    var cnt = 1;
    if (hasName) {
      rec.step(0, '解析器的缓存中有这条记录，并且还没有过期，直接回答，不需要再问任何服务器。整个过程只有一个来回。',
        { hl: H('res', 'cli'), arrow: ['res', 'cli', '缓存', -14], vars: { '查询次数': cnt } });
      return;
    }
    if (!hasTld) {
      cnt++;
      rec.step(0, '解析器的缓存中什么都没有，只好从根服务器开始问。根服务器的地址是内置在解析器中的。',
        { hl: H('res', 'root'), arrow: ['res', 'root', 'A?', -14], vars: { '查询次数': cnt } });
      m.cache.push('.' + tld + ' 的服务器：a～m.gtld-servers.net（2 天）');
      rec.step(0, '根服务器不知道答案，但知道谁管 .' + tld + '：它返回 .' + tld + ' 服务器的名字和地址（转介，referral）。解析器把它记入缓存，有效期 172800 秒（2 天）。',
        { hl: H('root', 'res'), arrow: ['root', 'res', '转介', 14], vars: { '查询次数': cnt } });
    } else {
      rec.step(0, '解析器的缓存中已经有 .' + tld + ' 服务器的地址，跳过根服务器，直接从 .' + tld + ' 服务器开始。', { hl: H('res'), vars: { '查询次数': cnt } });
    }
    cnt++;
    rec.step(0, '解析器问 .' + tld + ' 服务器。', { hl: H('res', 'tld'), arrow: ['res', 'tld', 'A?', -14], vars: { '查询次数': cnt } });
    var d = DATA[name];
    if (m.cache.every(function (c) { return c.indexOf(name + ' 的服务器') !== 0; })) m.cache.push(name + ' 的服务器：' + d.ns);
    rec.step(0, '.' + tld + ' 服务器也不知道答案，但知道 ' + name + ' 由哪些服务器负责（它的 NS 记录），再次转介。',
      { hl: H('tld', 'res'), arrow: ['tld', 'res', '转介', 14], vars: { '查询次数': cnt } });
    cnt++;
    rec.step(0, '解析器问 ' + name + ' 的权威服务器。', { hl: H('res', 'auth'), arrow: ['res', 'auth', 'A?', -14], vars: { '查询次数': cnt } });
    m.cache.push(name + ' A ' + d.a + '（' + d.ttl + ' 秒）');
    rec.step(0, '权威服务器给出最终答案：A ' + d.a + '，TTL ' + d.ttl + ' 秒。解析器把答案记入缓存，' + d.ttl + ' 秒内再有人问就直接回答。',
      { hl: H('auth', 'res'), arrow: ['auth', 'res', '答案', 14], vars: { '查询次数': cnt } });
    rec.step(0, '解析器把答案交给你的电脑。对你的电脑来说，它只问了一次；解析器替它问了 ' + (cnt - 1) + ' 个服务器（迭代查询）。',
      { hl: H('res', 'cli'), arrow: ['res', 'cli', '答案', 14], vars: { '查询次数': cnt } });
  }

  DSViz.register('dns-resolve', {
    title: 'DNS 解析过程',
    height: 310,
    init: function () { return { cache: [] }; },
    view: view,
    start: function (m, rec) { rec.step(0, '依次点「第一次查询」「再查一次」「查另一个域名」，观察解析器的缓存怎样减少查询。'); },
    actions: [
      { label: '第一次查询', run: function (m, p, rec) { m.cache = []; query(m, rec, 'example.com'); } },
      { label: '再查一次', run: function (m, p, rec) { query(m, rec, 'example.com'); } },
      { label: '查另一个域名', run: function (m, p, rec) {
        var keep = m.cache.filter(function (c) { return c.indexOf('.com 的服务器') === 0; });
        if (!keep.length) {
          rec.step(0, '（先运行「第一次查询」，解析器的缓存中才有 .com 服务器的记录。这里假设已经查过 example.com。）');
          m.cache = ['.com 的服务器：a～m.gtld-servers.net（2 天）', 'example.com 的服务器：' + DATA['example.com'].ns, 'example.com A ' + DATA['example.com'].a + '（300 秒）'];
        }
        B.auth.text = 'github.com 的服务器';
        query(m, rec, 'github.com');
        B.auth.text = 'example.com 的服务器';
      } }
    ]
  });
})();
