#!/usr/bin/env node
/**
 * 检查文章里的 C/C++ 代码块（「跟练」功能）：
 *   按 snippet-core.js 的规则找出能在浏览器里运行的代码块，用和网页里同一个编译器编译、运行一遍，
 *   编译失败、运行出错、超时的都会列出来。修改代码，或者在代码块前加 <!-- run: off --> 关掉运行按钮。
 * 能跟练的代码块（不是 run: error）还会检查：
 *   - 「填空」挖不出空的（太短的程序）只是提示，这种代码不提供填空；想要的话用 <!-- run: blank="行号" --> 指定
 *   - 连跑两次输出一样；不一样又没被识别成「输出不固定」的，判定会出错，需要补充 snippet-core.js 里 unstable 的规则
 *
 * notebook 文章（front-matter 写 notebook: true）整篇拼成一个程序检查，语句格的输出要和文中紧跟着的输出块一致。
 *
 * 用法：node tools/check-snippets.mjs [--list] [--verbose] [--notebook | --force-notebook] [文章 slug ...]
 *   --list     只统计哪些代码块能运行、哪些不能（不需要编译器）
 *   --verbose  显示每个代码块的输出
 *   --notebook 只检查 notebook 文章；--force-notebook 把指定的文章都当 notebook 试（还没开 notebook 时先试试）
 * 需要先装编译器（不写进 package.json）：npm i --no-save @yowasp/clang@22.0.0-git20542-10
 */
import { createRequire } from 'node:module';
import { Worker } from 'node:worker_threads';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const require = createRequire(import.meta.url);
const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const J = require(path.join(ROOT, 'themes/lucky-term/source/js/judge/lang.js'));
const S = require(path.join(ROOT, 'themes/lucky-term/source/js/judge/snippet-core.js'));
const DIR = path.join(ROOT, 'source/_posts');
const TIMEOUT = 10000;

const args = process.argv.slice(2);
const LIST = args.includes('--list'), VERBOSE = args.includes('--verbose');
// --notebook：只检查 notebook 文章（front-matter 写了 notebook: true）；--force-notebook 把指定的文章都当 notebook 试一遍
const NB_ONLY = args.includes('--notebook') || args.includes('--force-notebook'), NB_FORCE = args.includes('--force-notebook');
const only = args.filter(a => !a.startsWith('--'));

const posts = fs.readdirSync(DIR).filter(f => f.endsWith('.md')).sort()
  .filter(f => !only.length || only.includes(f.slice(0, -3)));

const all = [];
for (const f of posts) {
  const md = fs.readFileSync(path.join(DIR, f), 'utf8');
  const col = (/^categories:\s*\n\s*-\s*(.+)$/m.exec(md) || [, '?'])[1].trim();
  const nb = /^---\n[\s\S]*?^notebook:\s*true\s*$[\s\S]*?^---/m.test(md);
  for (const b of S.extractBlocks(md)) {
    const lang = S.normLang(b.lang);
    if (!lang) continue;
    const info = S.classify(lang, b.code, b.opt);
    // notebook 文章里参与拼接的格子：统计时算能运行（逐个检查时跳过，由下面的 notebook 检查负责）
    if (nb && !info.runnable && /^(def|main)$/.test(S.cellKind(lang, b.code, b.opt))) Object.assign(info, { runnable: true, notebook: true, reason: undefined });
    all.push(Object.assign(b, { file: f, col, info }));
  }
}

// 统计
const byCol = {};
const reasons = {};
for (const b of all) {
  const c = byCol[b.col] = byCol[b.col] || { total: 0, run: 0 };
  c.total++;
  if (b.info.runnable) c.run++;
  else reasons[b.info.reason.replace(/（.*）|".*"/, '')] = (reasons[b.info.reason.replace(/（.*）|".*"/, '')] || 0) + 1;
}
console.log('\x1b[1m专栏                C/C++ 代码块   可运行\x1b[0m');
for (const [k, v] of Object.entries(byCol)) console.log(`${k.padEnd(16)}  ${String(v.total).padStart(8)}   ${String(v.run).padStart(6)}`);
console.log('\n\x1b[1m不能运行的原因\x1b[0m');
Object.entries(reasons).sort((a, b) => b[1] - a[1]).forEach(([k, v]) => console.log(`  ${String(v).padStart(4)}  ${k}`));
const runnable = all.filter(b => b.info.runnable && !b.info.notebook);
const nbCells = all.filter(b => b.info.notebook).length;
const practicable = runnable.filter(b => !b.info.error).length;
console.log(`\n共 ${all.length} 个 C/C++ 代码块，${runnable.length + nbCells} 个可以在浏览器里运行（其中 ${nbCells} 个在 notebook 文章里），单独运行的 ${practicable} 个可以跟练\n`);
if (LIST) process.exit(0);

