// Daily: delete reports older than 90 days (with their .meta.json and .shot.* files). Paths are reports/YYYY/MM/DD/...
import { list, del } from '@vercel/blob';
import { json, safeEqual } from '../../lib/common.js';

const KEEP_DAYS = 90;

async function folders(prefix) {
  const out = [];
  let cursor;
  do {
    const r = await list({ prefix, mode: 'folded', cursor, limit: 1000 });
    out.push(...r.folders);
    cursor = r.hasMore ? r.cursor : undefined;
  } while (cursor);
  return out;
}

// Deletes blobs under prefix; if cutoff is set, only those uploaded before it.
async function purge(prefix, cutoff) {
  let n = 0;
  let cursor;
  do {
    const r = await list({ prefix, cursor, limit: 1000 });
    const old = r.blobs.filter((b) => !cutoff || new Date(b.uploadedAt) < cutoff).map((b) => b.url);
    if (old.length) await del(old);
    n += old.length;
    cursor = r.hasMore ? r.cursor : undefined;
  } while (cursor);
  return n;
}

export async function GET(request) {
  const secret = process.env.CRON_SECRET;
  if (!secret || !safeEqual(request.headers.get('authorization') || '', `Bearer ${secret}`)) {
    return json(401, { error: 'unauthorized' });
  }
  const cutoff = new Date(Date.now() - KEEP_DAYS * 86400 * 1000);
  const cutDay = cutoff.toISOString().slice(0, 10).replace(/-/g, '/'); // YYYY/MM/DD
  let deleted = 0;
  for (const y of await folders('reports/')) {
    const yy = y.slice(8, 12);
    if (yy > cutDay.slice(0, 4)) continue;
    for (const m of await folders(y)) {
      const ym = m.slice(8, 15); // YYYY/MM
      if (ym > cutDay.slice(0, 7)) continue;
      for (const d of await folders(m)) {
        const ymd = d.slice(8, 18); // YYYY/MM/DD
        if (ymd < cutDay) deleted += await purge(d, null);
        else if (ymd === cutDay) deleted += await purge(d, cutoff);
      }
    }
  }
  // "Handled" markers (state/handled/...) older than the cutoff belong to deleted reports.
  deleted += await purge('state/', cutoff);
  console.log(`expire: deleted ${deleted} reports older than ${cutoff.toISOString()}`);
  return json(200, { deleted, cutoff: cutoff.toISOString() });
}
