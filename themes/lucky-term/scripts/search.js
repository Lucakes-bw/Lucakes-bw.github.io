/**
 * 站内搜索索引：/search.json
 *   [{ t: 标题, u: 地址, g: 标签（空格分隔）, c: 正文纯文本 }]
 *
 * 只留能读的文字：去掉代码块、示意图、逐步演示和 HTML 标签。行内代码保留，所以
 * 搜 priority_queue 这类名字照样能搜到。比整篇 HTML 小得多，首次搜索下载更快。
 */
'use strict';

const { stripHTML } = require('hexo-util');

const ENTITIES = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ', '#39': "'" };

function plainText(html) {
  const cleaned = String(html || '')
    .replace(/<figure class="highlight[\s\S]*?<\/figure>/g, ' ')   // 代码块
    .replace(/<figure class="diagram[\s\S]*?<\/figure>/g, ' ')     // 示意图
    .replace(/<svg[\s\S]*?<\/svg>/g, ' ')
    .replace(/<(script|style)[\s\S]*?<\/\1>/g, ' ')
    .replace(/<div class="dsviz"[^>]*><\/div>/g, ' ');              // 逐步演示的占位
  return stripHTML(cleaned)
    .replace(/&(#39|[a-z]+);/g, (m, k) => ENTITIES[k] || ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

hexo.extend.generator.register('search_json', function (locals) {
  const urlFor = hexo.extend.helper.get('url_for').bind(hexo);
  const data = locals.posts.sort('-date').map(post => ({
    t: post.title,
    u: urlFor(post.path),
    g: post.tags.map(t => t.name).join(' '),
    c: plainText(post.content)
  }));
  return { path: 'search.json', data: JSON.stringify(data) };
});