let runClang;
try {
  ({ runClang } = await import('@yowasp/clang'));
} catch (e) {
  console.log(`\x1b[31m没有安装编译器：npm i --no-save ${J.CLANG.pkg}@${J.CLANG.version}\x1b[0m`);
  process.exit(1);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lt-snippet-'));
// 运行放到单独的线程里，死循环也能按时结束
const RUNNER = `
const { workerData, parentPort } = require('node:worker_threads');
const { WASI } = require('node:wasi');
const fs = require('node:fs');
const { wasm, inFile, outFile, errFile } = workerData;
const fds = [fs.openSync(inFile, 'r'), fs.openSync(outFile, 'w'), fs.openSync(errFile, 'w')];
const wasi = new WASI({ version: 'preview1', args: ['main'], stdin: fds[0], stdout: fds[1], stderr: fds[2], returnOnExit: true });
let exitCode = 0, error = null;
try {
  const inst = new WebAssembly.Instance(new WebAssembly.Module(wasm), { wasi_snapshot_preview1: wasi.wasiImport });
  exitCode = wasi.start(inst);
} catch (e) { error = String(e && e.message || e); }
fds.forEach(fd => fs.closeSync(fd));
parentPort.postMessage({ exitCode, error });
`;
function runWasm(wasm, stdin) {
  const inFile = path.join(tmp, 'in.txt'), outFile = path.join(tmp, 'out.txt'), errFile = path.join(tmp, 'err.txt');
  fs.writeFileSync(inFile, stdin);
  return new Promise(resolve => {
    const w = new Worker(RUNNER, { eval: true, execArgv: ['--no-warnings'], workerData: { wasm, inFile, outFile, errFile } });
    const read = () => ({ stdout: fs.readFileSync(outFile, 'utf8'), stderr: fs.readFileSync(errFile, 'utf8') });
    const timer = setTimeout(() => { w.terminate(); resolve(Object.assign(read(), { timeout: true })); }, TIMEOUT);
    w.on('message', r => { clearTimeout(timer); resolve(Object.assign(read(), r)); });
    w.on('error', e => { clearTimeout(timer); resolve(Object.assign(read(), { error: String(e) })); });
  });
}

async function compile(lang, code, flags) {
  let stderr = '';
  const dec = new TextDecoder();
  try {
    const out = await runClang(S.compileArgs(lang, flags), S.compileFiles(lang, code),
      { stderr: b => { if (b) stderr += dec.decode(b, { stream: true }); }, stdout: () => {} });
    return { wasm: out && out['main.wasm'], stderr };
  } catch (e) {
    return { error: stderr || String(e) };
  }
}

const indent = s => s.replace(/\n$/, '').split('\n').map(l => '      ' + l).join('\n');
let failed = 0, n = 0;
for (const b of NB_ONLY ? [] : runnable) {
  n++;
  const where = `${b.file}:${b.line}`;
  const c = await compile(b.info.lang, b.code, b.info.flags);
  if (!c.wasm && b.info.error) { console.log(`\x1b[32m✓\x1b[0m \x1b[2m[${n}/${runnable.length}] 按预期编译出错\x1b[0m ${where}`); continue; }
  if (!c.wasm) {
    failed++;
    console.log(`\x1b[31m✗ 编译失败\x1b[0m ${where}\n${indent(c.error || '')}`);
    continue;
  }
  const t0 = Date.now();
  const r = await runWasm(c.wasm, b.info.stdin);
  const ms = Date.now() - t0;
  const bad = r.timeout ? `超时（${TIMEOUT / 1000}s）` : r.error ? `运行出错：${r.error}` : null;
  if (b.info.error) {
    if (bad || r.exitCode) { console.log(`\x1b[32m✓\x1b[0m \x1b[2m[${n}/${runnable.length}] 按预期出错\x1b[0m ${where}`); continue; }
    failed++;
    console.log(`\x1b[31m✗ 标了 run: error，但编译和运行都正常\x1b[0m ${where}`);
    continue;
  }
  if (bad) {
    failed++;
    console.log(`\x1b[31m✗ ${bad}\x1b[0m ${where}`);
    if (r.stdout) console.log(indent(r.stdout.slice(0, 600)));
    continue;
  }
  // 跟练：填空要有空可挖；输出要稳定，判定才能和原程序比
  const noFill = S.blanks(b.code, b.opt.blank).length ? '' : ' \x1b[2m（太短，不提供填空）\x1b[0m';
  if (!S.unstable(b.code)) {
    const again = await runWasm(c.wasm, b.info.stdin);
    const cmp = S.compareOutput(again.stdout, r.stdout);
    if (!cmp.ok) {
      failed++;
      console.log(`\x1b[31m✗ 两次运行输出不一样（第 ${cmp.line} 行），但没被识别成「输出不固定」\x1b[0m ${where}\n${indent(cmp.exp + '\n' + cmp.got)}`);
      continue;
    }
  }
  const exit = r.exitCode ? ` \x1b[33m退出码 ${r.exitCode}\x1b[0m` : '';
  console.log(`\x1b[32m✓\x1b[0m \x1b[2m[${n}/${runnable.length}] ${ms}ms\x1b[0m ${where}${exit}${noFill}`);
  if (VERBOSE && r.stdout) console.log(indent(r.stdout.slice(0, 1200)));
}

/* ---------- notebook 文章：整篇拼成一个程序，编译运行，每个语句格的输出和文中紧跟的输出块对比 ---------- */
const OUT_LANG = /^(|text|plain|txt|output|console)$/;
let nbFailed = 0, nbPosts = 0;
for (const f of posts) {
  const md = fs.readFileSync(path.join(DIR, f), 'utf8');
  const fm = (/^---\n([\s\S]*?)\n---/.exec(md) || [, ''])[1];
  if (!NB_FORCE && !/^notebook:\s*true\s*$/m.test(fm)) continue;
  nbPosts++;
  const blocks = S.extractBlocks(md);
  const cells = [];
  let num = 0, hid = 0;
  blocks.forEach((b, k) => {
    const lang = S.normLang(b.lang);
    if (!lang) return;
    const kind = S.cellKind(lang, b.code, b.opt);
    const name = kind === 'hidden' ? 'hidden' + (++hid) : /^(def|main)$/.test(kind) ? 'cell' + (++num) : '-';
    const next = blocks[k + 1];
    const expect = kind === 'main' && !b.opt.loose && next && OUT_LANG.test(next.lang) ? next.code : null;
    cells.push({ kind, code: b.code, name, lang, line: b.line, expect, stdin: b.opt.stdin || '' });
  });
  const lang = S.notebookLang(cells);
  cells.forEach(c => { if (c.lang !== lang && c.kind !== 'alone') c.kind = 'skip'; });   // 另一种语言的格子不参与
  const { source, mains } = S.assemble(lang, cells);
  const kinds = cells.map(c => ({ def: 'D', main: 'M', hidden: 'H', skip: '-', alone: 'A' }[c.kind])).join('');
  const c = await compile(lang, source, []);
  if (!c.wasm) {
    nbFailed++;
    console.log(`\x1b[31m✗ notebook 编译失败\x1b[0m ${f}  ${kinds}`);
    const where = name => { const x = cells.find(y => y.name === name); return x ? `第 ${name.replace(/\D+/, '')} 格（${f}:${x.line}）` : name; };
    for (const d of S.nbDiagnostics(c.error).filter(d => d.severity === 'error').slice(0, 8)) console.log(`      ${where(d.cell)} 第 ${d.line} 行：${d.msg}`);
    if (VERBOSE) console.log(indent(source));
    continue;
  }
  const r = await runWasm(c.wasm, cells.map(x => x.stdin).filter(Boolean).join('\n'));
  const seg = S.splitOutput(r.stdout);
  const problems = [];
  if (r.timeout || r.error) problems.push(`${r.timeout ? '超时' : '运行出错：' + r.error}（停在第 ${seg.last == null ? '?' : cells[seg.last].name.replace(/\D+/, '')} 格）`);
  let compared = 0;
  for (const i of mains) {
    const cell = cells[i];
    if (cell.expect == null || S.unstable(cell.code)) continue;
    compared++;
    const cmp = S.compareOutput(seg.out[i] || '', cell.expect);
    if (!cmp.ok) problems.push(`第 ${cell.name.replace(/\D+/, '')} 格（${f}:${cell.line}）输出第 ${cmp.line} 行和文中不同\n${indent('文中  ' + cmp.exp + '\n实际  ' + cmp.got)}`);
  }
  if (problems.length) { nbFailed++; console.log(`\x1b[31m✗ notebook\x1b[0m ${f}  ${kinds}`); problems.forEach(p => console.log('      ' + p)); }
  else console.log(`\x1b[32m✓\x1b[0m notebook ${f}  ${kinds}  \x1b[2m${mains.length} 个语句格，${compared} 个和文中输出对上\x1b[0m`);
  if (VERBOSE) for (const i of mains) console.log(indent(`[${cells[i].name}]\n` + (seg.out[i] || '')));
}

fs.rmSync(tmp, { recursive: true, force: true });
if (!NB_ONLY) console.log(`\n${runnable.length - failed} / ${runnable.length} 通过`);
if (nbPosts) console.log(`notebook：${nbPosts - nbFailed} / ${nbPosts} 篇通过`);
process.exit(failed || nbFailed ? 1 : 0);
