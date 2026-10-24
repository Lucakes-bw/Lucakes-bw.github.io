/**
 * helper  asset_url(path)   主题静态文件的地址，带上按内容算出的版本号 ?v=xxxxxxxx
 *
 * GitHub Pages 让浏览器缓存静态文件约 10 分钟，更新后可能出现「新页面 + 旧脚本」。
 * 文件内容一变版本号就变，浏览器会去取新文件。
 */
'use strict';

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const cache = {};

hexo.extend.helper.register('asset_url', function (p) {
  if (!(p in cache)) {
    try {
      const file = path.join(hexo.theme_dir, 'source', p);
      cache[p] = crypto.createHash('md5').update(fs.readFileSync(file)).digest('hex').slice(0, 8);
    } catch (e) {
      cache[p] = '';
    }
  }
  return this.url_for(p) + (cache[p] ? '?v=' + cache[p] : '');
});

// hexo server 下改了文件要重新算
hexo.on('generateBefore', () => { Object.keys(cache).forEach(k => delete cache[k]); });
