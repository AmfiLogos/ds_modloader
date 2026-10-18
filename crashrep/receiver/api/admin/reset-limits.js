// Admin only: clears the per-IP rate counters of the caller's own IP (for tests).
import { ipAddress } from '@vercel/functions';
import { json, isAdmin, ipKey } from '../../lib/common.js';
import { resetIp, cacheBackend } from '../../lib/limits.js';

export async function POST(request) {
  if (!isAdmin(request)) return json(401, { error: 'unauthorized' });
  await resetIp(ipKey(ipAddress(request) || request.headers.get('x-forwarded-for') || ''));
  return json(200, { ok: true, backend: cacheBackend() });
}
