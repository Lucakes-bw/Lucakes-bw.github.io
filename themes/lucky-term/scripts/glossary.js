/**
 * 术语卡片：source/_data/glossary.yml 里的术语在文章正文中第一次出现时，包成
 *   <span class="gl" data-gl="序号" tabindex="0">术语</span>
 * 前端（main.js 的「术语卡片」）在悬停、聚焦或点击时显示解释和「详细讲解」的链接。
 *
 *   只处理 _posts 下的文章；front-matter 写 glossary: false 可以整篇关掉
 *   每个术语每篇只标第一次出现；在它自己的「详细讲解」那篇文章里不标
 *   代码（pre / code）、链接、标题、图表、演示组件里的文字不标
 *   英文术语要求前后不紧挨字母数字下划线：DTS 不会匹配 ADTS，IDR 不会匹配 CIDR
 *
 * helper  glossary_json(content)   这篇文章用到的术语，给 post.ejs 内嵌成 JSON
 */
'use strict';

const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');

const SKIP = new Set(['pre', 'code', 'a', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'script', 'style', 'svg', 'figure', 'kbd', 'textarea', 'button', 'abbr', 'sup']);
const VOID = new Set(['br', 'hr', 'img', 'input', 'meta', 'link', 'wbr', 'source', 'col', 'area', 'embed', 'track', 'param', 'base']);
// 演示组件、拆包组件等由 JS 画出来的块：class 里带这些名字的 div 整块跳过
const SKIP_CLASS = /\b(dsviz|pktview|ide|snip|mermaid)\b/;

let cache = { mtime: 0, list: [] };
function glossary() {
  const file = path.join(hexo.source_dir, '_data', 'glossary.yml');
  let st;
  try { st = fs.statSync(file); } catch (e) { return []; }
  if (st.mtimeMs !== cache.mtime) {
    let raw = [];
    try { raw = yaml.load(fs.readFileSync(file, 'utf8')) || []; } catch (e) { hexo.log.warn('glossary.yml 解析失败：' + e.message); }
    const list = raw.filter(g => g && g.term && g.desc && g.post).map((g, i) => {
      const forms = [g.term].concat(g.alias || []).map(String).sort((a, b) => b.length - a.length);   // 长的写法优先
      return { id: i, term: String(g.term), en: g.en || '', desc: String(g.desc), post: String(g.post), forms };
    });
    cache = { mtime: st.mtimeMs, list };
  }
  return cache.list;
}

const ascii = /[A-Za-z0-9_]/;
const isWordChar = ch => !!ch && ascii.test(ch);

// 在一段文本里找某个写法第一次出现的位置（英文写法检查左右边界）
function findForm(text, form) {
  const needBound = ascii.test(form[0]) || ascii.test(form[form.length - 1]);
  let from = 0;
  for (;;) {
    const i = text.indexOf(form, from);
    if (i < 0) return -1;
    if (!needBound || (!(ascii.test(form[0]) && isWordChar(text[i - 1])) && !(ascii.test(form[form.length - 1]) && isWordChar(text[i + form.length])))) return i;
    from = i + 1;
  }
}

// 把 HTML 切成「标签」和「文字」两种片段，文字片段标记是否在要跳过的元素里
function segments(html) {
  const out = [];
  const re = /<!--[\s\S]*?-->|<[^>]+>/g;
  const stack = [];   // 打开的跳过元素
  let last = 0, m;
  const skipping = () => stack.length > 0;
  while ((m = re.exec(html))) {
    if (m.index > last) out.push({ text: html.slice(last, m.index), skip: skipping() });
    const tag = m[0];
    out.push({ tag });
    const t = /^<(\/?)([a-zA-Z][a-zA-Z0-9-]*)([^>]*)>$/.exec(tag);
    if (t) {
      const name = t[2].toLowerCase();
      const closing = !!t[1], selfClosing = /\/\s*$/.test(t[3]) || VOID.has(name);
      if (!closing && !selfClosing) {
        const cls = /\bclass\s*=\s*"([^"]*)"/.exec(t[3]);
        if (SKIP.has(name) || (cls && SKIP_CLASS.test(cls[1]))) stack.push(name);
        else if (skipping()) stack.push(name);          // 跳过区域里的子元素也压栈，好和关闭标签对上
      } else if (closing && skipping()) {
        const i = stack.lastIndexOf(name);
        if (i >= 0) stack.length = i;
      }
    }
    last = re.lastIndex;
  }
  if (last < html.length) out.push({ text: html.slice(last), skip: skipping() });
  return out;
}

function annotate(html, ownSlug) {
  const list = glossary().filter(g => g.post !== ownSlug);
  if (!list.length) return html;
  const segs = segments(html);
  const marks = [];   // 已经包起来的原文，文字里先用占位符代替，免得后面的术语匹配到里面
  list.forEach(g => {
    for (const s of segs) {
      if (s.tag !== undefined || s.skip) continue;
      let best = -1, form = '';
      g.forms.forEach(f => { const i = findForm(s.text, f); if (i >= 0 && (best < 0 || i < best)) { best = i; form = f; } });
      if (best < 0) continue;
      marks.push({ id: g.id, text: form });
      s.text = s.text.slice(0, best) + `\u0000${marks.length - 1}\u0001` + s.text.slice(best + form.length);
      return;
    }
  });
  if (!marks.length) return html;
  return segs.map(s => (s.tag !== undefined ? s.tag : s.text.replace(/\u0000(\d+)\u0001/g, (_, i) => {
    const mk = marks[i];
    return `<span class="gl" data-gl="${mk.id}" tabindex="0">${mk.text}</span>`;
  }))).join('');
}

hexo.extend.filter.register('after_post_render', function (data) {
  if (!data.content || !data.source || !data.source.startsWith('_posts/') || data.glossary === false) return data;
  data.content = annotate(data.content, path.basename(data.source, path.extname(data.source)));
  return data;
});

hexo.extend.helper.register('glossary_json', function (content) {
  const ids = new Set();
  String(content || '').replace(/<span class="gl" data-gl="(\d+)"/g, (_, i) => ids.add(+i));
  if (!ids.size) return '';
  const list = glossary();
  const posts = new Map(hexo.locals.get('posts').map(p => [path.basename(p.source, path.extname(p.source)), p]));
  const out = {};
  ids.forEach(i => {
    const g = list[i];
    if (!g) return;
    const p = posts.get(g.post);
    out[i] = { term: g.term, en: g.en, desc: g.desc, url: p ? this.url_for(p.path) : '', title: p ? p.title : '' };
  });
  return JSON.stringify(out).replace(/</g, '\\u003c');
});

module.exports = { annotate, segments };
