/**
 * 生成站点根目录下的 /sw.js（Service Worker，必须放在根目录才能管到全站）
 *
 * 模板在主题的 sw/sw.js，把里面的 __VERSION__ 换成「模板 + 主题 source 下所有文件」的哈希：
 * 主题的 CSS / JS / 字体一改，版本就变，浏览器装上新的 SW 后清掉旧缓存。
 */
'use strict';

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

function walk(dir, out) {
  for (const name of fs.readdirSync(dir).sort()) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) walk(full, out);
    else out.push(full);
  }
  return out;
}

hexo.extend.generator.register('lucky_term_sw', function () {
  const tpl = fs.readFileSync(path.join(hexo.theme_dir, 'sw', 'sw.js'), 'utf8');
  const hash = crypto.createHash('md5').update(tpl);
  const src = path.join(hexo.theme_dir, 'source');
  // 用相对路径参与哈希，本机和 CI 上构建出来的版本号一致
  walk(src, []).forEach(f => hash.update(path.relative(src, f)).update(fs.readFileSync(f)));
  const version = hash.digest('hex').slice(0, 8);
  return { path: 'sw.js', data: tpl.replace("const VERSION = '__VERSION__'", `const VERSION = '${version}'`) };
});
