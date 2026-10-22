/**
 * 首页分类栏只显示一级分类（专栏），不显示章节
 * 在 before_generate 中注册，覆盖主题自带的 catalog_list
 */
'use strict';

hexo.extend.filter.register('before_generate', () => {
  hexo.extend.helper.register('catalog_list', function (type) {
    const urlFor = hexo.extend.helper.get('url_for').bind(hexo);
    return hexo.locals.get(type)
      .filter(item => !item.parent && item.length > 0)
      .map(item => `<div class="catalog-list-item" id="/${item.path}"><a href="${urlFor(item.path)}">${item.name}</a></div>`)
      .join('');
  });
});
