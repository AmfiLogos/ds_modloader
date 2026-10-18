import { get } from '@vercel/blob';
import { json, isAdmin } from '../../lib/common.js';

export async function GET(request) {
  if (!isAdmin(request)) return json(401, { error: 'unauthorized' });
  const path = new URL(request.url).searchParams.get('path') || '';
  if (!/^reports\/[0-9A-Za-z._\/-]+$/.test(path) || path.includes('..')) {
    return json(400, { error: 'bad path' });
  }
  const r = await get(path, { access: 'private', useCache: false });
  if (!r || r.statusCode !== 200) return json(404, { error: 'not found' });
  return new Response(r.stream, {
    status: 200,
    headers: {
      'content-type': 'application/gzip',
      'content-length': String(r.blob.size),
      'cache-control': 'no-store',
      'content-disposition': `attachment; filename="${path.split('/').pop()}"`,
    },
  });
}
