/* 《用户、权限与软件包》：权限的数字形式和符号形式互相转换 */
(function () {
  'use strict';
  var LETTERS = ['r', 'w', 'x'], WHO = ['所有者 u', '所属组 g', '其他人 o'], VAL = [4, 2, 1];

  function view(m, x) {
    var cells = [];
    for (var i = 0; i < 9; i++) {
      var on = m.bits[i];
      cells.push({ v: on ? LETTERS[i % 3] : '-', hl: (x.hl || {})[i] || (on ? 'ok' : 'dim') });
    }
    var digits = [0, 1, 2].map(function (g) { return VAL.reduce(function (s, v, k) { return s + (m.bits[g * 3 + k] ? v : 0); }, 0); });
    return {
      kind: 'multi', vars: x.vars || { '数字形式': digits.join(''), '符号形式': cells.map(function (c) { return c.v; }).join('') },
      parts: [
        { kind: 'array', x0: 90, cellW: 58, top: 86, cells: cells, noIndex: true, ptrs: x.ptrs || [], label: '每一位：r = 4，w = 2，x = 1' },
        { kind: 'array', x0: 90, cellW: 174, top: 160, noIndex: true, cells: WHO.map(function (w, g) { return { v: w + '：' + digits[g], hl: (x.ghl || {})[g] }; }) }
      ]
    };
  }

  DSViz.register('perm-bits', {
    title: '权限的数字形式和符号形式',
    height: 210,
    init: function () { return { bits: [1, 1, 1, 1, 0, 1, 1, 0, 0] }; },
    view: view,
    start: function (m, rec) { rec.step(0, '9 个权限位分成 3 组，每组 3 位。输入一个数字（如 754、640）或者符号形式（如 rwxr-x---），逐位查看它们的对应关系。'); },
    actions: [{
      label: '解析数字',
      fields: [{ key: 'num', label: '数字', value: '754', size: 4 }],
      check: function (m, p) { return /^[0-7]{3}$/.test(String(p.num)) ? null : '请输入 3 位八进制数字，每位 0 到 7'; },
      run: function (m, p, rec) {
        var s = String(p.num);
        m.bits = [0, 0, 0, 0, 0, 0, 0, 0, 0];
        rec.step(0, '从全部为 - 开始，逐组解析 ' + s + '。');
        for (var g = 0; g < 3; g++) {
          var d = +s[g], parts = [];
          for (var k = 0; k < 3; k++) {
            if (d & VAL[k]) { m.bits[g * 3 + k] = 1; parts.push(VAL[k] + '（' + LETTERS[k] + '）'); }
          }
          var hl = {}; for (var j = 0; j < 3; j++) hl[g * 3 + j] = m.bits[g * 3 + j] ? 'new' : 'cmp';
          var gh = {}; gh[g] = 'cur';
          rec.step(0, WHO[g] + '：' + d + (parts.length ? ' = ' + parts.join(' + ') : '，没有任何权限') + '。', { hl: hl, ghl: gh });
        }
        rec.step(0, s + ' 的符号形式是 ' + m.bits.map(function (b, i) { return b ? LETTERS[i % 3] : '-'; }).join('') + '。');
      }
    }, {
      label: '解析符号',
      fields: [{ key: 'sym', label: '符号', value: 'rw-r-----', size: 11 }],
      check: function (m, p) { return /^[r-][w-][x-][r-][w-][x-][r-][w-][x-]$/.test(String(p.sym)) ? null : '格式是 9 个字符，每 3 个一组，例如 rwxr-x---'; },
      run: function (m, p, rec) {
        var s = String(p.sym);
        m.bits = s.split('').map(function (c) { return c === '-' ? 0 : 1; });
        var digits = [];
        for (var g = 0; g < 3; g++) {
          var sum = 0, parts = [];
          for (var k = 0; k < 3; k++) if (m.bits[g * 3 + k]) { sum += VAL[k]; parts.push(LETTERS[k] + ' = ' + VAL[k]); }
          digits.push(sum);
          var hl = {}; for (var j = 0; j < 3; j++) hl[g * 3 + j] = 'cur';
          var gh = {}; gh[g] = 'cur';
          rec.step(0, WHO[g] + '：' + s.substr(g * 3, 3) + '，' + (parts.length ? parts.join('，') + '，合计 ' + sum : '全是 -，合计 0') + '。', { hl: hl, ghl: gh });
        }
        rec.step(0, s + ' 写成数字是 ' + digits.join('') + '，可以用 chmod ' + digits.join('') + ' 文件 设置。');
      }
    }]
  });
})();
