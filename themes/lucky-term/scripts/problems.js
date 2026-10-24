/**
 * 刷题：读取 source/_problems/*.md，生成题库列表页 /problems/ 和每道题的做题页 /problems/<slug>/
 *
 * 题目文件的 front-matter 见 source/_problems/README.md；正文是题目描述（Markdown）。
 * source/_problems/solutions/ 下是参考解答，只给 tools/check-problems.mjs 用，不会发布。
 */
'use strict';

const path = require('path');
const yfm = require('hexo-front-matter');
const J = require(path.join(__dirname, '../source/js/judge/lang.js'));

const problems = new Map();
const DIFF = { easy: '简单', medium: '中等', hard: '困难' };

hexo.extend.processor.register('_problems/:slug.md', async function (file) {
  const { slug } = file.params;
  if (slug === 'README') return;
  if (file.type === 'delete') { problems.delete(slug); return; }
  // 'skip'（文件没变）也要读：problems 只存在内存里，每次启动都是空的
  const fm = yfm.parse(await file.read());
  let sig;
  try {
    sig = J.signature(fm);
  } catch (e) {
    hexo.log.warn(`题目 ${slug}：${e.message}`);
    return;
  }
  const md = text => hexo.render.render({ text: String(text), engine: 'markdown' });
  const examples = await Promise.all((fm.examples || []).map(async ex => ({
    in: ex.in,
    out: ex.out,
    explain: ex.explain ? (await md(ex.explain)).trim().replace(/^<p>([\s\S]*)<\/p>$/, '$1') : ''
  })));
  problems.set(slug, {
    slug,
    id: fm.id,
    title: fm.title,
    difficulty: fm.difficulty in DIFF ? fm.difficulty : 'easy',
    tags: fm.tags || [],
    related: fm.related || [],
    leetcode: fm.leetcode || '',
    timeLimit: fm.time_limit || null,
    sig,
    examples,
    tests: (fm.tests || []).map(t => ({ in: t.in, out: t.out })),
    content: await md(fm._content || '')
  });
});

hexo.extend.helper.register('difficulty_name', d => DIFF[d] || d);

hexo.extend.generator.register('problems', function (locals) {
  const list = [...problems.values()].sort((a, b) => a.id - b.id);
  list.forEach(p => {
    p.relatedPosts = p.related
      .map(s => locals.posts.findOne({ source: `_posts/${s}.md` }))
      .filter(Boolean)
      .map(post => ({ title: post.title, path: post.path }));
  });
  const routes = [{
    path: 'problems/index.html',
    layout: ['problems'],
    data: { title: '刷题', type: 'problems', problems: list }
  }];
  list.forEach((p, i) => routes.push({
    path: `problems/${p.slug}/index.html`,
    layout: ['problem'],
    data: {
      title: `${p.id}. ${p.title}`,
      description: `${p.title}（${DIFF[p.difficulty]}）：在浏览器里用 C++ / Python / JavaScript 直接写代码、运行和提交。`,
      ide: true,
      problem: p,
      prev: list[i - 1] || null,
      next: list[i + 1] || null
    }
  }));
  return routes;
});

// 做题页给前端的数据：签名、用例（判题在浏览器里做，用例本来就是公开的）
hexo.extend.helper.register('problem_json', function (p) {
  return JSON.stringify({
    slug: p.slug,
    id: p.id,
    title: p.title,
    sig: p.sig,
    timeLimit: p.timeLimit,
    examples: p.examples.map(e => ({ in: e.in, out: e.out })),
    tests: p.tests
  }).replace(/</g, '\\u003c');
});
