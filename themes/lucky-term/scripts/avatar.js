/**
 * 首页头像：主题配置 home.avatar 可以写
 *   - 预设名：terminal / identicon / monogram / pixel-cat / pixel-robot / code
 *   - 图片地址：/img/me.png 或 https://...
 *   - github：用 theme.github 对应账号的 GitHub 头像
 *
 * 预设头像在构建时生成 SVG，输出到 /img/avatars/<名字>.svg
 * helper  avatar_src()   当前配置对应的头像地址
 */
'use strict';

const crypto = require('crypto');

const PRESETS = ['terminal', 'identicon', 'monogram', 'pixel-cat', 'pixel-robot', 'code'];

const svg = (body, bg) =>
  `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 420 420" shape-rendering="crispEdges"><rect width="420" height="420" fill="${bg}"/>${body}</svg>`;

// 像素画：每个字符一格，'.' 留空，其余字符按 colors 取色
function pixelArt(rows, colors, bg) {
  const n = Math.max(rows.length, ...rows.map(r => r.length));
  const cell = Math.floor(300 / n);
  const off = (420 - cell * n) / 2;
  let body = '';
  rows.forEach((row, y) => [...row].forEach((ch, x) => {
    if (ch !== '.') body += `<rect x="${off + x * cell}" y="${off + y * cell}" width="${cell}" height="${cell}" fill="${colors[ch]}"/>`;
  }));
  return svg(body, bg);
}

// 和 GitHub 一样的 5×5 左右对称像素块，颜色和图案都由 seed 的哈希决定
function identicon(seed) {
  const h = crypto.createHash('md5').update(String(seed)).digest();
  const hue = Math.round(h[15] / 255 * 360);
  const color = `hsl(${hue}, 55%, 58%)`;
  const cell = 54, off = 75;          // 四周留白，放进圆形头像也不会被裁掉
  let body = '';
  for (let y = 0; y < 5; y++) {
    for (let x = 0; x < 3; x++) {
      if (h[y * 3 + x] % 2 === 0) {
        body += `<rect x="${off + x * cell}" y="${off + y * cell}" width="${cell}" height="${cell}" fill="${color}"/>`;
        if (x < 2) body += `<rect x="${off + (4 - x) * cell}" y="${off + y * cell}" width="${cell}" height="${cell}" fill="${color}"/>`;
      }
    }
  }
  return svg(body, '#f0f0f0');
}

function monogram(text) {
  const ch = [...String(text || '?').trim()][0].toUpperCase();
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 420 420"><defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5b8def"/><stop offset="1" stop-color="#7c5ce0"/></linearGradient></defs><rect width="420" height="420" fill="url(#g)"/><text x="210" y="212" text-anchor="middle" dominant-baseline="central" font-family="-apple-system, 'Helvetica Neue', Arial, sans-serif" font-size="210" font-weight="700" fill="#fff">${ch.replace(/[<&>"]/g, '')}</text></svg>`;
}

const CAT = [
  '..X........X..',
  '.XXX......XXX.',
  '.XpXX....XXpX.',
  '.XXXXXXXXXXXX.',
  'XXXXXXXXXXXXXX',
  'XXwwXXXXXXwwXX',
  'XXwkXXXXXXwkXX',
  'XXXXXXppXXXXXX',
  'XXXXXkXXkXXXXX',
  '.XXXXXkkXXXXX.',
  '.XXXXXXXXXXXX.',
  '..XXXXXXXXXX..'
];
const ROBOT = [
  '......aa......',
  '......XX......',
  '..XXXXXXXXXX..',
  '..XwwXXXXwwX..',
  '..XwkXXXXwkX..',
  '..XXXXXXXXXX..',
  '..XXmmmmmmXX..',
  '..XXXXXXXXXX..',
  '....XXXXXX....',
  '.XXXXXXXXXXXX.',
  '.XXXXaaXXXXXX.',
  '.XXXXXXXXXXXX.'
];

function presetSvg(name, theme) {
  const who = (theme.prompt && theme.prompt.user) || 'lucky';
  switch (name) {
    case 'terminal':
      return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 420 420"><rect width="420" height="420" fill="#0e1116"/><path d="M110 150l65 65-65 65" fill="none" stroke="#3fd68f" stroke-width="32" stroke-linecap="round" stroke-linejoin="round"/><rect x="205" y="262" width="110" height="32" rx="16" fill="#3fd68f"/></svg>';
    case 'identicon': return identicon(who);
    case 'monogram': return monogram(who);
    case 'pixel-cat': return pixelArt(CAT, { X: '#f4a259', p: '#f7a1b5', w: '#ffffff', k: '#2d2a32' }, '#fdf3e7');
    case 'pixel-robot': return pixelArt(ROBOT, { X: '#8fa3bf', a: '#ff6b6b', w: '#ffffff', k: '#1f2937', m: '#4ade80' }, '#e8eef7');
    case 'code':
      return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 420 420"><rect width="420" height="420" fill="#1f2430"/><path d="M150 140l-70 70 70 70M270 140l70 70-70 70" fill="none" stroke="#ffcc66" stroke-width="28" stroke-linecap="round" stroke-linejoin="round"/><path d="M232 120l-44 180" stroke="#73d0ff" stroke-width="28" stroke-linecap="round"/></svg>';
    default: return '';
  }
}

hexo.extend.generator.register('avatars', function () {
  return PRESETS.map(name => ({ path: `img/avatars/${name}.svg`, data: () => presetSvg(name, hexo.theme.config) }));
});

hexo.extend.helper.register('avatar_src', function () {
  const theme = this.theme || {};
  const conf = String((theme.home && theme.home.avatar) || 'terminal').trim();
  if (PRESETS.includes(conf)) return this.url_for(`/img/avatars/${conf}.svg`);
  if (conf === 'github') {
    const m = String(theme.github || '').match(/github\.com\/([^/?#]+)/);
    if (m) return `https://github.com/${m[1]}.png?size=460`;
    return this.url_for('/img/avatars/terminal.svg');
  }
  return /^(https?:)?\/\//.test(conf) ? conf : this.url_for(conf);
});
