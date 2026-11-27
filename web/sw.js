'use strict';

const CACHE_NAME = 'cpr-chest-v2.0.0';
const PRECACHE_URLS = [
  './',
  './index.html',
  './app.css?v=2.0.0',
  './app.js?v=2.0.0',
  './manifest.json',
  './icon.svg',
  './fonts/anuphan-thai.woff2',
  './demo/manifest.json',
  './demo/good.jsonl',
  './demo/shallow.jsonl',
  './demo/cycle.jsonl',
  './demo/test2min.jsonl',
  './demo/kgonly_good.jsonl'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then((cache) => cache.addAll(PRECACHE_URLS))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(
        keys
          .filter((key) => key !== CACHE_NAME)
          .map((key) => caches.delete(key))
      ))
      .then(() => self.clients.claim())
  );
});

function isCacheFirstAsset(pathname) {
  return pathname.endsWith('.woff2') || pathname.endsWith('.jsonl');
}

self.addEventListener('fetch', (event) => {
  const req = event.request;
  if (req.method !== 'GET') return;

  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;
  if (url.pathname.endsWith('/ws')) return;

  if (isCacheFirstAsset(url.pathname)) {
    event.respondWith(
      caches.match(req).then((cached) => {
        if (cached) return cached;
        return fetch(req).then((res) => {
          if (res && res.ok) {
            const copy = res.clone();
            caches.open(CACHE_NAME).then((cache) => cache.put(req, copy));
          }
          return res;
        });
      })
    );
    return;
  }

  event.respondWith(
    fetch(req)
      .then((res) => {
        if (res && res.ok) {
          const copy = res.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(req, copy));
        }
        return res;
      })
      .catch(() => {
        if (req.mode === 'navigate') {
          return caches.match('./index.html').then((hit) => hit || caches.match('./'));
        }
        return caches.match(req).then((hit) => hit || caches.match(req, { ignoreSearch: true }));
      })
  );
});
