/* lucky-term 主题脚本：配色方案、阅读记录与统计、搜索、阅读进度、代码复制、目录高亮、评论 */
(function () {
  'use strict';

  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => Array.from(el.querySelectorAll(s));
  const esc = s => s.replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const store = {
    get(k, d) { try { const v = localStorage.getItem(k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* 隐私模式下忽略 */ } }
  };

  /* ---------- 配色方案 ---------- */
  // 首屏的方案由 head 里的脚本设好；这里负责切换、预览和菜单
  const html = document.documentElement;
  const SCHEMES = window.LT.schemes;
  const schemeById = new Map(SCHEMES.list.map(s => [s.id, s]));
  let savedScheme = html.getAttribute('data-scheme');

  function applyScheme(id) {
    const s = schemeById.get(id);
    if (!s) return;
    html.setAttribute('data-scheme', id);
    html.setAttribute('data-theme', s.mode);
    $$('[data-scheme-toggle]').forEach(el => { el.title = `配色：${s.name}`; });
    $$('.scheme-item').forEach(li => li.setAttribute('aria-selected', String(li.dataset.schemeId === id)));
    const meta = $('meta[name="theme-color"]');
    if (meta) meta.content = getComputedStyle(html).getPropertyValue('--bg').trim();
  }
  // 预览只换颜色；setScheme 才保存，并同步评论区
  function setScheme(id) {
    if (!schemeById.has(id)) return;
    applyScheme(id);
    savedScheme = id;
    try { localStorage.setItem('lt-scheme', id); } catch (e) { /* ignore */ }
    const frame = $('iframe.giscus-frame');
    if (frame) frame.contentWindow.postMessage({ giscus: { setConfig: { theme: giscusTheme() } } }, 'https://giscus.app');
  }
  const revertScheme = () => { if (html.getAttribute('data-scheme') !== savedScheme) applyScheme(savedScheme); };
  applyScheme(savedScheme);

  // 没手动选过时跟随系统深浅色
  const darkMQ = matchMedia('(prefers-color-scheme: dark)');
  const followSystem = () => {
    let picked = null;
    try { picked = localStorage.getItem('lt-scheme'); } catch (e) { /* ignore */ }
    if (picked) return;
    savedScheme = SCHEMES[darkMQ.matches ? 'dark' : 'light'];
    applyScheme(savedScheme);
  };
  if (darkMQ.addEventListener) darkMQ.addEventListener('change', followSystem);

  const picker = $('[data-scheme-picker]');
  if (picker) {
    const toggle = $('[data-scheme-toggle]', picker);
    const menu = $('[data-scheme-menu]', picker);
    const list = $('.scheme-list', menu);
    const items = $$('.scheme-item', menu);
    let cur = -1;

    const highlight = (i, preview) => {
      cur = i;
      items.forEach((li, j) => li.classList.toggle('is-active', j === i));
      if (i < 0) return;
      items[i].scrollIntoView({ block: 'nearest' });
      if (preview) applyScheme(items[i].dataset.schemeId);
    };
    const openMenu = () => {
      menu.hidden = false;
      toggle.setAttribute('aria-expanded', 'true');
      highlight(items.findIndex(li => li.dataset.schemeId === savedScheme), false);
      list.focus();
    };
    const closeMenu = (commit) => {
      if (menu.hidden) return;
      if (commit && cur >= 0) setScheme(items[cur].dataset.schemeId);
      else revertScheme();
      menu.hidden = true;
      toggle.setAttribute('aria-expanded', 'false');
    };

    toggle.addEventListener('click', () => (menu.hidden ? openMenu() : closeMenu(false)));
    items.forEach((li, i) => {
      li.addEventListener('mouseenter', () => highlight(i, true));
      li.addEventListener('click', () => { highlight(i, false); closeMenu(true); toggle.focus(); });
    });
    // 鼠标移出列表就恢复，键盘选中的那项留到 Enter 再说
    list.addEventListener('mouseleave', () => { highlight(items.findIndex(li => li.dataset.schemeId === savedScheme), false); revertScheme(); });
    list.addEventListener('keydown', e => {
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        e.preventDefault();
        const d = e.key === 'ArrowDown' ? 1 : -1;
        highlight((cur + d + items.length) % items.length, true);
      } else if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        closeMenu(true);
        toggle.focus();
      } else if (e.key === 'Escape') {
        e.stopPropagation();
        closeMenu(false);
        toggle.focus();
      } else if (e.key === 'Tab') closeMenu(false);
    });
    document.addEventListener('click', e => { if (!picker.contains(e.target)) closeMenu(false); });
  }

  /* ---------- 已读记录（保存在本机浏览器） ---------- */
  const READ_KEY = 'lt-read';
  const READ_AT_KEY = 'lt-read-at';   // url → 标记已读的时间戳，用于阅读统计；更早的记录没有时间
  const readSet = new Set(store.get(READ_KEY, []));
  const readAt = store.get(READ_AT_KEY, {});
  const saveRead = () => { store.set(READ_KEY, Array.from(readSet)); store.set(READ_AT_KEY, readAt); };
  const markRead = url => { readSet.add(url); readAt[url] = Date.now(); saveRead(); };
  const unmarkRead = url => { readSet.delete(url); delete readAt[url]; saveRead(); };

  function asciiBar(done, total, width) {
    const n = total ? Math.round(done / total * width) : 0;
    return '[' + '#'.repeat(n) + '-'.repeat(width - n) + ']';
  }

  function renderReadState() {
    $$('[data-read-url]').forEach(el => el.classList.toggle('is-read', readSet.has(el.dataset.readUrl)));

    const dataEl = $('#series-data');
    if (!dataEl) return;
    let series = [];
    try { series = JSON.parse(dataEl.textContent); } catch (e) { return; }

    series.forEach(s => {
      const done = s.posts.filter(p => readSet.has(p.url)).length;
      $$(`[data-series="${s.key}"]`).forEach(node => {
        const bar = $('[data-bar]', node);
        if (bar) bar.textContent = asciiBar(done, s.posts.length, bar.textContent.length - 2);
        const count = $('[data-count]', node);
        if (count) count.textContent = done;
        node.classList.toggle('is-done', s.posts.length > 0 && done === s.posts.length);
      });
    });

    // 侧栏 next：每个专栏下一篇没读的
    series.forEach(s => {
      const li = $(`[data-next="${s.key}"]`);
      if (!li || !s.posts.length) return;
      const next = s.posts.find(p => !readSet.has(p.url));
      const a = $('a', li);
      if (next) { a.href = next.url; a.textContent = next.title; }
      else { a.removeAttribute('href'); a.textContent = '✓ 已全部读完'; a.classList.add('c-accent'); }
    });

    // 继续阅读：按路线顺序找到第一篇没读过的文章
    const resume = $('[data-resume]');
    if (resume && readSet.size) {
      for (const s of series) {
        const next = s.posts.find(p => !readSet.has(p.url));
        if (next) {
          const a = $('a', resume);
          a.href = next.url;
          a.textContent = `${s.name} · ${next.title}`;
          resume.hidden = false;
          break;
        }
      }
    }
  }

  const markBox = $('[data-mark-read]');
  function updateMarkBox() {
    if (!markBox) return;
    const read = readSet.has(markBox.dataset.markRead);
    markBox.classList.toggle('is-read', read);
    $('.mark-text', markBox).textContent = read ? '已读完这一篇 ✓' : 'touch .read';
    $('[data-mark-btn]', markBox).textContent = read ? '标记为未读' : '标记为已读';
  }
  if (markBox) {
    $('[data-mark-btn]', markBox).addEventListener('click', () => {
      const url = markBox.dataset.markRead;
      readSet.has(url) ? unmarkRead(url) : markRead(url);
      updateMarkBox();
    });
    updateMarkBox();
  }

  /* ---------- 阅读进度条 + 读到文末自动标记已读 ---------- */
  const content = $('#post-content');
  const bar = $('#read-progress');
  if (content && bar) {
    let autoMarked = false;
    const onScroll = () => {
      const rect = content.getBoundingClientRect();
      const total = rect.height - window.innerHeight * 0.6;
      const pct = Math.min(1, Math.max(0, -rect.top / Math.max(total, 1)));
      bar.style.width = (pct * 100).toFixed(1) + '%';
      if (pct >= 0.98 && !autoMarked && markBox && !readSet.has(markBox.dataset.markRead)) {
        autoMarked = true;
        markRead(markBox.dataset.markRead);
        updateMarkBox();
      }
    };
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();
  }

  renderReadState();

  /* ---------- 首页统计面板 ---------- */
  // 每个标签页是一个 { label, compute(since) } ；以后加「练习」只要在 STAT_TABS 里多写一项
  const statsEl = $('[data-stats]');
  if (statsEl) {
    const DAY = 864e5;
    const pad = n => String(n).padStart(2, '0');
    const dayKey = t => { const d = new Date(t); return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`; };
    const startOfDay = t => { const d = new Date(t); d.setHours(0, 0, 0, 0); return d.getTime(); };
    const fmtWords = n => (n >= 1e4 ? `${(n / 1e4).toFixed(1)} 万字` : `${n.toLocaleString('en-US')} 字`);
    const fmtHour = h => {
      const part = h < 6 ? '凌晨' : h < 12 ? '上午' : h === 12 ? '中午' : h < 18 ? '下午' : '晚上';
      return `${part} ${h > 12 ? h - 12 : h} 点`;
    };
    const mode = arr => {
      const cnt = new Map();
      arr.forEach(x => cnt.set(x, (cnt.get(x) || 0) + 1));
      let best = null, max = 0;
      cnt.forEach((n, x) => { if (n > max) { max = n; best = x; } });
      return best;
    };
    const longestRun = days => {
      const ts = Array.from(new Set(days)).map(k => new Date(k + 'T00:00:00').getTime()).sort((a, b) => a - b);
      let best = 0, run = 0;
      ts.forEach((t, i) => { run = i && Math.round((t - ts[i - 1]) / DAY) === 1 ? run + 1 : 1; best = Math.max(best, run); });
      return best;
    };

    let posts = [];
    try { posts = JSON.parse($('#posts-data').textContent); } catch (e) { /* ignore */ }
    const postByUrl = new Map(posts.map(p => [p.url, p]));
    const totalWords = posts.reduce((n, p) => n + p.words, 0);

    const STAT_TABS = {
      reading: {
        label: '我的阅读',
        compute(since) {
          const all = Array.from(readSet).map(url => ({ at: readAt[url], post: postByUrl.get(url) })).filter(e => e.post);
          const list = since ? all.filter(e => e.at >= since) : all;
          const timed = list.filter(e => e.at);
          const words = list.reduce((n, e) => n + e.post.words, 0);
          const days = timed.map(e => dayKey(e.at));
          const run = longestRun(days);
          const peak = mode(timed.map(e => new Date(e.at).getHours()));
          const series = mode(list.map(e => e.post.series).filter(Boolean));
          const heat = new Map();
          all.forEach(e => { if (e.at) { const k = dayKey(e.at); heat.set(k, (heat.get(k) || 0) + 1); } });

          let note;
          if (!all.length) note = '还没有阅读记录。读到文章末尾或点「标记为已读」，就会记在这里。';
          else if (!list.length) note = '这段时间没有读完的文章。';
          else {
            const min = Math.round(words / 300);
            const time = min >= 60 ? `${Math.floor(min / 60)} 小时 ${min % 60} 分钟` : `${Math.max(1, min)} 分钟`;
            note = `按每分钟 300 字估算，你在这里读了约 ${time}，占全部笔记的 ${(words / totalWords * 100).toFixed(1)}%。`;
            const untimed = list.length - timed.length;
            if (untimed) note += ` 有 ${untimed} 篇是之前的已读记录，没有阅读时间，不计入天数和热力图。`;
          }
          return {
            tiles: [
              ['已读文章', `${list.length} 篇`],
              ['读过的字数', fmtWords(words)],
              ['阅读天数', `${new Set(days).size} 天`],
              ['最长连续', run ? `${run} 天` : '-'],
              ['高峰时段', peak === null ? '-' : fmtHour(peak)],
              ['最常读的专栏', series || '-']
            ],
            heat,
            unit: '篇',
            note
          };
        }
      }
    };

    const tabsEl = $('[data-stats-tabs]');
    const tilesEl = $('[data-stat-tiles]');
    const heatEl = $('[data-heatmap]');
    const noteEl = $('[data-stats-note]');
    let tab = 'reading';
    let range = 'all';

    tabsEl.innerHTML = Object.keys(STAT_TABS).map(k =>
      `<button type="button" role="tab" data-tab="${k}" aria-selected="${k === tab}">${STAT_TABS[k].label}</button>`).join('');

    const level = n => (n <= 0 ? 0 : n === 1 ? 1 : n === 2 ? 2 : n <= 4 ? 3 : 4);

    function renderStats() {
      const today = startOfDay(Date.now());
      const since = range === 'all' ? 0 : today - (Number(range) - 1) * DAY;
      const r = STAT_TABS[tab].compute(since);

      tilesEl.innerHTML = r.tiles.map(([k, v]) => `<div class="stat-tile"><dt>${esc(k)}</dt><dd>${esc(v)}</dd></div>`).join('');
      noteEl.textContent = r.note;

      // 热力图：列是周（周一在上），最后一列是本周；列数按容器宽度来
      const cell = 14, gap = 4;
      const weeks = Math.max(8, Math.min(53, Math.floor((heatEl.clientWidth + gap) / (cell + gap))));
      const dow = (new Date(today).getDay() + 6) % 7;            // 周一 = 0
      const start = today - (dow + (weeks - 1) * 7) * DAY;
      let html = '';
      for (let i = 0; i < weeks * 7; i++) {
        const t = start + i * DAY;
        if (t > today) { html += '<i class="is-future"></i>'; continue; }
        const k = dayKey(t);
        const n = r.heat.get(k) || 0;
        const out = since && t < since ? ' is-out' : '';
        html += `<i class="l${level(n)}${out}" title="${k} · ${n ? `读了 ${n} ${r.unit}` : '没有阅读'}"></i>`;
      }
      heatEl.style.setProperty('--weeks', weeks);
      heatEl.innerHTML = html;
      heatEl.setAttribute('aria-label', `最近 ${weeks} 周的阅读热力图`);
    }

    tabsEl.addEventListener('click', e => {
      const b = e.target.closest('[data-tab]');
      if (!b) return;
      tab = b.dataset.tab;
      $$('[data-tab]', tabsEl).forEach(x => x.setAttribute('aria-selected', String(x === b)));
      renderStats();
    });
    $('[data-stats-range]').addEventListener('click', e => {
      const b = e.target.closest('[data-range]');
      if (!b) return;
      range = b.dataset.range;
      $$('[data-range]', statsEl).forEach(x => x.setAttribute('aria-pressed', String(x === b)));
      renderStats();
    });
    let resizeTimer;
    window.addEventListener('resize', () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(renderStats, 150); });

    statsEl.hidden = false;
    renderStats();
  }

  /* ---------- 代码块：语言标签 + 复制按钮 ---------- */
  $$('.md figure.highlight').forEach(fig => {
    const lang = (fig.className.replace('highlight', '').trim() || 'text').split(/\s+/)[0];
    const table = $('table', fig);
    if (table) {
      const scroll = document.createElement('div');
      scroll.className = 'code-scroll';
      table.parentNode.insertBefore(scroll, table);
      scroll.appendChild(table);
    }
    const head = document.createElement('div');
    head.className = 'code-head';
    head.innerHTML = `<span>${lang === 'plaintext' ? 'text' : lang}</span><button type="button" class="code-copy">copy</button>`;
    fig.insertBefore(head, fig.firstChild);
    $('.code-copy', head).addEventListener('click', e => {
      const lines = $$('.code .line', fig).map(l => l.textContent);
      const text = lines.length ? lines.join('\n') : fig.innerText;
      const btn = e.currentTarget;
      const done = () => {
        btn.textContent = 'copied ✓';
        btn.classList.add('is-done');
        setTimeout(() => { btn.textContent = 'copy'; btn.classList.remove('is-done'); }, 1600);
      };
      if (navigator.clipboard) navigator.clipboard.writeText(text).then(done);
      else {
        const ta = document.createElement('textarea');
        ta.value = text; document.body.appendChild(ta); ta.select();
        document.execCommand('copy'); ta.remove(); done();
      }
    });
  });

  /* ---------- 目录高亮 ---------- */
  const tocLinks = $$('#toc a');
  if (tocLinks.length && 'IntersectionObserver' in window) {
    const map = new Map();
    tocLinks.forEach(a => {
      const id = decodeURIComponent(a.getAttribute('href').slice(1));
      const h = document.getElementById(id);
      if (h) map.set(h, a);
    });
    const visible = new Set();
    const io = new IntersectionObserver(entries => {
      entries.forEach(en => en.isIntersecting ? visible.add(en.target) : visible.delete(en.target));
      const heads = Array.from(map.keys());
      let current = heads.filter(h => visible.has(h))[0];
      if (!current) current = heads.filter(h => h.getBoundingClientRect().top < 120).pop();
      tocLinks.forEach(a => a.classList.remove('is-active'));
      if (current) map.get(current).classList.add('is-active');
    }, { rootMargin: '-80px 0px -60% 0px' });
    map.forEach((_, h) => io.observe(h));
  }

  /* ---------- 搜索 ---------- */
  const modal = $('#search-modal');
  const input = $('#search-input');
  const results = $('#search-results');
  let index = null;
  let active = -1;

  async function loadIndex() {
    if (index) return index;
    const xml = await fetch(window.LT.search).then(r => r.text());
    const doc = new DOMParser().parseFromString(xml, 'text/xml');
    index = $$('entry', doc).map(e => {
      const tmp = document.createElement('div');
      tmp.innerHTML = (e.querySelector('content') || {}).textContent || '';
      tmp.querySelectorAll('.gutter').forEach(g => g.remove());   // 去掉代码行号
      return {
        title: (e.querySelector('title') || {}).textContent || '',
        url: (e.querySelector('url') || {}).textContent || '',
        text: tmp.textContent.replace(/\s+/g, ' ')
      };
    });
    return index;
  }

  const hl = (s, kws) => kws.reduce((acc, k) => acc.replace(new RegExp(k.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi'), m => `<mark>${m}</mark>`), esc(s));

  // 搜索框里输入 `theme <名字>` 切换配色，选中项实时预览
  function schemeSearch(arg) {
    const kw = arg.toLowerCase();
    const hits = SCHEMES.list.filter(s => s.id.includes(kw) || s.name.toLowerCase().includes(kw));
    active = hits.length ? Math.max(0, hits.findIndex(s => s.id === savedScheme && !kw)) : -1;
    results.innerHTML = hits.length
      ? hits.map((s, i) => `<li class="sr-scheme${i === active ? ' is-active' : ''}" data-scheme-id="${esc(s.id)}"><a href="#"><span class="scheme-swatch" data-scheme="${esc(s.id)}"><i></i><i></i><i></i><i></i></span><span class="sr-title">${esc(s.name)}</span><span class="c-muted">${s.mode}${s.id === savedScheme ? ' · 当前' : ''}</span></a></li>`).join('')
      : `<li class="sr-empty">theme: 没有叫「${esc(arg)}」的配色，输入 theme 查看全部</li>`;
    if (hits.length) applyScheme(hits[active].id);
    else revertScheme();
  }

  async function doSearch() {
    const q = input.value.trim();
    const cmd = q.match(/^theme(?:\s+(.*))?$/i);
    if (cmd) { schemeSearch((cmd[1] || '').trim()); return; }
    revertScheme();
    if (!q) { results.innerHTML = ''; return; }
    const kws = q.toLowerCase().split(/\s+/).filter(Boolean);
    const data = await loadIndex();
    const hits = data.map(p => {
      const t = p.title.toLowerCase();
      const c = p.text.toLowerCase();
      if (!kws.every(k => t.includes(k) || c.includes(k))) return null;
      const score = kws.reduce((s, k) => s + (t.includes(k) ? 10 : 0) + (c.split(k).length - 1), 0);
      const pos = Math.max(0, c.indexOf(kws[0]) - 30);
      const snippet = (pos ? '…' : '') + p.text.slice(pos, pos + 90) + '…';
      return { p, score, snippet };
    }).filter(Boolean).sort((a, b) => b.score - a.score).slice(0, 12);

    active = hits.length ? 0 : -1;
    results.innerHTML = hits.length
      ? hits.map((h, i) => `<li class="${i === 0 ? 'is-active' : ''}"><a href="${esc(h.p.url)}"><span class="sr-title">${hl(h.p.title, kws)}</span><span class="sr-snippet">${hl(h.snippet, kws)}</span></a></li>`).join('')
      : `<li class="sr-empty">grep: 没有找到包含「${esc(q)}」的文章</li>`;
  }

  function openSearch() {
    modal.hidden = false;
    document.body.style.overflow = 'hidden';
    input.focus();
    input.select();
    if (input.value.trim()) doSearch();   // 上次没关掉的 theme 命令要重新预览
    loadIndex();
  }
  function closeSearch() {
    modal.hidden = true;
    document.body.style.overflow = '';
    input.blur();
    revertScheme();
  }
  function moveActive(d) {
    const items = $$('li:not(.sr-empty)', results);
    if (!items.length) return;
    active = (active + d + items.length) % items.length;
    items.forEach((li, i) => li.classList.toggle('is-active', i === active));
    items[active].scrollIntoView({ block: 'nearest' });
    if (items[active].dataset.schemeId) applyScheme(items[active].dataset.schemeId);
  }
  function pickScheme(li) {
    setScheme(li.dataset.schemeId);
    input.value = '';
    results.innerHTML = '';
    closeSearch();
  }

  if (modal) {
    $$('[data-search-open]').forEach(b => b.addEventListener('click', openSearch));
    $$('[data-search-close]').forEach(b => b.addEventListener('click', closeSearch));
    let timer;
    input.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(doSearch, 120); });
    input.addEventListener('keydown', e => {
      if (e.key === 'ArrowDown') { e.preventDefault(); moveActive(1); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); moveActive(-1); }
      else if (e.key === 'Enter') {
        const li = $$('li:not(.sr-empty)', results)[active];
        if (!li) return;
        if (li.dataset.schemeId) pickScheme(li);
        else location.href = $('a', li).href;
      }
    });
    results.addEventListener('click', e => {
      const li = e.target.closest('li[data-scheme-id]');
      if (li) { e.preventDefault(); pickScheme(li); }
    });
    document.addEventListener('keydown', e => {
      const typing = /input|textarea|select/i.test(document.activeElement.tagName) || document.activeElement.isContentEditable;
      if (e.key === 'Escape' && !modal.hidden) closeSearch();
      else if (!typing && (e.key === '/' || ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k'))) {
        e.preventDefault();
        openSearch();
      }
    });
  }

  /* ---------- Giscus 评论 ---------- */
  function giscusTheme() {
    return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark_dimmed' : 'light';
  }
  const gEl = $('#giscus');
  if (gEl) {
    const load = () => {
      const s = document.createElement('script');
      s.src = 'https://giscus.app/client.js';
      s.async = true;
      s.crossOrigin = 'anonymous';
      const d = gEl.dataset;
      const attrs = {
        'data-repo': d.repo, 'data-repo-id': d.repoId,
        'data-category': d.category, 'data-category-id': d.categoryId,
        'data-mapping': d.mapping, 'data-strict': '0',
        'data-reactions-enabled': '1', 'data-emit-metadata': '0',
        'data-input-position': 'top', 'data-lang': d.lang,
        'data-loading': 'lazy', 'data-theme': giscusTheme()
      };
      Object.keys(attrs).forEach(k => s.setAttribute(k, attrs[k]));
      gEl.appendChild(s);
    };
    if ('IntersectionObserver' in window) {
      const io = new IntersectionObserver(es => { if (es[0].isIntersecting) { io.disconnect(); load(); } }, { rootMargin: '400px' });
      io.observe(gEl);
    } else load();
  }
})();
