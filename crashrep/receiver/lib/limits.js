// Rate-limit counters in Vercel Runtime Cache (fixed windows, per region).
import { getCache } from '@vercel/functions';

export const LIMITS = {
  ipHour: 6,
  ipDay: 20,
  installDay: 5,
  globalDay: 2000,
};

// Keys are already short and unique; skip the default 32-bit hash (collisions).
const cache = getCache({ namespace: 'dscr1', keyHashFunction: (k) => k });

function windows(now = new Date()) {
  const iso = now.toISOString(); // 2026-10-18T12:34:56.789Z
  const day = iso.slice(0, 10).replace(/-/g, '');
  const hour = day + iso.slice(11, 13);
  const nextHour = new Date(now);
  nextHour.setUTCMinutes(60, 0, 0);
  const nextDay = new Date(now);
  nextDay.setUTCHours(24, 0, 0, 0);
  return {
    day,
    hour,
    hourLeft: Math.max(1, Math.ceil((nextHour - now) / 1000)),
    dayLeft: Math.max(1, Math.ceil((nextDay - now) / 1000)),
  };
}

async function read(key) {
  const v = await cache.get(key);
  const n = typeof v === 'number' ? v : Number(v);
  return Number.isFinite(n) ? n : 0;
}

async function bump(key, value, ttl) {
  await cache.set(key, value + 1, { ttl, name: '' });
}

// Phase 1 (every keyed request): per-IP counters. Returns null or retryAfter seconds.
export async function checkIp(ipHash) {
  const w = windows();
  const kh = `ip:h:${ipHash}:${w.hour}`;
  const kd = `ip:d:${ipHash}:${w.day}`;
  const [h, d] = await Promise.all([read(kh), read(kd)]);
  if (d >= LIMITS.ipDay) return w.dayLeft;
  if (h >= LIMITS.ipHour) return w.hourLeft;
  await Promise.all([bump(kh, h, 3700), bump(kd, d, 90000)]);
  return null;
}

// Phase 2 (before storing): per-install and global day caps.
export async function checkInstallAndGlobal(install) {
  const w = windows();
  const ki = `inst:d:${install}:${w.day}`;
  const kg = `glob:d:${w.day}`;
  const [i, g] = await Promise.all([read(ki), read(kg)]);
  if (g >= LIMITS.globalDay) return { retryAfter: w.dayLeft };
  if (i >= LIMITS.installDay) return { retryAfter: w.dayLeft };
  return { commit: () => Promise.all([bump(ki, i, 90000), bump(kg, g, 90000)]) };
}

// Admin only: clear the per-IP counters of one IP hash (used after tests).
export async function resetIp(ipHash) {
  const w = windows();
  await Promise.all([
    cache.delete(`ip:h:${ipHash}:${w.hour}`),
    cache.delete(`ip:d:${ipHash}:${w.day}`),
  ]);
}

// Which store backs the counters: 'runtime-cache' when deployed, 'memory' if unavailable.
export function cacheBackend() {
  const ctx = globalThis[Symbol.for('@vercel/request-context')]?.get?.() ?? {};
  if (ctx.cache || (process.env.RUNTIME_CACHE_ENDPOINT && process.env.RUNTIME_CACHE_HEADERS)) {
    return 'runtime-cache';
  }
  return 'memory';
}
