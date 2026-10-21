// Cloudflare entry for the crash-report receiver. Same routes as vercel.json; handlers unchanged
// (wrangler aliases @vercel/blob and @vercel/functions to ./shims).
import * as report from '../api/report.js';
import * as privacy from '../api/privacy.js';
import * as web from '../api/web.js';
import * as adminList from '../api/admin/list.js';
import * as adminGet from '../api/admin/get.js';
import * as adminReset from '../api/admin/reset-limits.js';
import * as expire from '../api/cron/expire.js';
import { LIMITS_PREFIX } from './shims/functions.js';

const ROUTES = {
  '/api/report': report,
  '/api/privacy': privacy,
  '/privacy': privacy,
  '/api/web': web,
  '/admin': web,
  '/api/admin/list': adminList,
  '/api/admin/get': adminGet,
  '/api/admin/reset-limits': adminReset,
  '/api/cron/expire': expire,
};

function notFound() {
  return new Response('Not found', { status: 404, headers: { 'content-type': 'text/plain', 'cache-control': 'no-store' } });
}

// Rate counters live at most 25 h; drop the ones older than 2 days.
async function pruneLimits(env) {
  const cutoff = Date.now() - 2 * 86400e3;
  let cursor;
  do {
    const r = await env.FILES.list({ prefix: LIMITS_PREFIX, cursor, limit: 1000 });
    const old = r.objects.filter((o) => o.uploaded.getTime() < cutoff).map((o) => o.key);
    if (old.length) await env.FILES.delete(old);
    cursor = r.truncated ? r.cursor : undefined;
  } while (cursor);
}

export default {
  async fetch(request) {
    const url = new URL(request.url);
    let mod = ROUTES[url.pathname];
    // vercel.json: /admin/:r -> /api/web?r=:r
    const m = /^\/admin\/([^/]+)$/.exec(url.pathname);
    if (!mod && m) {
      url.searchParams.set('r', decodeURIComponent(m[1]));
      request = new Request(url, request);
      mod = web;
    }
    if (!mod) return notFound();
    const handler = mod[request.method];
    if (!handler) return new Response(null, { status: 405, headers: { allow: Object.keys(mod).filter((k) => /^[A-Z]+$/.test(k)).join(', ') } });
    return handler(request);
  },

  // vercel.json cron "17 4 * * *": run the expire handler with the cron secret, as Vercel does.
  async scheduled(controller, env, ctx) {
    const req = new Request('https://cron.internal/api/cron/expire', { headers: { authorization: `Bearer ${env.CRON_SECRET}` } });
    ctx.waitUntil(expire.GET(req).then((r) => r.text()).then((t) => console.log('expire', t)));
    ctx.waitUntil(pruneLimits(env));
  },
};
