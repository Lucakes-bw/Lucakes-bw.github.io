/* 设计模式专栏《享元》：一行字幕「我们走吧我不走」——每个字一个完整对象，还是共享工厂里的字形（dp-flyweight） */
(function () {
  'use strict';

  var TEXT = ['我', '们', '走', '吧', '我', '不', '走'];
  var Y0 = 44, DY = 36, BH = 28;

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'tl', x: 20, y: 24, text: m.mode === 'fly' ? '这一行的 PlacedGlyph（外部状态）' : '这一行的 Glyph 对象', cls: 'muted' });
    texts.push({ id: 'tr', x: 440, y: 24, text: m.mode === 'fly' ? 'GlyphFactory 里的享元（内部状态）' : '每个对象自己的字形位图', cls: 'muted' });
    for (var i = 0; i < m.n; i++) {
      var c = TEXT[i];
      var col = m.colors[i] ? '，' + m.colors[i] : '';
      boxes.push({ id: 's' + i, x: 20, y: Y0 + i * DY, w: 250, h: BH,
        text: m.mode === 'fly' ? '[' + i + '] → 「' + c + '」 x=' + i * 48 + col : '[' + i + '] 「' + c + '」 x=' + i * 48 + ' 88 B', hl: hl['s' + i] });
      if (m.mode === 'fly') {
        arrows.push({ from: 's' + i, to: 'g' + m.pool.indexOf(c), hl: hl['a' + i] });
      } else {
        boxes.push({ id: 'b' + i, x: 440, y: Y0 + i * DY, w: 280, h: BH, text: '「' + c + '」位图 48×48 = 2304 B', hl: hl['b' + i] });
        arrows.push({ from: 's' + i, to: 'b' + i, hl: hl['a' + i] });
      }
    }
    if (m.mode === 'fly') {
      m.pool.forEach(function (c, j) {
        boxes.push({ id: 'g' + j, x: 440, y: Y0 + j * DY, w: 280, h: BH,
          text: '「' + c + '」位图 2304 B' + (m.poolColor[c] ? '，color=' + m.poolColor[c] : ''), hl: hl['g' + j] });
      });
    }
    var bytes = m.mode === 'fly' ? m.n * 16 + m.pool.length * (40 + 2304) : m.n * (88 + 2304);
    texts.push({ id: 'cnt', x: 20, y: Y0 + 7 * DY + 22,
      text: '字：' + m.n + ' 个　字形对象：' + (m.mode === 'fly' ? m.pool.length : m.n) + ' 个　光栅化：' + m.raster + ' 次　约 ' + bytes + ' 字节' });
    if (x.note) texts.push({ id: 'note', x: 20, y: Y0 + 7 * DY + 44, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m, mode) { m.mode = mode; m.n = 0; m.pool = []; m.raster = 0; m.colors = {}; m.poolColor = {}; }

  function naive(m, p, rec) {
    reset(m, 'naive');
    rec.step(1, '不共享：排版这一行字幕，每个字创建一个完整的 Glyph 对象。');
    for (var i = 0; i < TEXT.length; i++) {
      m.n = i + 1; m.raster++;
      var h = { hl: {} };
      h.hl['s' + i] = 'new'; h.hl['b' + i] = 'new'; h.hl['a' + i] = 'cur';
      var again = TEXT.indexOf(TEXT[i]) < i;
      if (again) { h.hl['b' + TEXT.indexOf(TEXT[i])] = 'warn'; h.hl['b' + i] = 'warn'; }
      rec.step(2, again ? '又是「' + TEXT[i] + '」：和第 ' + TEXT.indexOf(TEXT[i]) + ' 个字的位图一模一样，还是重新光栅化了一遍，又占了 2304 字节。'
        : '「' + TEXT[i] + '」：光栅化出一张 48×48 的位图，连同位置、颜色一起放进新对象。', h);
    }
    rec.step(2, '7 个字，7 个对象，7 张位图，其中 2 张是重复的。整条字幕 19440 个字只有 2315 种，按这个比例，重复的位图占了 88%。',
      { hl: { b0: 'warn', b4: 'warn', b2: 'warn', b6: 'warn' }, note: '实测：整条字幕 44.4 MB，光栅化 19440 次' });
  }

  function fly(m, p, rec) {
    reset(m, 'fly');
    rec.step(3, '享元：排版时不再自己光栅化，而是向 GlyphFactory 要字形。');
    for (var i = 0; i < TEXT.length; i++) {
      var c = TEXT[i], j = m.pool.indexOf(c), h = { hl: {} };
      m.n = i + 1;
      h.hl['s' + i] = 'new'; h.hl['a' + i] = 'cur';
      if (j < 0) {
        m.pool.push(c); m.raster++; j = m.pool.length - 1;
        h.hl['g' + j] = 'new';
        rec.step(5, '「' + c + '」第一次出现：工厂里没有，光栅化一次存进去，返回它的地址；这一行记下 PlacedGlyph（指针 + 位置）。', h);
      } else {
        h.hl['g' + j] = 'ok';
        rec.step(4, '「' + c + '」已经在工厂里：直接返回同一个 Glyph 的地址，不再光栅化。这一行只多了一个 16 字节的 PlacedGlyph（指针 + 位置）。', h);
      }
    }
    rec.step(6, '7 个字只有 5 个字形对象，光栅化 5 次。两个「我」、两个「走」指向同一个享元。',
      { hl: { a0: 'ok', a4: 'ok', a2: 'ok', a6: 'ok', g0: 'ok', g2: 'ok' }, note: '实测：整条字幕 5.6 MB，光栅化 2315 次' });
  }

  function mutate(m, p, rec) {
    fly(m, p, { step: function () {} });
    rec.step(6, '排版好了。现在做卡拉 OK：唱到第 0 个字，要把它染成黄色。', { hl: { s0: 'cur' } });
    m.poolColor['我'] = '黄';
    rec.step(7, '错误的写法：颜色放在了享元里，改的是工厂里那个共享的「我」……', { hl: { g0: 'warn', a0: 'cur' } });
    rec.step(7, '……于是第 4 个字（还没唱到）也跟着变黄了。享元被所有使用者共享，不能修改。', { hl: { g0: 'warn', s0: 'warn', s4: 'warn', a0: 'warn', a4: 'warn' }, note: '颜色、位置这些因字而异的东西是外部状态' });
    m.poolColor = {}; m.colors[0] = '黄';
    rec.step(6, '正确的写法：颜色是外部状态，放在 PlacedGlyph 里；工厂返回 const Glyph&，想改享元就编译不过。',
      { hl: { s0: 'ok', g0: 'ok' }, note: '只有第 0 个字变黄，享元没变' });
  }

  DSViz.register('dp-flyweight', {
    title: '享元：相同的字共享同一个字形',
    width: 740,
    height: Y0 + 7 * DY + 56,
    code: [
      'for (char32_t ch : U"我们走吧我不走")',
      '  line.push_back(FatGlyph{ch, rasterize(ch), x, y, color});   // 不共享',
      'const Glyph& g = factory.get(ch);                            // 享元',
      '  if (auto it = cache_.find(ch); it != cache_.end()) return it->second;',
      '  return cache_.emplace(ch, Glyph{ch, rasterize(ch)}).first->second;',
      'line.push_back(PlacedGlyph{&g, x, y});',
      'glyph.color = YELLOW;   // 错：颜色是外部状态'
    ],
    init: function () { var m = {}; reset(m, 'naive'); return m; },
    view: view,
    start: function (m, rec) { naive(m, {}, rec); },
    actions: [{ label: '不共享', run: naive }, { label: '享元', run: fly }, { label: '改了享元', run: mutate }]
  });
})();
