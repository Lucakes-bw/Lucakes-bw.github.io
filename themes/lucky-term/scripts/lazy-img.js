/**
 * 文章图片懒加载：给正文里的 <img> 加上 loading="lazy" decoding="async"，
 * 滚动到附近才下载，切到一篇图多的文章时不再一口气取完所有图片。
 * 第一张图常常在首屏，保持立即加载；已经写了 loading 的图片不动。
 */
'use strict';

hexo.extend.filter.register('after_post_render', function (data) {
  if (!data.content || !data.content.includes('<img')) return data;
  let n = 0;
  data.content = data.content.replace(/<img\b([^>]*)>/gi, (tag, attrs) => {
    n++;
    if (n === 1 || /\sloading\s*=/i.test(attrs)) return tag;
    const decoding = /\sdecoding\s*=/i.test(attrs) ? '' : ' decoding="async"';
    return `<img loading="lazy"${decoding}${attrs}>`;
  });
  return data;
});
