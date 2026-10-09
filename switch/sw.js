// SPDX-License-Identifier: GPL-3.0-only
// The firmware switcher off line: its page and FoMni-1's package are kept, and served from here first.
// tools/make_pages.py fills in the version (it changes when the page or the package does) and the list.
const CACHE = "fm1-switch-c20ea1a80508";
const KEEP = ["./", "manifest.webmanifest", "icon-192.png", "icon-512.png", "../firmware/omni-0.4.fwsc"];

self.addEventListener("install", (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(KEEP)).then(() => self.skipWaiting()));
});
self.addEventListener("activate", (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k.startsWith("fm1-switch-") && k !== CACHE).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});
self.addEventListener("fetch", (e) => {
  if (e.request.method !== "GET") return;
  e.respondWith(caches.match(e.request, { ignoreSearch: true }).then((hit) => hit || fetch(e.request)));
});
