/**
 * 专栏数据：根据 source/_data/series.yml 把文章按「专栏 → 章节 → order」组织起来
 *
 * helper  series_all()          所有专栏（按 series.yml 中的顺序）
 * helper  series_of(post)       文章所属专栏，以及在专栏中的上一篇 / 下一篇
 * tag     {% series key %}      在任意页面中输出某个专栏的目录
 */
'use strict';

function buildSeries(hexo, key, conf) {
  const posts = hexo.locals.get('posts').toArray();
  const groups = {};

  posts.forEach(post => {
    const cats = post.categories.toArray();
    const root = cats.find(c => c.name === conf.category && !c.parent);
    if (!root) return;
    const child = cats.find(c => c.parent === root._id);
    const chapter = child ? child.name : '其他';
    (groups[chapter] = groups[chapter] || []).push(post);
  });

  const names = [...(conf.chapters || [])];
  Object.keys(groups).forEach(n => { if (!names.includes(n)) names.push(n); });

  const byOrder = (a, b) => (a.order ?? 9999) - (b.order ?? 9999) || a.date - b.date;
  const chapters = names.map((name, i) => ({
    no: i + 1,
    name,
    posts: (groups[name] || []).sort(byOrder).map((post, j) => ({ no: `${i + 1}.${j + 1}`, post }))
  }));

  return {
    key,
    name: conf.category,
    desc: conf.desc || '',
    lang: conf.lang || '',
    color: conf.color || '',
    path: `column/${key}/`,
    chapters,
    items: chapters.flatMap(c => c.posts.map(p => ({ ...p, chapter: c.name })))
  };
}

function allSeries(hexo) {
  const data = hexo.locals.get('data').series || {};
  return Object.keys(data).map((key, i) => ({ index: i + 1, ...buildSeries(hexo, key, data[key]) }));
}

hexo.extend.helper.register('series_all', function () {
  return allSeries(hexo);
});

hexo.extend.helper.register('series_of', function (post) {
  for (const s of allSeries(hexo)) {
    const idx = s.items.findIndex(it => it.post._id === post._id);
    if (idx !== -1) {
      return { series: s, item: s.items[idx], prev: s.items[idx - 1], next: s.items[idx + 1] };
    }
  }
  return null;
});

// 首页 / 专栏页给前端 JS 用的精简数据：计算阅读进度和「继续阅读」
hexo.extend.helper.register('series_json', function () {
  const urlFor = hexo.extend.helper.get('url_for').bind(hexo);
  return JSON.stringify(allSeries(hexo).map(s => ({
    key: s.key,
    name: s.name,
    url: urlFor(s.path),
    color: s.color,
    posts: s.items.map(it => ({ title: it.post.title, url: urlFor(it.post.path) }))
  }))).replace(/</g, '\\u003c');
});

// 首页「我的阅读」统计用：每篇文章的标题、专栏和字数（与 hexo-wordcount 的算法一致）
hexo.extend.helper.register('posts_json', function () {
  const { stripHTML } = require('hexo-util');
  const urlFor = hexo.extend.helper.get('url_for').bind(hexo);
  const count = html => {
    const text = stripHTML(html);
    const cn = (text.match(/[一-龥]/g) || []).length;
    const en = (text.replace(/[一-龥]/g, '').match(/[a-zA-Z0-9_Β-ωЀ-ӿ]+|[一-鿿㐀-䶿豈-﫿぀-ゟ가-힯Ѐ-ӿ]+|[äÄåÅöÖ]+|\w+/g) || []).length;
    return cn + en;
  };
  return JSON.stringify(hexo.locals.get('posts').map(post => {
    const root = post.categories.toArray().find(c => !c.parent);
    return { url: urlFor(post.path), title: post.title, series: root ? root.name : '', words: count(post.content) };
  })).replace(/</g, '\\u003c');
});

hexo.extend.tag.register('series', function (args) {
  const data = hexo.locals.get('data').series || {};
  const key = args[0];
  if (!data[key]) return `<p>未找到专栏配置：${key}</p>`;
  const urlFor = hexo.extend.helper.get('url_for').bind(hexo);
  const s = buildSeries(hexo, key, data[key]);
  return s.chapters.map(c => `<h3>第 ${c.no} 章 · ${c.name}</h3><ul>${
    c.posts.length
      ? c.posts.map(p => `<li>${p.no} <a href="${urlFor(p.post.path)}">${p.post.title}</a></li>`).join('')
      : '<li>敬请期待…</li>'
  }</ul>`).join('');
});
