/* 设计模式专栏《责任链》：一个按键沿「对话框 → 字幕菜单 → 播放列表 → 播放器」传递，第一个处理它的层截住（dp-chain） */
(function () {
  'use strict';

  var LAYERS = [
    { id: 'dlg', name: '对话框', keys: '开着时吞掉所有键' },
    { id: 'menu', name: '字幕菜单', keys: '管 ↑ ↓ 回车 Esc' },
    { id: 'list', name: '播放列表', keys: '管 ↑ ↓ 回车 Esc' },
    { id: 'player', name: '播放器', keys: '管 空格 ← → ↑ ↓ Esc' }
  ];
  var W = 128, GAP = 34, X0 = 20, Y = 96;

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    boxes.push({ id: 'key', x: X0, y: 20, w: W, h: 40, text: m.key ? '按键：' + m.key : '（还没有按键）', hl: hl.key });
    texts.push({ id: 'tc', x: X0 + W + 20, y: 44, text: '↓ dialog.handle(k)：交给链头，从左往右依次问', cls: 'muted' });
    LAYERS.forEach(function (l, i) {
      var open = l.id === 'player' ? true : m[l.id];
      boxes.push({
        id: l.id, x: X0 + i * (W + GAP), y: Y, w: W, h: 56,
        text: l.name + (l.id === 'player' ? '' : (open ? '（开着）' : '（关着）')),
        sub: l.keys, hl: hl[l.id] || (open ? null : 'dim')
      });
      if (i > 0) arrows.push({ from: LAYERS[i - 1].id, to: l.id, hl: hl['a' + i] });
    });
    boxes.push({ id: 'end', x: X0 + 4 * (W + GAP) - 12, y: Y + 8, w: 60, h: 40, text: 'null', hl: hl.end });
    arrows.push({ from: 'player', to: 'end', hl: hl.a4 });
    if (m.result) boxes.push({ id: 'res', x: X0, y: 196, w: 420, h: 36, text: '结果：' + m.result, hl: hl.res });
    if (x.note) texts.push({ id: 'note', x: X0, y: 262, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function setup(m, key, dlg, menu, list) {
    m.key = key; m.dlg = dlg; m.menu = menu; m.list = list; m.result = '';
  }

  // 依次问每一层：skip 里的层把请求交给下一个，最后一层 who 处理（who 为 null 表示没人处理）
  function walk(m, rec, path, who, result, why) {
    rec.step(1, '按键先交给链头：dialog.handle(k)。', { hl: { key: 'cur', dlg: 'cur' } });
    for (var i = 0; i < path.length; i++) {
      var l = path[i], hl = {};
      hl[l.id] = 'cmp';
      hl['a' + i] = 'cur';
      rec.step(4, l.name + '：' + l.why + '，于是调用 KeyHandler::handle(k)，交给下一个。', { hl: hl });
    }
    var h = {};
    if (who) {
      h[who] = 'ok';
      h['a' + path.length] = 'cur';
      m.result = result;
      h.res = 'ok';
      rec.step(3, why, { hl: h, note: '处理了就直接 return，后面的层根本不会被问到' });
    } else {
      h.end = 'warn';
      h.a4 = 'cur';
      m.result = result;
      h.res = 'warn';
      rec.step(6, why, { hl: h, note: '链尾的 next_ 是空指针：返回「没人处理」。要不要报错、要不要放个兜底处理者，是设计时要定的' });
    }
  }

  function upMenu(m, p, rec) {
    setup(m, '↑', false, true, false);
    walk(m, rec, [{ id: 'dlg', name: '对话框', why: '关着' }], 'menu', '字幕菜单：上移',
      '字幕菜单开着，而且 ↑ 归它管：处理掉，返回「字幕菜单：上移」。播放器不会再收到这个 ↑，音量不变。');
  }

  function space(m, p, rec) {
    setup(m, '空格', false, true, false);
    walk(m, rec, [{ id: 'dlg', name: '对话框', why: '关着' }, { id: 'menu', name: '字幕菜单', why: '开着，但空格不归它管' }, { id: 'list', name: '播放列表', why: '关着' }],
      'player', '播放器：播放', '播放器管空格：开始播放。字幕菜单开着也不影响暂停 / 播放，因为菜单把不认识的键往下传了。');
  }

  function downList(m, p, rec) {
    setup(m, '↓', false, false, true);
    walk(m, rec, [{ id: 'dlg', name: '对话框', why: '关着' }, { id: 'menu', name: '字幕菜单', why: '关着' }], 'list', '播放列表：下移',
      '新加的播放列表开着，↓ 归它管。加这一层只写了一个类，再改一行组装代码：menu.setNext(&list)->setNext(&player)。');
  }

  function enter(m, p, rec) {
    setup(m, '回车', false, false, false);
    walk(m, rec, [{ id: 'dlg', name: '对话框', why: '关着' }, { id: 'menu', name: '字幕菜单', why: '关着' }, { id: 'list', name: '播放列表', why: '关着' }, { id: 'player', name: '播放器', why: '不管回车' }],
      null, '没人处理', '一路走到链尾也没人处理回车。');
  }

  function dialog(m, p, rec) {
    setup(m, '空格', true, true, false);
    rec.step(1, '对话框和字幕菜单都开着，按空格。', { hl: { key: 'cur', dlg: 'cur' } });
    m.result = '对话框：忽略';
    rec.step(3, '对话框是模态的：开着时所有键都归它，空格被吞掉，后面的字幕菜单和播放器都收不到。', { hl: { dlg: 'ok', res: 'ok' }, note: '链的顺序就是优先级：越靠前越先拿到请求' });
  }

  DSViz.register('dp-chain', {
    title: '一个按键沿着链走：谁处理了它，后面的层就收不到',
    width: 740,
    height: 276,
    code: [
      'std::string handle(Key k) override {     // 每一层都是这个样子',
      '  if (open && 归我管(k))',
      '    return 自己处理(k);                  // 处理了：到此为止',
      '  return KeyHandler::handle(k);          // 不归我管：交给下一个',
      '}',
      '// KeyHandler::handle：return next_ ? next_->handle(k) : "没人处理";'
    ],
    init: function () { var m = {}; setup(m, '', false, true, false); return m; },
    view: view,
    start: function (m, rec) { upMenu(m, {}, rec); },
    actions: [
      { label: '菜单开着按 ↑', run: upMenu },
      { label: '菜单开着按空格', run: space },
      { label: '列表开着按 ↓', run: downList },
      { label: '都关着按回车', run: enter },
      { label: '对话框开着按空格', run: dialog }
    ]
  });
})();
