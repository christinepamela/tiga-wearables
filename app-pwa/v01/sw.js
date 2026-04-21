/*
 * TIGA Companion App — Service Worker v0.1
 * Minimal SW for PWA install + offline fallback.
 * Caches the shell on install, serves from cache first.
 */
const CACHE_NAME = 'tiga-v0-1';
const ASSETS = [
  './',
  './index.html',
  './manifest.json'
];

self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(CACHE_NAME).then(cache => cache.addAll(ASSETS))
  );
  self.skipWaiting();
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(names =>
      Promise.all(names
        .filter(n => n !== CACHE_NAME)
        .map(n => caches.delete(n)))
    )
  );
  self.clients.claim();
});

self.addEventListener('fetch', event => {
  event.respondWith(
    caches.match(event.request).then(resp => resp || fetch(event.request))
  );
});
