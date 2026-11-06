/**
 * 文章代码块「跟练」（见 source/js/judge/snippet.js）
 *
 * helper  has_c_code(content)   文章里有没有 C/C++ 代码块：有才加载跟练脚本
 * helper  clang_pkg()           浏览器里用的 Clang 包名和版本（和做题页同一个，定义在 lang.js）
 * helper  practice_count(post)  文章里能跟练的代码块数（能运行、不是演示错误的），专栏页显示「练 m/n」
 */
'use strict';

const path = require('path');

const core = () => require(path.join(hexo.theme_dir, 'source/js/judge/snippet-core.js'));

hexo.extend.helper.register('has_c_code', content => /<figure class="highlight (c|cpp)[\s"]/.test(content || ''));

hexo.extend.helper.register('clang_pkg', () => {
  const { CLANG } = require(path.join(hexo.theme_dir, 'source/js/judge/lang.js'));
  return `${CLANG.pkg}@${CLANG.version}`;
});

const counts = new Map();
hexo.on('generateBefore', () => counts.clear());
hexo.extend.helper.register('practice_count', post => {
  const md = post && post._content;
  if (!md) return 0;
  if (!counts.has(md)) {
    const S = core();
    counts.set(md, S.extractBlocks(md).filter(b => {
      const r = S.classify(b.lang, b.code, b.opt);
      return r.runnable && !r.error;
    }).length);
  }
  return counts.get(md);
});
