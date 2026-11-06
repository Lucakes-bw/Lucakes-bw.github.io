/* lucky-term 的 Service Worker：让切换页面不再每次都等网络
 *
 * GitHub Pages 只让浏览器缓存 10 分钟（max-age=600），之后每次切页都要重新问一遍 CSS / JS 变了没有，
 * 国内访问一次往返就是几百毫秒。这里按资源的特点分别处理：
 *
 *   带 ?v=hash 的主题 CSS / JS、字体、带版本号的 CDN 包   缓存优先：内容一变地址就变，缓存里有就不用再问
 *   页面 HTML、search.json                               先给缓存（瞬间打开），同时后台取最新的，下次打开就是新的
 *   文章图片、可视化脚本                                 同上
 *
 * 构建时由 scripts/sw.js 把 __VERSION__ 换成主题文件的哈希；主题一改版本就变，旧缓存在 activate 时清掉。
 */
'use strict';

const VERSION = '__VERSION__';
const STATIC = 'lt-static-' + VERSION;   // 地址里带版本号的文件
const PAGES = 'lt-pages-' + VERSION;     // HTML 和 search.json
const ASSETS = 'lt-assets-' + VERSION;   // 图片等
const CDN = 'lt-cdn';                     // 带版本号的第三方包（Clang、Monaco、Pyodide），跟主题版本无关，不随之清理
const KEEP = [STATIC, PAGES, ASSETS, CDN];
const MAX_PAGES = 200;
const MAX_ASSETS = 300;

self.addEventListener('install', () => self.skipWaiting());

self.addEventListener('activate', event => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.filter(n => n.startsWith('lt-') && !KEEP.includes(n)).map(n => caches.delete(n)));
    await self.clients.claim();
  })());
});

// 只缓存完整、正常、同源或允许跨域读取的响应；重定向过的响应不能用来回复页面跳转
const cacheable = res => res && res.ok && res.status === 200 && !res.redirected && (res.type === 'basic' || res.type === 'cors');

async function trim(name, max) {
  const cache = await caches.open(name);
  const keys = await cache.keys();
  for (let i = 0; i < keys.length - max; i++) await cache.delete(keys[i]);
}

async function cacheFirst(event, name) {
  const cache = await caches.open(name);
  const hit = await cache.match(event.request);
  if (hit) return hit;
  const res = await fetch(event.request);
  if (cacheable(res)) event.waitUntil(cache.put(event.request, res.clone()));
  return res;
}

async function staleWhileRevalidate(event, name, max) {
  const cache = await caches.open(name);
  const hit = await cache.match(event.request, { ignoreVary: true });
  const update = fetch(event.request).then(async res => {
    if (cacheable(res)) {
      await cache.put(event.request, res.clone());
      if (max) await trim(name, max);
    }
    return res;
  });
  if (hit) {
    event.waitUntil(update.catch(() => {}));
    return hit;
  }
  return update;
}

self.addEventListener('fetch', event => {
  const req = event.request;
  if (req.method !== 'GET' || req.headers.has('range')) return;   // 视频的分段请求交给浏览器
  const url = new URL(req.url);

  if (url.origin === self.location.origin) {
    const p = url.pathname;
    if (p.endsWith('/sw.js')) return;
    if (/[?&]v=[0-9a-f]{8}\b/.test(url.search) || p.includes('/fonts/')) {
      event.respondWith(cacheFirst(event, STATIC));
    } else if (req.mode === 'navigate' || p.endsWith('/') || p.endsWith('.html') || p.endsWith('/search.json')) {
      if (url.search) return;   // 带参数的页面地址不缓存
      event.respondWith(staleWhileRevalidate(event, PAGES, MAX_PAGES));
    } else if (/\.(png|jpe?g|gif|webp|avif|svg|js)$/.test(p)) {
      event.respondWith(staleWhileRevalidate(event, ASSETS, MAX_ASSETS));
    }
    return;
  }

  // jsdelivr 上写死版本号的包：内容永远不变
  if (url.hostname === 'cdn.jsdelivr.net' && /@\d|\/pyodide\/v\d/.test(url.pathname)) {
    event.respondWith(cacheFirst(event, CDN));
  }
});
