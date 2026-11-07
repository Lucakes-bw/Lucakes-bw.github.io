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

  /* 界面风格（终端 / iOS）和配色分开存：lt-ui */
  function setUi(ui) {
    if (ui === 'ios') html.setAttribute('data-ui', 'ios'); else html.removeAttribute('data-ui');
    $$('[data-ui-v]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.uiV === ui)));
    try { localStorage.setItem('lt-ui', ui); } catch (e) { /* ignore */ }
    // 终端风格整页用等宽字体；iOS 风格只有代码要用
    if (ui !== 'ios' || $('pre, code, .ide, .dsviz')) window.LT.loadMono();
  }
  const curUi = html.getAttribute('data-ui') === 'ios' ? 'ios' : 'term';
  $$('[data-ui-v]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.uiV === curUi)));
  $$('[data-ui-switch]').forEach(g => g.addEventListener('click', e => {
    const b = e.target.closest('[data-ui-v]');
    if (b) setUi(b.dataset.uiV);
  }));
  if (curUi === 'ios' && $('pre, code, .ide, .dsviz')) window.LT.loadMono();

  const picker = $('[data-scheme-picker]');
  if (picker) {
    const toggle = $('[data-scheme-toggle]', picker);
    const menu = $('[data-scheme-menu]', picker);
    const list = $('.scheme-list', menu);
    const items = $$('.scheme-item', menu);
    const moreBtn = $('[data-scheme-more]', menu);
    let cur = -1;
    // 不常用的配色收在「更多配色」里；方向键只在看得见的几项之间移动
    const visible = () => items.filter(li => !(list.classList.contains('is-collapsed') && li.classList.contains('is-extra')));
    const expand = () => { list.classList.remove('is-collapsed'); if (moreBtn) moreBtn.parentNode.hidden = true; };
    if (moreBtn) moreBtn.addEventListener('click', () => { expand(); list.focus(); });

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
      const i = items.findIndex(li => li.dataset.schemeId === savedScheme);
      if (i >= 0 && items[i].classList.contains('is-extra')) expand();   // 正在用的方案收在里面时直接展开
      highlight(i, false);
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
        const vis = visible();
        const at = vis.indexOf(items[cur]);
        const next = vis[at < 0 ? (d > 0 ? 0 : vis.length - 1) : (at + d + vis.length) % vis.length];
        highlight(items.indexOf(next), true);
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
  // 页面可能是悬停时提前在后台渲染好的（预渲染），也可能开了好几个标签页：
  // 内存里的记录可能比 localStorage 旧，改之前先重新读一遍，免得把别处刚记下的已读覆盖掉
  const reloadRead = () => {
    readSet.clear();
    store.get(READ_KEY, []).forEach(u => readSet.add(u));
    Object.keys(readAt).forEach(k => delete readAt[k]);
    Object.assign(readAt, store.get(READ_AT_KEY, {}));
  };
  const saveRead = () => { store.set(READ_KEY, Array.from(readSet)); store.set(READ_AT_KEY, readAt); };
  const markRead = url => { reloadRead(); readSet.add(url); readAt[url] = Date.now(); saveRead(); };
  const unmarkRead = url => { reloadRead(); readSet.delete(url); delete readAt[url]; saveRead(); };

  function asciiBar(done, total, width) {
    const n = total ? Math.round(done / total * width) : 0;
    return '[' + '#'.repeat(n) + '-'.repeat(width - n) + ']';
  }

  // 跟练记录（js/judge/snippet.js 写入）：{ 页面路径: { 代码哈希: { fill / type / recall: 通过时间 } } }
  const practice = store.get('lt-practice', {});

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
        if (bar) {
          bar.textContent = asciiBar(done, s.posts.length, bar.textContent.length - 2);
          bar.style.setProperty('--pct', s.posts.length ? done / s.posts.length : 0);   // iOS 风格用它画进度条
        }
        const count = $('[data-count]', node);
        if (count) count.textContent = done;
        node.classList.toggle('is-done', s.posts.length > 0 && done === s.posts.length);
        const fill = $('[data-bar-fill]', node);
        if (fill) fill.style.width = (s.posts.length ? done / s.posts.length * 100 : 0) + '%';
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
    // 页面上默认写的是「从这里开始」第一篇；读过文章的人换成下一篇，只换文字不换结构，所以不会跳
    if (resume && readSet.size) {
      const set = (label, title, go, href) => {
        $('[data-resume-label]', resume).textContent = label;
        $('[data-resume-title]', resume).textContent = title;
        $('[data-resume-go]', resume).textContent = go;
        resume.href = href;
      };
      const s = series.find(x => x.posts.some(p => !readSet.has(p.url)));
      if (s) {
        const next = s.posts.find(p => !readSet.has(p.url));
        set('继续阅读', `${s.name} · ${next.title}`, '接着读 →', next.url);
      } else {
        set('全部读完', '✓ 所有专栏都读完了', '去刷题 →', (window.LT.root || '/') + 'problems/');
      }
    }
  }

  const markBox = $('[data-mark-read]');
  function updateMarkBox() {
    if (!markBox) return;
    const read = readSet.has(markBox.dataset.markRead);
    markBox.classList.toggle('is-read', read);
    const btn = $('[data-mark-btn]', markBox);
    btn.textContent = read ? '标记为未读' : '标记为已读';
    btn.classList.toggle('btn-solid', !read);   // 没读完时是主按钮，读完后退成中性按钮
    btn.classList.toggle('btn-ghost', read);
  }

  /* ---------- 专注阅读：只留正文 ---------- */
  const focusBtn = $('[data-focus-toggle]');
  if (focusBtn) {
    const setFocus = on => {
      html.classList.toggle('is-focus', on);
      focusBtn.setAttribute('aria-pressed', String(on));
      focusBtn.textContent = on ? '退出专注' : '专注阅读';
      try { localStorage.setItem('lt-focus', on ? '1' : ''); } catch (e) { /* ignore */ }
    };
    let on = false;
    try { on = localStorage.getItem('lt-focus') === '1'; } catch (e) { /* ignore */ }
    if (on) setFocus(true);
    focusBtn.addEventListener('click', () => setFocus(!html.classList.contains('is-focus')));
    document.addEventListener('keydown', e => {
      const typing = /input|textarea|select/i.test(document.activeElement.tagName) || document.activeElement.isContentEditable;
      if (typing || e.metaKey || e.ctrlKey || e.altKey) return;
      if (e.key === 'f' || e.key === 'F') setFocus(!html.classList.contains('is-focus'));
      else if (e.key === 'Escape' && html.classList.contains('is-focus') && !$('#search-modal:not([hidden])')) setFocus(false);
    });
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
    // 预渲染时读者还没打开这一页：短文章一屏就能看完，不能这时就记成已读
    if (document.prerendering) document.addEventListener('prerenderingchange', onScroll, { once: true });
    else onScroll();
  }

  renderReadState();

  // 预渲染好的页面被打开时，读者可能刚在上一页读完一篇、换了配色：按最新的本机数据刷新一遍。
  // 首页的学习记录（热力图、动态）是一次性画的，数据变了就直接重新加载
  if (document.prerendering) {
    let readSnapshot = null;
    try { readSnapshot = localStorage.getItem(READ_KEY); } catch (e) { /* ignore */ }
    document.addEventListener('prerenderingchange', () => {
      let latest = null;
      try { latest = localStorage.getItem(READ_KEY); } catch (e) { /* ignore */ }
      if (latest !== readSnapshot && $('[data-cal]')) { location.reload(); return; }
      reloadRead();
      renderReadState();
      updateMarkBox();
      let scheme = null, ui = null;
      try { scheme = localStorage.getItem('lt-scheme'); ui = localStorage.getItem('lt-ui'); } catch (e) { /* ignore */ }
      if (scheme && scheme !== savedScheme && schemeById.has(scheme)) { savedScheme = scheme; applyScheme(scheme); }
      if (ui === 'ios' || ui === 'term') {
        if (ui === 'ios') html.setAttribute('data-ui', 'ios'); else html.removeAttribute('data-ui');
        $$('[data-ui-v]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.uiV === ui)));
        if (ui !== 'ios' || $('pre, code, .ide, .dsviz')) window.LT.loadMono();
      }
    }, { once: true });
  }

  /* ---------- 首页：学习记录（热力图、学习动态、成就） ----------
     一次「学习」= 读完一篇文章，或在刷题里通过一道题；数据都只在本机浏览器里 */
  const calEl = $('[data-cal]');
  if (calEl) {
    const DAY = 864e5;
    const pad = n => String(n).padStart(2, '0');
    const dayKey = t => { const d = new Date(t); return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`; };
    const startOfDay = t => { const d = new Date(t); d.setHours(0, 0, 0, 0); return d.getTime(); };
    const addDays = (t, n) => { const d = new Date(t); d.setDate(d.getDate() + n); return d.getTime(); };   // 跨夏令时也按日历日走
    const fmtDay = t => { const d = new Date(t); return `${d.getMonth() + 1} 月 ${d.getDate()} 日`; };
    const readJSON = id => { try { return JSON.parse($('#' + id).textContent); } catch (e) { return []; } };

    const posts = readJSON('posts-data');
    const problems = readJSON('problems-data');
    const series = readJSON('series-data');
    const postByUrl = new Map(posts.map(p => [p.url, p]));
    const pbBySlug = new Map(problems.map(p => [p.slug, p]));
    const seriesByName = new Map(series.map(s => [s.name, s]));
    const pbState = store.get('lt-pb-state', {});

    // 带时间的学习事件，新的在前
    const events = [];
    readSet.forEach(url => {
      const post = postByUrl.get(url);
      if (post && readAt[url]) events.push({ type: 'read', at: readAt[url], post });
    });
    Object.keys(pbState).forEach(slug => {
      const st = pbState[slug], problem = pbBySlug.get(slug);
      if (problem && st && st.status === 'ac' && st.at) events.push({ type: 'ac', at: st.at, problem });
    });
    const MODE_NAME = { fill: '填空', type: '照着敲', recall: '默写' };
    Object.keys(practice).forEach(url => {
      const post = postByUrl.get(url);
      if (!post) return;
      Object.values(practice[url] || {}).forEach(r => Object.keys(r || {}).forEach(mode => {
        if (MODE_NAME[mode] && r[mode]) events.push({ type: 'practice', at: r[mode], post, mode });
      }));
    });
    const practiceCount = events.filter(e => e.type === 'practice').length;
    events.sort((a, b) => b.at - a.at);
    const perDay = new Map();
    events.forEach(e => { const k = dayKey(e.at); perDay.set(k, (perDay.get(k) || 0) + 1); });

    // 刷题卡片上的进度
    const solved = Object.keys(pbState).filter(k => pbState[k] && pbState[k].status === 'ac' && pbBySlug.has(k)).length;
    $$('[data-problems]').forEach(card => {
      const total = Number(card.dataset.problems) || 0;
      const n = $('[data-pb-count]', card), fill = $('[data-pb-fill]', card);
      if (n) n.textContent = solved;
      if (fill) fill.style.width = (total ? solved / total * 100 : 0) + '%';
    });

    /* 热力图：和 GitHub 一样一列一周、周日在最上面 */
    const level = n => (n <= 0 ? 0 : n === 1 ? 1 : n <= 3 ? 2 : n <= 5 ? 3 : 4);
    const WEEKDAY = ['', '周一', '', '周三', '', '周五', ''];
    const titleEl = $('[data-contrib-title]');
    const scrollEl = $('[data-cal-scroll]');
    const yearsEl = $('[data-years]');
    const activityEl = $('[data-activity]');
    let range = 'last';

    function bounds(r) {
      const today = startOfDay(Date.now());
      if (r === 'last') return { from: addDays(today, -364), to: today, label: '过去一年' };
      const y = Number(r);
      return { from: new Date(y, 0, 1).getTime(), to: Math.min(new Date(y, 11, 31).getTime(), today), end: new Date(y, 11, 31).getTime(), label: `${y} 年` };
    }

    function renderCalendar() {
      const b = bounds(range);
      const today = startOfDay(Date.now());
      const first = addDays(b.from, -new Date(b.from).getDay());          // 往前退到周日
      const last = b.end || b.to;
      // 星期一列在窄屏横向滚动时固定在左边，每一行（含月份行）都要有一块遮住滚过来的格子
      let html = '<span class="gh-cal-day" style="grid-row:1"></span>' + WEEKDAY.map((w, i) => `<span class="gh-cal-day" style="grid-row:${i + 2}">${w}</span>`).join('');
      let col = 2, total = 0, lastMonth = -1;
      for (let t = first; t <= last; t = addDays(t, 7), col++) {
        // 月份标签：这一列在范围内的第一天换了月就标一次；第一列太靠月底就不标，免得和下一列挤在一起
        const head = Math.max(t, b.from);
        const hd = new Date(head);
        if (hd.getMonth() !== lastMonth) {
          if (col > 2 || hd.getDate() <= 14) html += `<span class="gh-cal-month" style="grid-column:${col} / span 4">${hd.getMonth() + 1} 月</span>`;
          lastMonth = hd.getMonth();
        }
        for (let i = 0; i < 7; i++) {
          const d = addDays(t, i);
          if (d < b.from || d > last) continue;
          if (d > today) { html += `<i class="is-future" style="grid-column:${col};grid-row:${i + 2}"></i>`; continue; }
          const n = d >= b.from && d <= b.to ? perDay.get(dayKey(d)) || 0 : 0;
          total += n;
          html += `<i class="l${level(n)}" style="grid-column:${col};grid-row:${i + 2}" title="${n ? `${n} 次学习` : '没有学习记录'} · ${fmtDay(d)}"></i>`;
        }
      }
      calEl.style.setProperty('--weeks', col - 2);
      calEl.innerHTML = html;
      titleEl.textContent = events.length ? `${b.label}有 ${total} 次学习记录` : '学习记录';
      if (range === 'last') scrollEl.scrollLeft = scrollEl.scrollWidth;
    }

    function renderYears() {
      const years = new Set([new Date().getFullYear()]);
      events.forEach(e => years.add(new Date(e.at).getFullYear()));
      const items = ['last', ...Array.from(years).sort((a, b) => b - a)];
      yearsEl.innerHTML = items.map(y => `<li><button type="button" data-year="${y}" aria-pressed="${y === range || String(y) === range}">${y === 'last' ? '最近一年' : y}</button></li>`).join('');
    }

    /* 学习动态：按月分组，每月里分「读完文章」「通过题目」「开始阅读的专栏」 */
    const firstReadOf = new Map();                                       // 专栏名 → 第一次读完其中一篇的时间
    events.slice().reverse().forEach(e => { if (e.type === 'read' && e.post.series && !firstReadOf.has(e.post.series)) firstReadOf.set(e.post.series, e.at); });
    let monthsShown = 2;

    function seriesLink(name) {
      const s = seriesByName.get(name);
      const dot = `<i class="gh-dot" style="background:${esc((s && s.color) || 'var(--muted)')}"></i>`;
      return s ? `${dot}<a href="${esc(s.url)}">${esc(name)}</a>` : `${dot}<span>${esc(name || '其他文章')}</span>`;
    }

    function renderActivity() {
      const b = bounds(range);
      const list = events.filter(e => e.at >= b.from && e.at < addDays(b.end || b.to, 1));
      if (!list.length) {
        activityEl.innerHTML = `<div class="gh-act-empty">${events.length ? `${esc(b.label)}没有学习记录。` : '还没有学习记录。读完一篇文章（滚到文末或点「标记为已读」），在刷题里通过一道题，或者跟练通过一段代码，这里就会出现第一格。'}</div>`;
        return;
      }
      const months = new Map();
      list.forEach(e => {
        const d = new Date(e.at), k = `${d.getFullYear()} 年 ${d.getMonth() + 1} 月`;
        if (!months.has(k)) months.set(k, []);
        months.get(k).push(e);
      });
      const keys = Array.from(months.keys());
      let html = '';
      keys.slice(0, monthsShown).forEach(k => {
        const evs = months.get(k);
        const reads = evs.filter(e => e.type === 'read');
        const acs = evs.filter(e => e.type === 'ac');
        const pcs = evs.filter(e => e.type === 'practice');
        html += `<div class="gh-act-month"><h3><span>${k}</span></h3>`;
        if (reads.length) {
          const bySeries = new Map();
          reads.forEach(e => bySeries.set(e.post.series, (bySeries.get(e.post.series) || 0) + 1));
          const rows = Array.from(bySeries).sort((a, b) => b[1] - a[1]);
          const max = rows[0][1];
          html += `<div class="gh-act-item"><span class="gh-act-icon">${ICON_READ}</span><div class="gh-act-body">
            <p class="gh-act-head">读完 ${reads.length} 篇文章，涉及 ${rows.length} 个专栏</p>
            <ul class="gh-act-rows">${rows.map(([name, n]) => `<li><span class="gh-act-name">${seriesLink(name)}</span><span class="gh-act-n">${n} 篇</span><span class="gh-act-bar"><i style="width:${Math.max(4, n / max * 100)}%"></i></span></li>`).join('')}</ul>
          </div></div>`;
        }
        if (acs.length) {
          html += `<div class="gh-act-item"><span class="gh-act-icon">${ICON_AC}</span><div class="gh-act-body">
            <p class="gh-act-head">在刷题里通过 ${acs.length} 道题</p>
            <ul class="gh-act-list">${acs.slice(0, 6).map(e => `<li><a href="${esc(e.problem.url)}">${esc(e.problem.id ? e.problem.id + '. ' : '')}${esc(e.problem.title)}</a><span class="gh-date">${fmtDay(e.at)}</span></li>`).join('')}</ul>
          </div></div>`;
        }
        if (pcs.length) {
          const byPost = new Map();
          pcs.forEach(e => { const x = byPost.get(e.post.url) || { post: e.post, at: 0 }; x.at = Math.max(x.at, e.at); byPost.set(e.post.url, x); });
          const rows = Array.from(byPost.values()).sort((a, b) => b.at - a.at);
          const by = m => pcs.filter(e => e.mode === m).length;
          html += `<div class="gh-act-item"><span class="gh-act-icon">${ICON_CODE}</span><div class="gh-act-body">
            <p class="gh-act-head">跟练通过 ${pcs.length} 次（${Object.keys(MODE_NAME).filter(by).map(m => `${MODE_NAME[m]} ${by(m)}`).join(' · ')}），涉及 ${rows.length} 篇文章</p>
            <ul class="gh-act-list">${rows.slice(0, 6).map(x => `<li><a href="${esc(x.post.url)}">${esc(x.post.title)}</a><span class="gh-date">${fmtDay(x.at)}</span></li>`).join('')}</ul>
          </div></div>`;
        }
        const started = Array.from(firstReadOf).filter(([, at]) => evs.some(e => e.at === at));
        if (started.length) {
          html += `<div class="gh-act-item"><span class="gh-act-icon">${ICON_REPO}</span><div class="gh-act-body">
            <p class="gh-act-head">开始阅读 ${started.length} 个专栏</p>
            <ul class="gh-act-list">${started.map(([name, at]) => `<li><span class="gh-act-name">${seriesLink(name)}</span><span class="gh-date">${fmtDay(at)}</span></li>`).join('')}</ul>
          </div></div>`;
        }
        html += '</div>';
      });
      if (keys.length > monthsShown) html += `<button type="button" class="btn btn-ghost gh-act-more" data-act-more>显示更多动态</button>`;
      activityEl.innerHTML = html;
    }

    const ICON_READ = '<svg viewBox="0 0 16 16" width="16" height="16"><path d="M2 3.2c2-.8 4-.8 6 .6 2-1.4 4-1.4 6-.6v9.6c-2-.8-4-.8-6 .6-2-1.4-4-1.4-6-.6zM8 3.8v9.6" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linejoin="round"/></svg>';
    const ICON_AC = '<svg viewBox="0 0 16 16" width="16" height="16"><circle cx="8" cy="8" r="6" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M5.3 8.2l1.8 1.8 3.6-3.8" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg>';
    const ICON_CODE = '<svg viewBox="0 0 16 16" width="16" height="16"><path d="M5.5 4.5L2 8l3.5 3.5M10.5 4.5L14 8l-3.5 3.5M9 3l-2 10" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>';
    const ICON_REPO = '<svg viewBox="0 0 16 16" width="16" height="16"><path d="M3 13V3.2C3 2.5 3.6 2 4.3 2H13v9.5H4.4C3.6 11.5 3 12.1 3 13zm0 0c0 .7.6 1.2 1.4 1.2H13" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linejoin="round"/></svg>';

    /* 成就：都由本机记录算出来 */
    function renderBadges() {
      const days = Array.from(perDay.keys()).map(k => new Date(k + 'T00:00:00').getTime()).sort((a, b) => a - b);
      let streak = 0, run = 0;
      days.forEach((t, i) => { run = i && Math.round((t - days[i - 1]) / DAY) === 1 ? run + 1 : 1; streak = Math.max(streak, run); });
      const seriesDone = series.filter(s => s.posts.length && s.posts.every(p => readSet.has(p.url))).length;
      const night = events.some(e => e.type === 'read' && (new Date(e.at).getHours() >= 23 || new Date(e.at).getHours() < 5));
      const BADGES = [
        { text: '1', name: '初来乍到', desc: '读完第一篇文章', ok: readSet.size >= 1 },
        { text: '10', name: '渐入佳境', desc: '读完 10 篇文章', ok: readSet.size >= 10 },
        { text: '7d', name: '七日连学', desc: '连续 7 天都有学习记录', ok: streak >= 7 },
        { text: '★', name: '专栏毕业', desc: '读完一个专栏的全部文章', ok: seriesDone >= 1 },
        { text: '☾', name: '夜猫子', desc: '在 23 点到 5 点之间读完一篇文章', ok: night },
        { text: 'AC', name: '首次通过', desc: '在刷题里通过第一道题', ok: solved >= 1 },
        { text: '✎', name: '动手派', desc: '在文章里跟练通过 10 次（填空、照着敲、默写都算）', ok: practiceCount >= 10 }
      ];
      const got = BADGES.filter(x => x.ok).length;
      $('[data-badges]').innerHTML = BADGES.map((x, i) =>
        `<span class="gh-badge b${i}${x.ok ? '' : ' is-locked'}" title="${esc(x.name)}：${esc(x.desc)}${x.ok ? '' : '（未解锁）'}">${esc(x.text)}</span>`).join('');
      $('[data-badges-hint]').textContent = got ? `已解锁 ${got} / ${BADGES.length}` : '读完第一篇文章，解锁第一个成就';
      $('[data-badges]').classList.toggle('is-empty', !got);         // 一个都没解锁时缩小显示
    }

    function renderAll() { renderYears(); renderCalendar(); renderActivity(); }

    yearsEl.addEventListener('click', e => {
      const btn = e.target.closest('[data-year]');
      if (!btn) return;
      range = btn.dataset.year;
      monthsShown = 2;
      renderAll();
    });
    activityEl.addEventListener('click', e => {
      if (!e.target.closest('[data-act-more]')) return;
      monthsShown += 3;
      renderActivity();
    });

    // 新访客没有任何记录：热力图和年份收起来，只留一行提示，不占一大片空格子
    calEl.closest('[data-contrib]').classList.toggle('is-empty', !events.length);
    renderBadges();
    renderAll();
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
  const fabText = $('[data-toc-fab-text]');                    // 窄屏目录按钮上显示当前小节
  if (tocLinks.length) {
    const map = new Map();
    tocLinks.forEach(a => {
      const id = decodeURIComponent(a.getAttribute('href').slice(1));
      const h = document.getElementById(id);
      if (h) map.set(h, a);
    });
    // 当前小节 = 最后一个已经滚过顶部附近的标题。直接按位置算，快速滑过一大段也不会漏掉
    const heads = Array.from(map.keys());
    let ticking = false;
    const update = () => {
      ticking = false;
      let current = null;
      for (const h of heads) { if (h.getBoundingClientRect().top < 120) current = h; else break; }
      tocLinks.forEach(a => a.classList.remove('is-active'));
      if (current) map.get(current).classList.add('is-active');
      if (fabText) fabText.textContent = current ? map.get(current).textContent.trim() : '目录';
    };
    window.addEventListener('scroll', () => { if (!ticking) { ticking = true; requestAnimationFrame(update); } }, { passive: true });
    update();
  }

  /* ---------- 窄屏目录：右下角按钮弹出，按钮上显示当前小节 ---------- */
  const fab = $('[data-toc-fab]');
  const toc = $('#toc');
  const tocBackdrop = $('[data-toc-backdrop]');
  if (fab && toc) {
    const setSheet = open => {
      toc.classList.toggle('is-open', open);
      tocBackdrop.hidden = !open;
      fab.setAttribute('aria-expanded', String(open));
      document.body.style.overflow = open ? 'hidden' : '';
      if (open) {
        const cur = $('a.is-active', toc);
        if (cur) cur.scrollIntoView({ block: 'center' });
      }
    };
    fab.addEventListener('click', () => setSheet(!toc.classList.contains('is-open')));
    tocBackdrop.addEventListener('click', () => setSheet(false));
    toc.addEventListener('click', e => { if (e.target.closest('a')) setSheet(false); });
    document.addEventListener('keydown', e => { if (e.key === 'Escape' && toc.classList.contains('is-open')) setSheet(false); });
    matchMedia('(min-width: 1001px)').addEventListener('change', e => { if (e.matches) setSheet(false); });
  }

  /* ---------- 手机顶栏：往下读时收起，往上滑时出现 ---------- */
  const narrow = matchMedia('(max-width: 760px)');
  const headerEl = $('.site-header');
  const isPostPage = !!$('#post-content');
  let lastY = window.scrollY;
  window.addEventListener('scroll', () => {
    const y = window.scrollY;
    // 只在文章页收起：首页、列表页要经常点导航，收起来反而碍事
    if (!narrow.matches || !headerEl || !isPostPage) { html.classList.remove('is-header-hidden'); lastY = y; return; }
    if (Math.abs(y - lastY) < 8) return;                         // 忽略手指的细小抖动
    const menuOpen = !$('[data-scheme-menu]').hidden;
    html.classList.toggle('is-header-hidden', y > lastY && y > headerEl.offsetHeight && !menuOpen);
    lastY = y;
  }, { passive: true });

  /* ---------- 搜索 ---------- */
  const modal = $('#search-modal');
  const input = $('#search-input');
  const results = $('#search-results');
  let index = null;
  let active = -1;

  async function loadIndex() {
    if (index) return index;
    // search.json 由主题的 scripts/search.js 生成：已经是纯文本，不含代码块
    const data = await fetch(window.LT.search).then(r => r.json());
    index = data.map(p => ({ title: p.t, url: p.u, text: (p.g ? p.g + ' ' : '') + p.c }));
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

  /* ---------- 切页加速：预取 + Service Worker ----------
     Chrome / Edge 由 layout.ejs 里的 speculationrules 在悬停时预渲染；
     其他浏览器在这里悬停 65ms（手机上按下）就先把页面取回来。 */
  const conn = navigator.connection;
  const saveData = conn && (conn.saveData || /2g/.test(conn.effectiveType || ''));
  const prefetched = new Set([location.pathname]);
  const NO_PREFETCH = /\/(search\.json|downloads\/|media\/|img\/)|\.xml$/;
  function prefetchable(a) {
    if (!a || !a.href || a.target === '_blank' || a.hasAttribute('download') || a.hasAttribute('data-no-prefetch')) return null;
    let u;
    try { u = new URL(a.href, location.href); } catch (e) { return null; }
    if (u.origin !== location.origin || u.search || NO_PREFETCH.test(u.pathname) || prefetched.has(u.pathname)) return null;
    return u.pathname;
  }
  function prefetch(path) {
    if (saveData || prefetched.has(path)) return;
    prefetched.add(path);
    // 有 Service Worker 时用 fetch：取回来的页面进它的缓存，Safari 也能用上
    if (navigator.serviceWorker && navigator.serviceWorker.controller) {
      fetch(path, { credentials: 'same-origin', priority: 'low' }).catch(() => prefetched.delete(path));
    } else {
      const l = document.createElement('link');
      l.rel = 'prefetch';
      l.href = path;
      document.head.appendChild(l);
    }
  }
  const hasSpecRules = HTMLScriptElement.supports && HTMLScriptElement.supports('speculationrules');
  if (!hasSpecRules && !saveData) {
    let timer = 0;
    document.addEventListener('mouseover', e => {
      const path = prefetchable(e.target.closest && e.target.closest('a'));
      if (!path) return;
      clearTimeout(timer);
      timer = setTimeout(() => prefetch(path), 65);
    }, { passive: true });
    document.addEventListener('mouseout', () => clearTimeout(timer), { passive: true });
    document.addEventListener('touchstart', e => {
      const path = prefetchable(e.target.closest && e.target.closest('a'));
      if (path) prefetch(path);
    }, { passive: true });
  }
  // 文章页：读者多半会点「下一篇」，空闲时先取回来
  const nextLink = prefetchable($('.series-nav .sn-next'));
  if (nextLink && !document.prerendering) {
    const idle = window.requestIdleCallback || (fn => setTimeout(fn, 1500));
    window.addEventListener('load', () => idle(() => prefetch(nextLink)));
  }

  /* ---------- 术语卡片（scripts/glossary.js 给正文里的术语包上 .gl，解释在 #glossary-data） ----------
     鼠标悬停 / 键盘聚焦时显示，移到卡片上不消失（好点「详细讲解」）；触屏上点一下显示，点别处收起 */
  const glData = $('#glossary-data');
  if (glData) {
    let terms = {};
    try { terms = JSON.parse(glData.textContent); } catch (e) { /* ignore */ }
    let card = null, current = null, hideTimer = 0;
    const ensureCard = () => {
      if (card) return card;
      card = document.createElement('div');
      card.className = 'gl-card';
      card.setAttribute('role', 'tooltip');
      card.id = 'gl-card';
      card.hidden = true;
      card.addEventListener('mouseenter', () => clearTimeout(hideTimer));
      card.addEventListener('mouseleave', () => hideSoon());
      document.body.appendChild(card);
      return card;
    };
    const place = el => {
      const r = el.getBoundingClientRect(), c = card.getBoundingClientRect();
      const gap = 8, vw = document.documentElement.clientWidth;
      let left = Math.min(Math.max(16, r.left + r.width / 2 - c.width / 2), vw - c.width - 16);
      let top = r.bottom + gap;
      const below = top + c.height <= window.innerHeight - 8 || r.top - gap - c.height < 8;
      if (!below) top = r.top - gap - c.height;
      card.classList.toggle('is-above', !below);
      card.style.left = left + scrollX + 'px';
      card.style.top = top + scrollY + 'px';
      card.style.setProperty('--arrow', Math.min(Math.max(14, r.left + r.width / 2 - left), c.width - 14) + 'px');
    };
    const show = el => {
      const t = terms[el.dataset.gl];
      if (!t) return;
      clearTimeout(hideTimer);
      ensureCard();
      if (current !== el) {
        if (current) current.removeAttribute('aria-describedby');
        current = el;
        card.innerHTML = `<div class="gl-head"><b>${esc(t.term)}</b>${t.en ? `<span class="gl-en">${esc(t.en)}</span>` : ''}</div>` +
          `<p class="gl-desc">${esc(t.desc)}</p>` +
          (t.url ? `<a class="gl-more" href="${esc(t.url)}">详细讲解：${esc(t.title)} →</a>` : '');
      }
      el.setAttribute('aria-describedby', 'gl-card');
      card.hidden = false;
      place(el);
    };
    const hide = () => {
      clearTimeout(hideTimer);
      if (card) card.hidden = true;
      if (current) current.removeAttribute('aria-describedby');
      current = null;
    };
    const hideSoon = () => { clearTimeout(hideTimer); hideTimer = setTimeout(hide, 180); };
    const glOf = e => e.target.closest && e.target.closest('.gl[data-gl]');
    const canHover = matchMedia('(hover: hover)').matches;
    document.addEventListener('mouseover', e => { const el = glOf(e); if (el && canHover) show(el); });
    document.addEventListener('mouseout', e => { const el = glOf(e); if (el && canHover && !el.contains(e.relatedTarget)) hideSoon(); });
    document.addEventListener('focusin', e => { const el = glOf(e); if (el) show(el); });
    document.addEventListener('focusout', e => { if (glOf(e) && !(card && card.contains(e.relatedTarget))) hideSoon(); });
    document.addEventListener('click', e => {
      const el = glOf(e);
      if (el) { if (current === el && !card.hidden && !canHover) hide(); else show(el); return; }
      if (card && !card.hidden && !card.contains(e.target)) hide();
    });
    document.addEventListener('keydown', e => { if (e.key === 'Escape' && card && !card.hidden) { const el = current; hide(); if (el) el.focus(); } });
    window.addEventListener('resize', () => { if (current && card && !card.hidden) place(current); }, { passive: true });
  }

  /* ---------- 学习记录备份（导出 / 导入，逻辑在 js/backup.js，第一次打开时才加载） ---------- */
  const backupDialog = $('#backup-dialog');
  if (backupDialog && backupDialog.showModal) {
    let loading = null;
    const loadBackup = () => loading || (loading = new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = backupDialog.dataset.src;
      s.onload = () => { window.LTBackup.init(backupDialog); resolve(); };
      s.onerror = () => { loading = null; reject(); };
      document.head.appendChild(s);
    }));
    document.addEventListener('click', e => {
      const link = e.target.closest && e.target.closest('[data-backup-open]');
      if (!link) return;
      e.preventDefault();
      // 先打开对话框，脚本到了再填上本机统计（统计那一栏先显示「…」）
      if (!backupDialog.open) backupDialog.showModal();
      loadBackup().then(() => backupDialog.dispatchEvent(new Event('lt-backup-open')), () => {
        const m = $('[data-bk-msg]', backupDialog);
        m.hidden = false; m.dataset.kind = 'err'; m.textContent = '加载失败，请检查网络后重试';
      });
    });
    backupDialog.addEventListener('click', e => { if (e.target === backupDialog) backupDialog.close(); });   // 点遮罩关闭
    backupDialog.addEventListener('close', () => { if (backupDialog.dataset.changed) location.reload(); });
  } else {
    $$('[data-backup-open]').forEach(a => { a.hidden = true; });   // 不支持 <dialog> 的老浏览器
  }

  // 本地 hexo server 下默认不注册，免得改了文件还看到缓存的旧页面；
  // 要在本地调试 SW 时，在控制台执行 localStorage.setItem('lt-sw-dev', '1')
  const isLocal = /^(localhost|127\.0\.0\.1|\[::1\])$/.test(location.hostname);
  let swDev = false;
  try { swDev = localStorage.getItem('lt-sw-dev') === '1'; } catch (e) { /* ignore */ }
  if ('serviceWorker' in navigator && (!isLocal || swDev)) {
    window.addEventListener('load', () => {
      navigator.serviceWorker.register(window.LT.root + 'sw.js').catch(() => { /* 不支持或被禁用时照常用网络 */ });
    });
  }
})();
