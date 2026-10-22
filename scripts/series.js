/**
 * 专栏目录标签：{% series <key> %}
 * 根据 source/_data/series.yml 生成按章节分组、按 order 排序的文章目录
 */
'use strict';

hexo.extend.tag.register('series', function (args) {
  const key = args[0];
  const data = hexo.locals.get('data').series || {};
  const conf = data[key];
  if (!conf) return `<p>未找到专栏配置：${key}</p>`;

  const urlFor = hexo.extend.helper.get('url_for').bind(hexo);
  const posts = hexo.locals.get('posts').filter(post => {
    const names = post.categories.map(c => c.name);
    return names.includes(conf.category);
  });

  // 按第二级分类（章节）分组
  const groups = {};
  posts.forEach(post => {
    const cats = post.categories.toArray();
    const root = cats.find(c => c.name === conf.category && !c.parent);
    const child = root && cats.find(c => c.parent === root._id);
    const chapter = child ? child.name : '其他';
    (groups[chapter] = groups[chapter] || []).push(post);
  });

  const chapters = [...(conf.chapters || [])];
  Object.keys(groups).forEach(name => {
    if (!chapters.includes(name)) chapters.push(name);
  });

  const byOrder = (a, b) => (a.order ?? 9999) - (b.order ?? 9999) || a.date - b.date;

  let total = 0;
  const html = chapters.map((name, i) => {
    const list = (groups[name] || []).sort(byOrder);
    total += list.length;
    const items = list.length
      ? list.map((p, j) => `<li><span class="series-no">${i + 1}.${j + 1}</span><a href="${urlFor(p.path)}">${p.title}</a><span class="series-date">${p.date.format('YYYY-MM-DD')}</span></li>`).join('')
      : '<li class="series-empty">敬请期待…</li>';
    return `<div class="series-chapter"><h3 class="series-chapter-title">第 ${i + 1} 章 · ${name}<span class="series-count">${list.length} 篇</span></h3><ul class="series-list">${items}</ul></div>`;
  }).join('');

  return `<div class="series"><p class="series-desc">${conf.desc || ''}<br>共 ${chapters.length} 章，已更新 ${total} 篇。</p>${html}</div>`;
});
