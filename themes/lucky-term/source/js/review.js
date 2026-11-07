/**
 * 错题本与间隔复习（复习页 layout/page.ejs 的 type: review，首页「学习动态」旁边的待复习数）
 *
 * 复习的对象：
 *   题目    在刷题里通过的题                      id = pb:<slug>
 *   代码块  在文章里通过过填空 / 照着敲 / 默写的代码  id = snip:<页面路径>#<代码哈希>
 *   错题本  提交过、还没通过的题（lt-pb-state 里 status 为 tried）
 *
 * 数据：lt-review = { id: { p: [每次通过的时间戳…], t: 代码块所在小节的标题 } }
 *   由 js/judge/problem.js、js/judge/snippet.js 在每次通过时追加，这里只读。
 *   这个功能上线前的通过记录没有 lt-review，用 lt-pb-state 的 at 和 lt-practice 里的时间补上。
 *
 * 间隔：第一次通过后 1 天复习，之后依次隔 2、4、7、15、30 天；按日历日算，到了那天就算「该复习」。
 *   一次通过只有在「到期之后」才算完成一轮复习；没到期时再做一遍不改变进度（多练不吃亏，也不会跳级）。
 *   六轮都复习完就算掌握，不再提醒。
 */
(function (global) {
  'use strict';

  const INTERVALS = [1, 2, 4, 7, 15, 30];
  const DAY = 864e5;
  const isObj = v => v && typeof v === 'object' && !Array.isArray(v);
  const startOfDay = t => { const d = new Date(t); d.setHours(0, 0, 0, 0); return d.getTime(); };
  const addDays = (t, n) => { const d = new Date(t); d.setDate(d.getDate() + n); return d.getTime(); };

  // 一组通过时间 → 复习进度 { stage: 完成了几轮, last: 最近一次计入进度的通过, due: 下次该复习的那天 0 点（掌握了为 null） }
  function progress(passes) {
    const ts = passes.filter(t => typeof t === 'number' && t > 0).sort((a, b) => a - b);
    if (!ts.length) return null;
    let stage = 0, last = ts[0];
    const dueOf = () => (stage < INTERVALS.length ? addDays(startOfDay(last), INTERVALS[stage]) : null);
    for (let i = 1; i < ts.length; i++) {
      const due = dueOf();
      if (due !== null && ts[i] >= due) { stage++; last = ts[i]; }
    }
    return { stage, last, due: dueOf(), first: ts[0] };
  }

  /**
   * 汇总所有复习项。src = { review, pbState, practice }（三份 localStorage 数据），
   * posts / problems 是页面里内嵌的文章、题目列表（找标题和地址用）
   */
  function collect(src, posts, problems, now) {
    const review = isObj(src.review) ? src.review : {};
    const pbState = isObj(src.pbState) ? src.pbState : {};
    const practice = isObj(src.practice) ? src.practice : {};
    const postByUrl = new Map((posts || []).map(p => [p.url, p]));
    const pbBySlug = new Map((problems || []).map(p => [p.slug, p]));
    const passesOf = id => (isObj(review[id]) && Array.isArray(review[id].p) ? review[id].p.slice() : []);
    const items = [], wrong = [];

    Object.keys(pbState).forEach(slug => {
      const st = pbState[slug], pb = pbBySlug.get(slug);
      if (!isObj(st) || !pb) return;
      if (st.status !== 'ac') { wrong.push({ kind: 'pb', slug, title: pb.title, url: pb.url, at: st.at || 0, lang: st.lang }); return; }
      const passes = passesOf('pb:' + slug);
      if (st.at) passes.push(st.at);
      const pr = progress(passes);
      if (pr) items.push(Object.assign({ kind: 'pb', id: 'pb:' + slug, title: pb.title, url: pb.url }, pr));
    });

    const seen = new Set();
    const addSnip = (page, hash) => {
      const id = `snip:${page}#${hash}`;
      if (seen.has(id)) return;
      seen.add(id);
      const post = postByUrl.get(page);
      if (!post) return;     // 文章删掉或改了地址
      const passes = passesOf(id);
      const rec = isObj(practice[page]) && isObj(practice[page][hash]) ? practice[page][hash] : {};
      Object.keys(rec).forEach(m => passes.push(rec[m]));
      const pr = progress(passes);
      if (!pr) return;
      const section = isObj(review[id]) && typeof review[id].t === 'string' ? review[id].t : '';
      items.push(Object.assign({ kind: 'snip', id, title: post.title, section, url: `${page}#snip-${hash}` }, pr));
    };
    Object.keys(practice).forEach(page => { if (isObj(practice[page])) Object.keys(practice[page]).forEach(h => addSnip(page, h)); });
    Object.keys(review).forEach(id => { const m = /^snip:(.+)#([^#]+)$/.exec(id); if (m) addSnip(m[1], m[2]); });

    const today = startOfDay(now);
    const due = items.filter(it => it.due !== null && it.due <= today).sort((a, b) => a.due - b.due || a.last - b.last);
    const upcoming = items.filter(it => it.due !== null && it.due > today).sort((a, b) => a.due - b.due);
    const mastered = items.filter(it => it.due === null);
    wrong.sort((a, b) => b.at - a.at);
    return { due, upcoming, mastered, wrong, total: items.length };
  }

  /* ---------- 浏览器部分 ---------- */
  const read = (k, d) => { try { const v = JSON.parse(localStorage.getItem(k)); return v == null ? d : v; } catch (e) { return d; } };
  const readJSON = id => { try { return JSON.parse(document.getElementById(id).textContent); } catch (e) { return []; } };
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  function load(now) {
    return collect({ review: read('lt-review', {}), pbState: read('lt-pb-state', {}), practice: read('lt-practice', {}) },
      readJSON('posts-data'), readJSON('problems-data'), now);
  }

  const daysAgo = (t, now) => {
    const n = Math.round((startOfDay(now) - startOfDay(t)) / DAY);
    return n <= 0 ? '今天' : n === 1 ? '昨天' : `${n} 天前`;
  };
  const daysLater = (t, now) => {
    const n = Math.round((t - startOfDay(now)) / DAY);
    return n === 1 ? '明天' : n === 2 ? '后天' : `${n} 天后`;
  };
  const kindTag = it => `<span class="rv-kind rv-${it.kind}">${it.kind === 'pb' ? '题目' : '代码'}</span>`;
  const itemTitle = it => esc(it.title) + (it.section ? `<span class="c-muted"> · ${esc(it.section)}</span>` : '');
  const action = it => (it.kind === 'pb' ? '重新做一遍，提交通过' : '回到文章，默写或填空通过一次');

  // 复习页
  function renderPage(root, now) {
    const r = load(now);
    const $ = s => root.querySelector(s);
    $('[data-rv-sum]').innerHTML = r.total || r.wrong.length
      ? `今天该复习 <b>${r.due.length}</b> 项 · 错题 <b>${r.wrong.length}</b> 道 · 已掌握 <b>${r.mastered.length}</b> 项 · 共跟踪 ${r.total} 项`
      : '还没有可以复习的内容。在刷题里通过一道题，或在文章的代码块里通过一次填空 / 照着敲 / 默写，第二天就会出现在这里。';

    const dueEl = $('[data-rv-due]');
    dueEl.innerHTML = r.due.length ? r.due.map(it => `<li><a href="${esc(it.url)}">${kindTag(it)}<span class="rv-title">${itemTitle(it)}</span>` +
      `<span class="rv-meta">第 ${it.stage + 1} 轮 · 上次 ${daysAgo(it.last, now)}${it.due < startOfDay(now) ? ` · <span class="rv-late">拖了 ${Math.round((startOfDay(now) - it.due) / DAY)} 天</span>` : ''}</span></a>` +
      `<span class="rv-how c-muted">${action(it)}</span></li>`).join('')
      : `<li class="rv-empty c-muted">${r.total ? '今天没有要复习的，' + (r.upcoming.length ? `下一项在${daysLater(r.upcoming[0].due, now)}。` : '全部掌握了。') : '暂无。'}</li>`;

    const wrongEl = $('[data-rv-wrong]');
    wrongEl.innerHTML = r.wrong.length ? r.wrong.map(it => `<li><a href="${esc(it.url)}">${kindTag(it)}<span class="rv-title">${esc(it.title)}</span>` +
      `<span class="rv-meta">最近一次提交 ${it.at ? daysAgo(it.at, now) : '时间不详'}</span></a></li>`).join('')
      : '<li class="rv-empty c-muted">没有错题：提交过的题都通过了。</li>';

    // 接下来 7 天，每天几项
    const weekEl = $('[data-rv-week]');
    const days = [];
    for (let i = 1; i <= 7; i++) {
      const d = addDays(startOfDay(now), i);
      days.push({ d, n: r.upcoming.filter(it => it.due === d).length });
    }
    const max = Math.max(1, ...days.map(x => x.n));
    weekEl.innerHTML = days.map(x => {
      const dt = new Date(x.d);
      return `<li title="${dt.getMonth() + 1} 月 ${dt.getDate()} 日：${x.n} 项"><span class="rv-bar" style="--h:${x.n / max}"></span><span class="rv-n">${x.n || ''}</span><span class="rv-day c-muted">${'日一二三四五六'[dt.getDay()]}</span></li>`;
    }).join('');
  }

  // 首页：「学习动态」标题旁的待复习数
  function renderBadge(el, now) {
    const r = load(now);
    const n = r.due.length, w = r.wrong.length;
    if (!n && !w) return;
    el.textContent = n ? `今天复习 ${n} 项${w ? ` · 错题 ${w}` : ''} →` : `错题本 ${w} 道 →`;
    el.hidden = false;
  }

  const api = { INTERVALS, progress, collect, renderPage, renderBadge };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else {
    global.LTReview = api;
    const run = () => {
      const now = Date.now();
      const page = document.querySelector('[data-review-page]');
      if (page) renderPage(page, now);
      document.querySelectorAll('[data-review-badge]').forEach(el => renderBadge(el, now));
    };
    if (document.prerendering) document.addEventListener('prerenderingchange', run, { once: true });
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', run); else run();
  }
})(this);
