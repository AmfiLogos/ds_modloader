import { createHash, createHmac, timingSafeEqual } from 'node:crypto';

export function json(status, body, extraHeaders = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'cache-control': 'no-store',
      ...extraHeaders,
    },
  });
}

// Constant-time string compare (hash both sides so lengths always match).
export function safeEqual(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || !a || !b) return false;
  const ha = createHash('sha256').update(a).digest();
  const hb = createHash('sha256').update(b).digest();
  return timingSafeEqual(ha, hb);
}

// True when the request carries "Authorization: Bearer <ADMIN_TOKEN>".
export function isAdmin(request) {
  const want = process.env.ADMIN_TOKEN;
  if (!want) return false;
  const h = request.headers.get('authorization') || '';
  if (!h.startsWith('Bearer ')) return false;
  return safeEqual(h.slice(7).trim(), want);
}

// Salted hash of the client IP. The plain IP is never stored or logged.
export function ipKey(ip) {
  const salt = process.env.IP_SALT;
  if (!salt) throw new Error('IP_SALT missing');
  return createHmac('sha256', salt).update(ip || 'unknown').digest('hex').slice(0, 32);
}

// Admin web pages: the cookie holds an HMAC of ADMIN_TOKEN, never the token itself.
export const COOKIE = '__Host-dscr_admin';
export function sessionValue() {
  const t = process.env.ADMIN_TOKEN;
  if (!t) return '';
  return createHmac('sha256', t).update('dscr-admin-session-1').digest('hex');
}
export function isAdminCookie(request) {
  const want = sessionValue();
  if (!want) return false;
  const c = request.headers.get('cookie') || '';
  for (const part of c.split(';')) {
    const i = part.indexOf('=');
    if (i > 0 && part.slice(0, i).trim() === COOKIE) return safeEqual(part.slice(i + 1).trim(), want);
  }
  return false;
}
