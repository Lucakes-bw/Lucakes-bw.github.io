/**
 * 订阅和收录，主题自带、不需要插件：
 *   /atom.xml     最近 20 篇文章的 Atom 订阅（RSS 阅读器都能读）
 *   /sitemap.xml  所有文章和页面，给搜索引擎
 *   /robots.txt   指向 sitemap
 */
'use strict';

const { stripHTML } = require('hexo-util');

const FEED_LIMIT = 20;
const xml = s => String(s == null ? '' : s)
  .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const summary = html => {
  const text = stripHTML(String(html || '').replace(/<figure class="highlight[\s\S]*?<\/figure>/g, ' ')).replace(/\s+/g, ' ').trim();
  return text.length > 200 ? text.slice(0, 200) + '…' : text;
};

hexo.extend.generator.register('atom_feed', function (locals) {
  const { config } = hexo;
  const full = p => hexo.extend.helper.get('full_url_for').call(hexo, p);
  const posts = locals.posts.sort('-date').limit(FEED_LIMIT).toArray();
  const updated = (posts[0] ? (posts[0].updated || posts[0].date) : new Date()).toISOString();
  const entries = posts.map(post => {
    const url = full(post.path);
    const root = post.categories.toArray().find(c => !c.parent);
    return `  <entry>
    <title>${xml(post.title)}</title>
    <link href="${xml(url)}"/>
    <id>${xml(url)}</id>
    <published>${post.date.toISOString()}</published>
    <updated>${(post.updated || post.date).toISOString()}</updated>
    ${root ? `<category term="${xml(root.name)}"/>` : ''}
    <summary>${xml(summary(post.content))}</summary>
  </entry>`;
  }).join('\n');
  const data = `<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>${xml(config.title)}</title>
  <subtitle>${xml(config.subtitle || config.description || '')}</subtitle>
  <link href="${xml(full('/atom.xml'))}" rel="self"/>
  <link href="${xml(full('/'))}"/>
  <id>${xml(full('/'))}</id>
  <updated>${updated}</updated>
  <author><name>${xml(config.author)}</name></author>
${entries}
</feed>
`;
  return { path: 'atom.xml', data };
});

hexo.extend.generator.register('sitemap', function (locals) {
  const full = p => hexo.extend.helper.get('full_url_for').call(hexo, p);
  const urls = [{ loc: full('/'), lastmod: new Date() }];
  locals.posts.sort('-date').forEach(p => urls.push({ loc: full(p.path), lastmod: p.updated || p.date }));
  locals.pages.forEach(p => {
    if (p.type === '404' || p.sitemap === false) return;
    urls.push({ loc: full(p.path.replace(/index\.html$/, '')), lastmod: p.updated || p.date });
  });
  urls.push({ loc: full('/archives/'), lastmod: new Date() });
  const data = `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
${urls.map(u => `  <url><loc>${xml(u.loc)}</loc><lastmod>${new Date(u.lastmod).toISOString().slice(0, 10)}</lastmod></url>`).join('\n')}
</urlset>
`;
  return { path: 'sitemap.xml', data };
});

hexo.extend.generator.register('robots', function () {
  const full = p => hexo.extend.helper.get('full_url_for').call(hexo, p);
  return { path: 'robots.txt', data: `User-agent: *\nAllow: /\n\nSitemap: ${full('/sitemap.xml')}\n` };
});
