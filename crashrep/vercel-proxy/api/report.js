// ds-crashrep.vercel.app/api/report: loaders v0.1.15-0.1.16.1 have this address compiled in.
// Forward each report to the Cloudflare receiver, with the player's IP for its rate limits
// (sent with the shared PROXY_SECRET; the receiver ignores the IP header without it).
const TARGET = 'https://crashrep.dungeonsettlers.wiki/api/report';
const PASS = ['content-type', 'x-dscr-key', 'x-dscr-install', 'x-dscr-kind', 'x-dscr-admin', 'user-agent'];

export async function POST(request) {
  const h = new Headers();
  for (const k of PASS) {
    const v = request.headers.get(k);
    if (v) h.set(k, v);
  }
  h.set('x-dscr-proxy', process.env.PROXY_SECRET || '');
  h.set('x-dscr-client-ip', request.headers.get('x-real-ip') || (request.headers.get('x-forwarded-for') || '').split(',')[0].trim());
  const r = await fetch(TARGET, { method: 'POST', headers: h, body: await request.arrayBuffer(), signal: AbortSignal.timeout(25000) });
  const out = new Headers({ 'content-type': r.headers.get('content-type') || 'application/json', 'cache-control': 'no-store' });
  const retry = r.headers.get('retry-after');
  if (retry) out.set('retry-after', retry);
  return new Response(await r.arrayBuffer(), { status: r.status, headers: out });
}

export function GET() {
  return new Response(JSON.stringify({ error: 'use POST' }), {
    status: 405,
    headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store', allow: 'POST' },
  });
}
