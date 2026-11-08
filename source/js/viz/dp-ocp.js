/* 设计模式专栏《SOLID 原则（上）》：密码依次经过每条规则；加规则、换规则都不用改 validate（dp-ocp） */
(function () {
  'use strict';
  var COMMON = ['password', 'Passw0rd', '12345678', 'qwerty123'];
  var ALL = {
    min8: { text: '≥ 8 个字符', msg: '至少 8 个字符', ok: function (s) { return s.length >= 8; } },
    min12: { text: '≥ 12 个字符', msg: '至少 12 个字符', ok: function (s) { return s.length >= 12; } },
    digit: { text: '含数字', msg: '至少包含一个数字', ok: function (s) { return /\d/.test(s); } },
    upper: { text: '含大写字母', msg: '至少包含一个大写字母', ok: function (s) { return /[A-Z]/.test(s); } },
    common: { text: '非常见密码', msg: '不能使用常见密码', ok: function (s) { return COMMON.indexOf(s) < 0; } }
  };
  var X0 = 10, IN_W = 110, R_W = 108, GAP = 24, Y = 70, H = 40;

  function rx(i) { return X0 + IN_W + GAP + i * (R_W + GAP); }

  function view(m, x) {
    x = x || {};
    var st = x.st || {}, boxes = [], arrows = [], texts = [];
    boxes.push({ id: 'in', x: X0, y: Y, w: IN_W, h: H, text: '"' + m.pwd + '"', sub: '输入', hl: x.at === -1 ? 'cur' : null });
    m.rules.forEach(function (k, i) {
      boxes.push({ id: 'r' + k, x: rx(i), y: Y, w: R_W, h: H, text: ALL[k].text,
        sub: st[k] === 'ok' ? '✓ 通过' : st[k] === 'bad' ? '✗ 不通过' : '', hl: st[k] === 'ok' ? 'ok' : st[k] === 'bad' ? 'warn' : (x.fresh === k ? 'new' : null) });
      arrows.push({ from: i ? 'r' + m.rules[i - 1] : 'in', to: 'r' + k, hl: x.at === i ? 'cur' : null, pkt: x.at === i ? m.pwd.slice(0, 10) : null });
    });
    var last = m.rules.length;
    boxes.push({ id: 'out', x: rx(last), y: Y, w: 86, h: H, text: x.result || 'errors', hl: x.result ? (x.errors && x.errors.length ? 'warn' : 'ok') : null });
    arrows.push({ from: last ? 'r' + m.rules[last - 1] : 'in', to: 'out', hl: x.at === last ? 'cur' : null, pkt: x.at === last ? '结果' : null });
    texts.push({ id: 'cfg', x: X0, y: 30, text: '当前产品线：' + m.name + '（规则列表 ' + m.rules.length + ' 条）' });
    if (x.errors && x.errors.length) texts.push({ id: 'err', x: X0, y: 160, text: 'errors = { ' + x.errors.map(function (e) { return '"' + e + '"'; }).join(', ') + ' }', cls: 'muted' });
    if (x.note) texts.push({ id: 'note', x: X0, y: 184, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function run(m, rec, intro) {
    var st = {}, errors = [];
    rec.step(1, intro || ('校验密码 "' + m.pwd + '"。validate 只做一件事：把密码依次交给列表里的每条规则。'), { at: -1 });
    m.rules.forEach(function (k, i) {
      var ok = ALL[k].ok(m.pwd);
      rec.step(3, '第 ' + (i + 1) + ' 条规则「' + ALL[k].text + '」检查 "' + m.pwd + '"……', { at: i, st: Object.assign({}, st), errors: errors.slice() });
      st[k] = ok ? 'ok' : 'bad';
      if (!ok) errors.push(ALL[k].msg);
      rec.step(4, ok ? '通过，继续下一条。' : '不通过，把提示「' + ALL[k].msg + '」放进 errors。', { st: Object.assign({}, st), errors: errors.slice() });
    });
    rec.step(5, errors.length ? '共 ' + errors.length + ' 条规则不通过，返回 errors。' : '所有规则都通过，返回空的 errors。',
      { at: m.rules.length, st: st, errors: errors, result: errors.length ? errors.length + ' 条不通过' : '通过' });
  }

  function website(m) { m.name = '官网'; m.rules = ['min8', 'digit', 'upper']; }

  DSViz.register('dp-ocp', {
    title: '规则列表：加规则、换规则，validate 都不用改',
    width: 760,
    height: 200,
    code: [
      'std::vector<std::string> validate(const std::string& pwd, const std::vector<Rule>& rules) {',
      '    std::vector<std::string> errors;',
      '    for (const Rule& r : rules)',
      '        if (!r.ok(pwd)) errors.push_back(r.message);',
      '    return errors;',
      '}'
    ],
    init: function () { var m = { pwd: 'Passw0rd' }; website(m); return m; },
    view: view,
    start: function (m, rec) { run(m, rec); },
    actions: [
      { label: '校验', fields: [{ key: 'pwd', label: '密码', value: 'Passw0rd' }], run: function (m, p, rec) {
        m.pwd = String(p.pwd == null ? '' : p.pwd).slice(0, 24); run(m, rec);
      } },
      { label: '加一条规则：非常见密码', run: function (m, p, rec) {
        if (m.rules.indexOf('common') < 0) m.rules.push('common');
        rec.step(3, '新需求：不能使用常见密码。写一个新函数 notCommon()，在官网的规则列表末尾加上它。validate 一行都没改。', { fresh: 'common', note: '改动：新增 notCommon()，规则列表加 1 项；validate 改动 0 行' });
        run(m, rec, '再校验一次 "' + m.pwd + '"。');
      } },
      { label: '换成后台的规则', run: function (m, p, rec) {
        m.name = '后台'; m.rules = ['min12', 'digit', 'upper', 'common'];
        rec.step(3, '后台管理系统要求更严：至少 12 个字符。它有自己的一份规则列表，和官网的互不影响。', { fresh: 'min12', note: '改动：换一份规则列表；validate 改动 0 行' });
        run(m, rec, '用后台的规则校验 "' + m.pwd + '"。');
      } },
      { label: '恢复官网规则', run: function (m, p, rec) { website(m); run(m, rec); } }
    ]
  });
})();
