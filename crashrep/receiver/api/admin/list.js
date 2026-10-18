import { list } from '@vercel/blob';
import { json, isAdmin } from '../../lib/common.js';

export async function GET(request) {
  if (!isAdmin(request)) return json(401, { error: 'unauthorized' });
  const u = new URL(request.url);
  const prefix = u.searchParams.get('prefix') || 'reports/';
  const cursor = u.searchParams.get('cursor') || undefined;
  const r = await list({ prefix, cursor, limit: 1000 });
  return json(200, {
    blobs: r.blobs.map((b) => ({ path: b.pathname, size: b.size, uploadedAt: b.uploadedAt, url: b.url })),
    cursor: r.cursor || null,
    hasMore: r.hasMore,
  });
}
