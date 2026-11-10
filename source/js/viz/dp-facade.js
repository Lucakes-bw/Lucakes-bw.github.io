/* 设计模式专栏《外观》：调用方自己按顺序调 4 个子系统 vs. 只调用 Thumbnailer 外观（dp-facade） */
(function () {
  'use strict';

  var SUBS = [
    { id: 'dm', name: 'Demuxer 解封装', y: 16 },
    { id: 'dec', name: 'Decoder 解码', y: 76 },
    { id: 'sc', name: 'Scaler 缩放', y: 136 },
    { id: 'enc', name: 'PngEncoder 编码', y: 196 }
  ];

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'ts', x: 470, y: 10, text: '子系统（FFmpeg 的 4 个库）', cls: 'muted' });
    boxes.push({ id: 'cli', x: 16, y: 106, w: 140, h: 44, text: '调用方', sub: m.cliSub, hl: hl.cli });
    if (m.facade) boxes.push({ id: 'fa', x: 236, y: 106, w: 160, h: 44, text: 'Thumbnailer 外观', sub: m.faSub, hl: hl.fa });
    SUBS.forEach(function (s) {
      boxes.push({ id: s.id, x: 470, y: s.y, w: 250, h: 44, text: s.name, sub: m.st[s.id], hl: hl[s.id] });
    });
    (x.calls || []).forEach(function (c, i) {   // 同一步里的几次调用依次飞过去，标签不重叠
      arrows.push({ from: c[0], to: c[1], label: c[2], pkt: c[2], hl: c[3] || 'cur', pktDelay: i * 450 });
    });
    if (m.facade && !x.calls) arrows.push({ from: 'cli', to: 'fa' });
    texts.push({ id: 'cnt', x: 16, y: 200, text: '调用方接触的函数：' + m.apis, cls: 'muted' });
    texts.push({ id: 'live', x: 16, y: 222, text: '没关闭的句柄：' + m.live, cls: 'muted' });
    if (x.note) texts.push({ id: 'note', x: 16, y: 262, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m, facade) {
    m.facade = facade; m.apis = 0; m.live = 0; m.cliSub = '要第 5 秒的缩略图'; m.faSub = '';
    m.st = { dm: '未打开', dec: '未打开', sc: '未打开', enc: '未打开' };
  }

  function noFacade(m, p, rec) {
    reset(m, false);
    rec.step(1, '不用外观：调用方要自己认识 4 个子系统。第一步打开文件（解封装）。', { calls: [['cli', 'dm', 'demux_open']] });
    m.st.dm = '已打开'; m.live = 1; m.apis = 1;
    m.st.dec = '已打开'; m.live = 2; m.apis = 3;
    rec.step(2, '找到视频流，用它的参数打开解码器。解码器依赖解封装器给的参数，所以顺序不能反。', { calls: [['cli', 'dec', 'dec_open']], hl: { dm: 'ok' } });
    m.st.dm = '跳到 3.458 s（关键帧）'; m.apis = 4;
    rec.step(3, 'seek 到第 5 秒——但只能落到 5 秒之前最近的关键帧，也就是 3.458 秒。', { calls: [['cli', 'dm', 'demux_seek']], hl: { dm: 'warn' } });
    m.st.dec = '解码 3.458 → 5.000 s，38 个包'; m.apis = 7;
    rec.step(4, '所以要一直读包、送进解码器、取出帧，直到 pts ≥ 5 秒。只解一帧就停，拿到的就是 3.458 秒的画面（实测，FFmpeg 也是这样）。', { calls: [['cli', 'dec', 'send / receive']], hl: { dec: 'cur' } });
    m.st.sc = '1280×720 → 320×180'; m.st.enc = '写出 thumb.png'; m.live = 4; m.apis = 11;
    rec.step(5, '缩放成 320×180，再编码成 PNG、写文件。又是两个子系统、5 个函数。', { calls: [['cli', 'sc', 'scale_run'], ['cli', 'enc', 'png_write']], hl: { sc: 'cur', enc: 'cur' } });
    m.st = { dm: '已关闭', dec: '已关闭', sc: '已关闭', enc: '已关闭' }; m.live = 0; m.apis = 15;
    rec.step(6, '最后按相反的顺序关闭 4 个句柄。中途任何一步失败提前 return，都要记得关掉前面已经打开的——漏一个就泄漏。', { hl: { dm: 'ok', dec: 'ok', sc: 'ok', enc: 'ok' } });
    rec.step(6, '每个要缩略图的地方都要把这 15 个函数按顺序写一遍。文中两位同事各写了一份：一份漏了第 4 步，一份在错误分支漏关了 2 个句柄。', { hl: { cli: 'warn' }, note: '不用外观：调用方知道一切，也要为一切负责' });
  }

  function withFacade(m, p, rec) {
    reset(m, true);
    m.faSub = '知道顺序和释放规则';
    rec.step(8, '用外观：调用方只认识 Thumbnailer，只调用一个函数。', { calls: [['cli', 'fa', 'save_thumbnail']], hl: { fa: 'cur' } });
    m.apis = 1; m.st.dm = '已打开，跳到 3.458 s'; m.st.dec = '解码到 5.000 s'; m.live = 2;
    rec.step(8, '外观替调用方打开文件和解码器、seek、一直解码到第 5 秒——第 4 步的细节只在外观里写一次。', { calls: [['fa', 'dm', 'open / seek'], ['fa', 'dec', 'decode']], hl: { fa: 'cur', dm: 'cur', dec: 'cur' } });
    m.st.sc = '1280×720 → 320×180'; m.st.enc = '写出 thumb.png'; m.live = 4;
    rec.step(8, '再缩放、编码、写文件。', { calls: [['fa', 'sc', 'scale'], ['fa', 'enc', 'encode']], hl: { sc: 'cur', enc: 'cur' } });
    m.st = { dm: '已关闭', dec: '已关闭', sc: '已关闭', enc: '已关闭' }; m.live = 0;
    m.cliSub = '拿到第 5.000 s 的画面';
    rec.step(8, '句柄由外观里的 unique_ptr 管理：正常返回、提前 return、抛异常都会按相反顺序关闭。调用方接触的函数从 15 个变成 1 个。', { hl: { cli: 'ok', fa: 'ok', dm: 'ok', dec: 'ok', sc: 'ok', enc: 'ok' }, note: '用外观：子系统没有变，只是多了一个替调用方安排它们的类' });
  }

  function bypass(m, p, rec) {
    reset(m, true);
    m.faSub = 'grab / save'; m.cliSub = '要所有关键帧的时间';
    rec.step(10, '外观没提供的功能：拖动进度条时要吸附到关键帧，需要所有关键帧的时间。', { hl: { cli: 'cur' } });
    m.st.dm = '读包，只看关键帧标记'; m.live = 1; m.apis = 3;
    rec.step(10, '外观不禁止直接使用子系统：调用方绕过 Thumbnailer，直接用 Demuxer 读包，看每个包的关键帧标记，不需要解码。', { calls: [['cli', 'dm', 'demux_read']], hl: { dm: 'cur' } });
    m.st.dm = '已关闭'; m.live = 0;
    m.cliSub = '拿到 7 个关键帧';
    rec.step(10, '7 个关键帧，解码 0 个包。外观照顾常见的用法，少见的用法仍然可以直接用子系统——外观是一条捷径，不是一堵墙。', { hl: { cli: 'ok', dm: 'ok' }, note: '外观不是封装：子系统对高级用户仍然开放' });
  }

  DSViz.register('dp-facade', {
    title: '外观：调用方自己安排 4 个子系统 vs. 交给 Thumbnailer',
    width: 740,
    height: 276,
    code: [
      'Demuxer* dm = demux_open(path);',
      'Decoder* dec = dec_open(dm, demux_find_video(dm));',
      'demux_seek(dm, t);               // 落在 t 之前的关键帧上',
      'while (pts < t) { demux_read(); dec_send(); dec_receive(); }',
      'scale_run(sc, f, &small);  png_write(enc, small, out);',
      'png_close(enc); scale_close(sc); dec_close(dec); demux_close(dm);',
      '// ---- 用外观 ----',
      'save_thumbnail("bbb-10s.mp4", 5.0, "thumb.png");',
      '// ---- 外观没提供的功能：直接用子系统 ----',
      'while (demux_read(dm, &pkt)) if (pkt.key) keys.push_back(pkt.pts);'
    ],
    init: function () { var m = {}; reset(m, false); return m; },
    view: view,
    start: function (m, rec) { noFacade(m, {}, rec); },
    actions: [{ label: '不用外观', run: noFacade }, { label: '用外观', run: withFacade }, { label: '绕过外观', run: bypass }]
  });
})();
