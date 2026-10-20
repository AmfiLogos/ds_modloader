import { randomBytes } from 'node:crypto';
import { createGunzip } from 'node:zlib';
import { put } from '@vercel/blob';
import { ipAddress, waitUntil } from '@vercel/functions';
import { json, safeEqual, ipKey } from '../lib/common.js';
import { checkIp, checkInstallAndGlobal } from '../lib/limits.js';
import { parseReport, checkBug, summary } from '../lib/parse.js';
import { pingDiscord } from '../lib/discord.js';

const APP_KEY = 'dscr1-621ed5ae365845cd8ed0034a'; // public app key, shipped in the client
const KINDS = new Set(['bug', 'crash', 'freeze', 'desync', 'join', 'blank', 'test']);
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

// Streams the gzip through inflate, stops past MAX_RAW, returns the unpacked text.
function inflateCapped(buf) {
  return new Promise((resolve) => {
    const gz = createGunzip();
    let total = 0;
    const parts = [];
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
      parts.push(chunk);
    });
    gz.on('error', () => done({ error: 'bad_gzip' }));
    gz.on('end', () => {
      const text = Buffer.concat(parts).toString('utf8');
      const firstLine = text.slice(0, 64).split('\n')[0].replace(/\r$/, '');
      done(firstLine === MAGIC ? { ok: true, text } : { error: 'bad_header' });
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

  const check = await inflateCapped(body);
  if (check.error === 'too_large') return json(413, { error: 'report too large when unpacked' });
  if (check.error === 'bad_gzip') return json(400, { error: 'not gzip' });
  if (check.error) return json(400, { error: 'bad report header' });
  const parsed = parseReport(check.text);
  check.text = null;
  let bug = null;
  if (kind === 'bug') {
    bug = checkBug(parsed);
    if (bug.error) return json(400, { error: bug.error });
  }

  let commit = null;
  if (!bypass) {
    const r = await checkInstallAndGlobal(install);
    if (r.retryAfter) return json(429, { error: 'rate limited' }, { 'retry-after': String(r.retryAfter) });
    commit = r.commit;
  }

  const now = new Date();
  const d = now.toISOString();
  const base =
    `reports/${d.slice(0, 4)}/${d.slice(5, 7)}/${d.slice(8, 10)}/` +
    `${stamp(now)}-${kind}-${install.slice(0, 8)}-${randomBytes(6).toString('hex')}`;
  const path = `${base}.txt.gz`;
  const shot = bug && bug.shot ? `${base}.shot.${bug.shot.ext}` : null;
  const opts = { access: 'private', addRandomSuffix: false };
  const meta = summary(parsed, kind, { size: body.length, shot, shotW: bug?.shot?.w || 0, shotH: bug?.shot?.h || 0 });
  await Promise.all([
    put(path, body, { ...opts, contentType: 'application/gzip' }),
    put(`${base}.meta.json`, JSON.stringify(meta), { ...opts, contentType: 'application/json' }),
    shot ? put(shot, bug.shot.buf, { ...opts, contentType: bug.shot.ext === 'png' ? 'image/png' : 'image/jpeg' }) : null,
  ]);
  if (commit) await commit();
  // Tests (admin bypass) never ping Discord.
  if (!bypass) waitUntil(pingDiscord(kind, meta, path).catch(() => {}));
  return json(201, { id: path });
}

export function GET() {
  return json(405, { error: 'use POST' }, { allow: 'POST' });
}
