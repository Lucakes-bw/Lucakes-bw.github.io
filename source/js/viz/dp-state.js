/* 设计模式专栏《状态》：点事件按钮，看当前状态对象怎么处理它、要不要换状态、换的时候执行哪些退出 / 进入动作（dp-state） */
(function () {
  'use strict';

  var NAMES = { idle: '空闲', loading: '加载中', playing: '播放', buffering: '缓冲中', paused: '暂停', ended: '结束', error: '出错' };
  var CLS = { idle: 'Idle', loading: 'Loading', playing: 'Playing', buffering: 'Buffering', paused: 'Paused', ended: 'Ended', error: 'Error' };
  // 每个状态一列，避免箭头穿过别的框
  var POS = {
    idle: [20, 100], loading: [125, 100], playing: [230, 100], buffering: [335, 30],
    paused: [440, 170], ended: [545, 100], error: [650, 30]
  };
  var EVENTS = [
    ['open', '打开'], ['loaded', '加载完'], ['play', '播放'], ['pause', '暂停'], ['stall', '卡顿'],
    ['buffered', '缓冲好'], ['eof', '播完'], ['fail', '出错'], ['stop', '停止']
  ];
  // 规格：NEXT[状态][事件] = 下一个状态；没写的组合用 State 的默认版本：什么也不做
  var NEXT = {
    idle: { open: 'loading' },
    loading: { loaded: 'playing', fail: 'error', stop: 'idle' },
    playing: { pause: 'paused', stall: 'buffering', eof: 'ended', fail: 'error', stop: 'idle' },
    paused: { play: 'playing', fail: 'error', stop: 'idle' },
    buffering: { buffered: 'playing', pause: 'paused', fail: 'error', stop: 'idle' },
    ended: { play: 'playing', open: 'loading', stop: 'idle' },
    error: { open: 'loading', stop: 'idle' }
  };
  var ENTER = {
    idle: ['关闭文件', function (d) { d.file = false; }],
    loading: ['打开文件，开始解封装', function (d) { d.file = true; }],
    playing: ['打开音频设备，开始走时钟', function (d) { d.audio = true; }],
    buffering: ['显示转圈图标', function (d) { d.spinner = true; }],
    error: ['关闭文件，显示错误', function (d) { d.file = false; }]
  };
  var EXIT = {
    playing: ['关闭音频设备', function (d) { d.audio = false; }],
    buffering: ['隐藏转圈图标', function (d) { d.spinner = false; }]
  };

  function H() {   // H(键, 值, 键, 值, ...) → 高亮对象
    var o = {};
    for (var i = 0; i + 1 < arguments.length; i += 2) o[arguments[i]] = arguments[i + 1];
    return o;
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    Object.keys(POS).forEach(function (k) {
      boxes.push({ id: k, x: POS[k][0], y: POS[k][1], w: 90, h: 38, text: NAMES[k], hl: hl[k] || (k === m.state && !x.keep ? 'cur' : undefined) });
    });
    if (m.from) {
      var a = POS[m.from], b = POS[m.to];
      var curve = a[1] === b[1] && Math.abs(a[0] - b[0]) > 120 ? 46 : 0;
      arrows.push({ from: m.from, to: m.to, label: m.ev, hl: hl.arrow || 'cur', curve: curve });
    }
    var d = m.dev;
    texts.push({ id: 'dev', x: 20, y: 236, text: '文件：' + (d.file ? '打开' : '关闭') + '    音频设备：' + (d.audio ? '打开' : '关闭') + '    转圈图标：' + (d.spinner ? '显示' : '隐藏'), cls: hl.dev ? '' : 'muted' });
    texts.push({ id: 'his', x: 20, y: 258, text: '经过：' + m.history.slice(-8).map(function (s) { return NAMES[s]; }).join(' → '), cls: 'muted' });
    if (x.note) texts.push({ id: 'note', x: 20, y: 280, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m) {
    m.state = 'idle'; m.from = null; m.to = null; m.ev = '';
    m.dev = { file: false, audio: false, spinner: false };
    m.history = ['idle'];
  }

  function fire(key, label) {
    return function (m, p, rec) {
      var s = m.state, cls = CLS[s], hl = {};
      m.from = null;
      rec.step(1, '点了「' + label + '」：界面调用 p.' + key + '()。Player 自己不判断现在是什么状态。', { hl: hl });
      hl[s] = 'cmp';
      rec.step(2, 'Player 直接把事件交给当前的状态对象 ' + cls + '：state_->' + key + '(*this)。', { hl: hl });
      var t = NEXT[s][key];
      if (!t) {
        hl[s] = 'ok';
        rec.step(4, cls + ' 没有重写 ' + key + '()，用的是基类 State 的默认版本：什么也不做。按钮没有反应，状态还是「' + NAMES[s] + '」。', { hl: hl, note: '「' + NAMES[s] + '」状态下「' + label + '」是无效事件，不需要在 Player 里写 if 挡住它' });
        return;
      }
      m.from = s; m.to = t; m.ev = label;
      rec.step(3, cls + '::' + key + '() 决定下一个状态是 ' + CLS[t] + '，调用 p.transitionTo(' + t + '())。', { hl: H('arrow', 'cur', s, 'cmp', t, 'cmp') });
      if (EXIT[s]) {
        EXIT[s][1](m.dev);
        rec.step(6, '退出动作：' + cls + '::exit() ' + EXIT[s][0] + '。', { hl: H('arrow', 'cur', s, 'warn', 'dev', 1) });
      } else {
        rec.step(6, cls + ' 没有退出动作。', { hl: H('arrow', 'cur', s, 'cmp') });
      }
      m.state = t;
      m.history.push(t);
      rec.step(7, 'state_ = &next：当前状态换成 ' + CLS[t] + '。以后的事件都交给它处理。', { hl: H('arrow', 'cur', t, 'new') });
      if (ENTER[t]) {
        ENTER[t][1](m.dev);
        rec.step(8, '进入动作：' + CLS[t] + '::enter() ' + ENTER[t][0] + '。', { hl: H('arrow', 'ok', t, 'ok', 'dev', 1), note: NAMES[s] + ' --' + label + '--> ' + NAMES[t] });
      } else {
        rec.step(8, CLS[t] + ' 没有进入动作。', { hl: H('arrow', 'ok', t, 'ok'), note: NAMES[s] + ' --' + label + '--> ' + NAMES[t] });
      }
    };
  }

  var actions = EVENTS.map(function (e) { return { label: e[1], run: fire(e[0], e[1]) }; });
  actions.push({
    label: '重置', run: function (m, p, rec) {
      reset(m);
      rec.step(1, '回到空闲状态，设备全部关闭。', { note: '点上面的事件按钮，试试在不同状态下点同一个按钮' });
    }
  });

  DSViz.register('dp-state', {
    title: '状态模式：点事件按钮，看状态对象怎么处理',
    width: 760,
    height: 292,
    code: [
      'p.event();                         // 界面按钮、解码线程发来的事件',
      '  state_->event(*this);            // Player 不判断状态，交给当前状态对象',
      '    p.transitionTo(next());        // 当前状态决定下一个状态',
      '    /* 没重写：State 的默认版本什么也不做 */',
      'void Player::transitionTo(State& next) {',
      '  state_->exit(*this);             // 退出动作',
      '  state_ = &next;',
      '  state_->enter(*this);            // 进入动作',
      '}'
    ],
    init: function () { var m = {}; reset(m); return m; },
    view: view,
    start: function (m, rec) {
      rec.step(1, '播放器现在是「空闲」状态。点下面的事件按钮发事件，比如先点「打开」「加载完」，再试试「卡顿」「暂停」「停止」。', { note: '同一个按钮在不同状态下的效果不同，这些差别都写在各个状态类里' });
    },
    actions: actions
  });
})();
