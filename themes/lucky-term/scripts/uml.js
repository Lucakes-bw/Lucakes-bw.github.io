/**
 * UML 类图：文章里的 ```uml 代码块（内容是 YAML）在渲染 Markdown 之前换成内联 SVG，
 * 外面包一层 <figure class="diagram uml">，和手画的配图样式一致。
 * 画法和字段说明见 source/js/uml.js（浏览器里的 dsviz 演示也用它）。
 *
 * 题库的题面也会转换（hexo.ltUmlFences，scripts/problems.js 调用）。
 * 前面一行写 <!-- uml: plain --> 时不转换，原样显示代码（介绍语法的时候用）。
 */
'use strict';

const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');

// hexo server 运行期间改了 uml.js 也能生效：文件变了就清掉 require 缓存重新加载
const FILE = path.join(__dirname, '../source/js/uml.js');
let loaded = { mtime: 0, mod: null };
function uml() {
  const mtime = fs.statSync(FILE).mtimeMs;
  if (mtime !== loaded.mtime) {
    delete require.cache[require.resolve(FILE)];
    loaded = { mtime, mod: require(FILE) };
  }
  return loaded.mod;
}

const FENCE = /(<!--\s*uml:\s*plain\s*-->\s*\n)?^([ \t]*)```uml[ \t]*\n([\s\S]*?)\n\2```[ \t]*$/gm;

function renderFences(text, source) {
  if (!text || text.indexOf('```uml') < 0) return text;
  return text.replace(FENCE, (all, plain, indent, body) => {
    if (plain) return indent + '```yaml\n' + body + '\n' + indent + '```';
    let spec;
    try {
      const src = indent ? body.split('\n').map(l => l.startsWith(indent) ? l.slice(indent.length) : l).join('\n') : body;
      spec = yaml.load(src);
    } catch (e) {
      hexo.log.warn(`uml 代码块解析失败（${source}）：${e.message}`);
      return all;
    }
    // 整块写成一行，避免 Markdown 把 SVG 中间的空行当成段落分隔
    return indent + '<figure class="diagram uml">' + uml().svg(spec) + '</figure>';
  });
}

// 题库（scripts/problems.js）的题面也用它
hexo.ltUmlFences = renderFences;

hexo.extend.filter.register('before_post_render', function (data) {
  data.content = renderFences(data.content, data.source);
  return data;
}, 9);
