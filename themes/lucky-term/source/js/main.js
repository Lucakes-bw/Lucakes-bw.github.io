/* lucky-term 主题脚本：配色方案、搜索、阅读进度、代码复制、目录高亮、评论 */
(function () {
  'use strict';

  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => Array.from(el.querySelectorAll(s));
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
  const readSet = new Set(store.get(READ_KEY, []));
  const saveRead = () => store.set(READ_KEY, Array.from(readSet));

  function asciiBar(done, total, width) {
    const n = total ? Math.round(done / total * width) : 0;
    return '[' + '#'.repeat(n) + '-'.repeat(width - n) + ']';
  }

  function renderReadState() {
    $$('[data-read-url]').forEach(el => el.classList.toggle('is-read', readSet.has(el.dataset.readUrl)));

    const total = $('[data-read-total]');
    if (total) total.textContent = readSet.size;

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
      readSet.has(url) ? readSet.delete(url) : readSet.add(url);
      saveRead();
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
        readSet.add(markBox.dataset.markRead);
        saveRead();
        updateMarkBox();
      }
    };
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();
  }

  renderReadState();

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

  const esc = s => s.replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
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
