/* 题库列表页：显示本机的做题记录，按难度 / 状态 / 标签 / 关键字筛选 */
(function () {
  'use strict';

  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => Array.from(el.querySelectorAll(s));
  let state = {};
  try { state = JSON.parse(localStorage.getItem('lt-pb-state')) || {}; } catch (e) { /* ignore */ }

  const rows = $$('.pb-row');
  const solved = { all: 0, easy: 0, medium: 0, hard: 0 };
  rows.forEach(row => {
    const s = state[row.dataset.slug];
    const mark = $('.pb-state', row);
    if (s && s.status === 'ac') {
      row.classList.add('is-ac');
      mark.textContent = '✓';
      mark.title = '已通过';
      solved.all++;
      solved[row.dataset.diff]++;
    } else if (s) {
      row.classList.add('is-tried');
      mark.textContent = '○';
      mark.title = '尝试过';
    }
  });
  $('[data-solved]').textContent = solved.all;
  ['easy', 'medium', 'hard'].forEach(d => { const el = $(`[data-solved-${d}]`); if (el) el.textContent = solved[d]; });
  const width = 20, done = rows.length ? Math.round(solved.all / rows.length * width) : 0;
  $('[data-bar]').textContent = `[${'#'.repeat(done)}${'-'.repeat(width - done)}]`;

  const filter = { diff: '', state: '', tag: '', q: '' };
  function apply() {
    let shown = 0;
    rows.forEach(row => {
      const s = state[row.dataset.slug];
      const ac = !!(s && s.status === 'ac');
      const ok = (!filter.diff || row.dataset.diff === filter.diff)
        && (!filter.state || (filter.state === 'ac') === ac)
        && (!filter.tag || row.dataset.tags.split(',').includes(filter.tag))
        && (!filter.q || row.dataset.q.includes(filter.q));
      row.hidden = !ok;
      if (ok) shown++;
    });
    $('[data-pb-empty]').hidden = shown > 0;
  }

  $$('[data-filter]').forEach(group => {
    group.addEventListener('click', e => {
      const b = e.target.closest('button[data-v]');
      if (!b) return;
      const key = group.dataset.filter;
      // 标签再点一次取消
      const v = key === 'tag' && filter.tag === b.dataset.v ? '' : b.dataset.v;
      filter[key] = v;
      $$('button[data-v]', group).forEach(x => x.setAttribute('aria-pressed', String(x.dataset.v === v)));
      apply();
    });
  });
  $('[data-filter-q]').addEventListener('input', e => { filter.q = e.target.value.trim().toLowerCase(); apply(); });
})();
