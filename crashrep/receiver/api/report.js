import { randomBytes } from 'node:crypto';
import { createGunzip } from 'node:zlib';
import { put } from '@vercel/blob';
import { ipAddress } from '@vercel/functions';
import { json, safeEqual, ipKey } from '../lib/common.js';
import { checkIp, checkInstallAndGlobal } from '../lib/limits.js';

const APP_KEY = 'dscr1-621ed5ae365845cd8ed0034a'; // public app key, shipped in the client
const KINDS = new Set(['crash', 'freeze', 'desync', 'join', 'test']);
const MAX_GZ = 4 * 1024 * 1024;
const MAX_RAW = 32 * 1024 * 1024;
const MAGIC = 'DSCRASH 1';

class TooLarge extends Error {}

async function readCapped(request, cap) {
  if (!request.body) return Buffer.alloc(0);
  const reader = request.body.getReader();
  const parts = [];
  let total = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > cap) {
      reader.cancel().catch(() => {});
      throw new TooLarge();
    }
    parts.push(value);
  }
  return Buffer.concat(parts.map((p) => Buffer.from(p.buffer, p.byteOffset, p.byteLength)));
}

// Streams the gzip through inflate, stops past MAX_RAW, keeps only the first line.
function inspectGzip(buf) {
  return new Promise((resolve) => {
    const gz = createGunzip();
    let total = 0;
    let head = Buffer.alloc(0);
    let finished = false;
    const done = (r) => {
      if (finished) return;
      finished = true;
      resolve(r);
    };
    gz.on('data', (chunk) => {
      total += chunk.length;
      if (total > MAX_RAW) {
        gz.destroy();
        done({ error: 'too_large' });
        return;
      }
      if (head.length < 64) head = Buffer.concat([head, chunk.subarray(0, 64 - head.length)]);
    });
    gz.on('error', () => done({ error: 'bad_gzip' }));
    gz.on('end', () => {
      const firstLine = head.toString('utf8').split('\n')[0].replace(/\r$/, '');
      done(firstLine === MAGIC ? { ok: true } : { error: 'bad_header' });
    });
    gz.end(buf);
  });
}

function stamp(now) {
  // ISO 8601 basic format (no colons in the path): 20261018T123456Z
  return now.toISOString().replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z');
}

export async function POST(request) {
  const h = request.headers;
  if (!safeEqual(h.get('x-dscr-key') || '', APP_KEY)) return json(401, { error: 'bad key' });

  const ctype = (h.get('content-type') || '').split(';')[0].trim().toLowerCase();
  if (ctype !== 'application/gzip') return json(415, { error: 'content-type must be application/gzip' });

  const install = h.get('x-dscr-install') || '';
  if (!/^[0-9a-f]{32}$/.test(install)) return json(400, { error: 'bad install id' });
  const kind = h.get('x-dscr-kind') || '';
  if (!KINDS.has(kind)) return json(400, { error: 'bad kind' });

  const admin = process.env.ADMIN_TOKEN;
  const bypass = !!admin && safeEqual(h.get('x-dscr-admin') || '', admin);

  if (!bypass) {
    const retry = await checkIp(ipKey(ipAddress(request) || h.get('x-forwarded-for') || ''));
    if (retry) return json(429, { error: 'rate limited' }, { 'retry-after': String(retry) });
  }

  const len = Number(h.get('content-length') || '0');
  if (len > MAX_GZ) return json(413, { error: 'report too large' });
  let body;
  try {
    body = await readCapped(request, MAX_GZ);
  } catch (e) {
    if (e instanceof TooLarge) return json(413, { error: 'report too large' });
    throw e;
  }
  if (body.length < 18) return json(400, { error: 'empty or not gzip' });

  const check = await inspectGzip(body);
  if (check.error === 'too_large') return json(413, { error: 'report too large when unpacked' });
  if (check.error === 'bad_gzip') return json(400, { error: 'not gzip' });
  if (check.error) return json(400, { error: 'bad report header' });

  let commit = null;
  if (!bypass) {
    const r = await checkInstallAndGlobal(install);
    if (r.retryAfter) return json(429, { error: 'rate limited' }, { 'retry-after': String(r.retryAfter) });
    commit = r.commit;
  }

  const now = new Date();
  const d = now.toISOString();
  const path =
    `reports/${d.slice(0, 4)}/${d.slice(5, 7)}/${d.slice(8, 10)}/` +
    `${stamp(now)}-${kind}-${install.slice(0, 8)}-${randomBytes(6).toString('hex')}.txt.gz`;
  await put(path, body, {
    access: 'private',
    contentType: 'application/gzip',
    addRandomSuffix: false,
  });
  if (commit) await commit();
  return json(201, { id: path });
}

export function GET() {
  return json(405, { error: 'use POST' }, { allow: 'POST' });
}
