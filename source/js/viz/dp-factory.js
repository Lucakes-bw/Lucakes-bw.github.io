/* 设计模式专栏《工厂方法》：注册表工厂按文件开头的字节识别格式（dp-factory）
 * 三段字节来自 projects/dp-lab/07-factory/registry/run.sh 从测试视频提取出的文件 */
(function () {
  'use strict';
  var FILES = {
    'bbb.mp4': '00 00 00 20 66 74 79 70 69 73 6f 6d 00 00 02 00',
    'bbb.h264': '00 00 00 01 06 05 ff ff a9 dc 45 e9 bd e6 d9 48',
    'bbb.aac': 'ff f1 4c 80 25 bf fc dc 00 4c 61 76 63 36 33 2e',
    'readme.txt': '48 65 6c 6c 6f 2c 20 77 6f 72 6c 64 21 0a 00 00'
  };
  function bytes(name) { return FILES[name].split(' ').map(function (h) { return parseInt(h, 16); }); }

  var PROBES = [
    { name: 'mp4', look: [4, 5, 6, 7], how: '第 4～7 字节是不是 "ftyp"',
      score: function (d) { return d[4] === 0x66 && d[5] === 0x74 && d[6] === 0x79 && d[7] === 0x70 ? 100 : 0; } },
    { name: 'h264', look: [0, 1, 2, 3, 4], how: '是否以 00 00 00 01 开头，第一个 NAL 是不是 SPS',
      score: function (d) { if (d[0] || d[1] || d[2] || d[3] !== 1) return 0; return (d[4] & 0x1f) === 7 ? 80 : 40; } },
    { name: 'aac', look: [0, 1], how: '前 12 位是不是同步字 0xFFF',
      score: function (d) { return d[0] === 0xff && (d[1] & 0xf6) === 0xf0 ? 60 : 0; } }
  ];
  var X0 = 18, BW = 40, BG = 4, BY = 50;

  function view(m, x) {
    x = x || {};
    var boxes = [], texts = [], d = bytes(m.file);
    texts.push({ id: 'fn', x: X0, y: 30, text: m.file + ' 的前 16 个字节' });
    d.forEach(function (b, i) {
      var hex = (b < 16 ? '0' : '') + b.toString(16);
      var ch = b >= 32 && b < 127 ? String.fromCharCode(b) : '·';
      boxes.push({ id: 'b' + i, x: X0 + i * (BW + BG), y: BY, w: BW, h: 30, text: hex, sub: ch, hl: x.look && x.look.indexOf(i) >= 0 ? (x.lookHl || 'cur') : null });
    });
    PROBES.forEach(function (p, i) {
      var s = x.scores && x.scores[p.name];
      boxes.push({ id: 'p' + p.name, x: X0 + i * 236, y: 130, w: 224, h: 38, text: p.name + (s == null ? '' : '：' + s + ' 分'),
        sub: s == null ? '' : s === 0 ? '不像' : s >= 80 ? '很有把握' : '有点像',
        hl: x.winner === p.name ? 'ok' : x.cur === p.name ? 'cur' : (s === 0 ? 'dim' : null) });
    });
    if (x.result) texts.push({ id: 'res', x: X0, y: 205, text: x.result });
    return { kind: 'free', boxes: boxes, texts: texts };
  }

  function run(name, m, rec) {
    m.file = name;
    var d = bytes(name), scores = {}, best = null, bestScore = 0;
    rec.step(1, '要识别的文件：' + name + '。注册表里登记了 3 种格式，每种格式都带着自己的探测函数。工厂把文件开头的字节依次交给它们打分。');
    PROBES.forEach(function (p) {
      var s = p.score(d);
      scores[p.name] = s;
      rec.step(3, p.name + ' 的探测函数检查：' + p.how + '。打 ' + s + ' 分。', { look: p.look, lookHl: s ? 'ok' : 'cur', cur: p.name, scores: Object.assign({}, scores) });
      if (s > bestScore) { bestScore = s; best = p.name; }
    });
    rec.step(5, best ? '得分最高的是 ' + best + '（' + bestScore + ' 分），工厂调用它登记的创建函数，返回一个 Parser。调用者只拿到 Parser 接口，不知道具体是哪个类。' : '所有探测函数都打了 0 分：无法识别，工厂返回空。',
      { scores: scores, winner: best, result: best ? '→ create() 返回 ' + best + ' 的解析器' : '→ 无法识别' });
  }

  DSViz.register('dp-factory', {
    title: '注册表工厂：按文件开头的字节识别格式',
    width: 740,
    height: 220,
    code: [
      'const Format* best = nullptr; int bestScore = 0;',
      'for (const auto& f : formats_) {',
      '    int s = f.probe(data);             // 每种格式自己判断「像不像我」',
      '    if (s > bestScore) { bestScore = s; best = &f; }',
      '}',
      'return best ? best->create() : nullptr;'
    ],
    init: function () { return { file: 'bbb.mp4' }; },
    view: view,
    start: function (m, rec) { run('bbb.mp4', m, rec); },
    actions: Object.keys(FILES).map(function (n) { return { label: n, run: function (m, p, rec) { run(n, m, rec); } }; })
  });
})();
