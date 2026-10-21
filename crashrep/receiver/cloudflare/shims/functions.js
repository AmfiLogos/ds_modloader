// @vercel/functions on Workers: client IP, waitUntil, and a store for the rate counters.

import { env, waitUntil as cfWaitUntil } from 'cloudflare:workers';

// Old loaders reach us through the ds-crashrep.vercel.app proxy. It sends the player's IP with a shared
// secret; without the secret the header is ignored, so nobody can fake an IP to dodge the limits.
export function ipAddress(request) {
  const h = request.headers;
  const proxy = env.PROXY_SECRET;
  if (proxy && h.get('x-dscr-proxy') === proxy && h.get('x-dscr-client-ip')) return h.get('x-dscr-client-ip');
  return h.get('cf-connecting-ip') || '';
}

export function waitUntil(p) {
  cfWaitUntil(p);
}

// Rate counters in R2 (strongly consistent; the Workers Cache API did not keep them on this zone).
// Keys carry their hour/day window; worker.js deletes counters older than 2 days each night.
export const LIMITS_PREFIX = 'ds-crashrep-limits/';

export function getCache({ namespace }) {
  const key = (k) => `${LIMITS_PREFIX}${namespace}/${k}`;
  return {
    async get(k) {
      const o = await env.FILES.get(key(k));
      if (!o) return null;
      const { v, exp } = await o.json();
      return exp > Date.now() ? v : null;
    },
    async set(k, value, { ttl = 3600 } = {}) {
      await env.FILES.put(key(k), JSON.stringify({ v: value, exp: Date.now() + ttl * 1000 }));
    },
    async delete(k) {
      await env.FILES.delete(key(k));
    },
  };
}
