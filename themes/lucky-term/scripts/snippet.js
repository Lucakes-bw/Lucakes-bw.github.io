/**
 * 文章代码块「跟练」（见 source/js/judge/snippet.js）
 *
 * helper  has_c_code(content)   文章里有没有 C/C++ 代码块：有才加载跟练脚本
 * helper  clang_pkg()           浏览器里用的 Clang 包名和版本（和做题页同一个，定义在 lang.js）
 */
'use strict';

const path = require('path');

hexo.extend.helper.register('has_c_code', content => /<figure class="highlight (c|cpp)[\s"]/.test(content || ''));

hexo.extend.helper.register('clang_pkg', () => {
  const { CLANG } = require(path.join(hexo.theme_dir, 'source/js/judge/lang.js'));
  return `${CLANG.pkg}@${CLANG.version}`;
});
