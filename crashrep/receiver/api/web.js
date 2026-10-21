// Private admin pages: /admin (list), /admin/r?id= (one report), /admin/shot, /admin/dl,
// /admin/handled, /admin/login, /admin/logout. Rewritten here by vercel.json.
// Login: ADMIN_TOKEN typed once; the cookie (HttpOnly, Secure, SameSite=Strict) holds an HMAC of it.
import { gunzipSync } from 'node:zlib';
import { list, get, put, del } from '@vercel/blob';
import { ipAddress } from '@vercel/functions';
import { safeEqual, ipKey, COOKIE, sessionValue, isAdminCookie } from '../lib/common.js';
import { checkLogin } from '../lib/limits.js';
import { parseReport, summary, SHOT_NAMES } from '../lib/parse.js';

const ID_RE = /^reports\/\d{4}\/\d{2}\/\d{2}\/[0-9A-Za-z-]+\.txt\.gz$/;
const KIND_LIST = ['bug', 'crash', 'freeze', 'desync', 'join', 'blank', 'test'];
const PAGE = 40;
const SHOW_MAX = 200 * 1024; // per log section shown in the page; the gzip has it all

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
const baseOf = (id) => id.slice(0, -'.txt.gz'.length);
const fileOf = (id) => id.split('/').pop();
const kindOf = (id) => (fileOf(id).match(/^\d{8}T\d{6}Z-([a-z]+)-/) || [])[1] || '?';
const timeOf = (id) => {
  const m = fileOf(id).match(/^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})Z/);
  return m ? `${m[1]}-${m[2]}-${m[3]} ${m[4]}:${m[5]} UTC` : '?';
};
const markerOf = (id) => `state/handled/${fileOf(id).replace(/\.txt\.gz$/, '')}.json`;

const SEC_HEADERS = {
  'cache-control': 'no-store',
  'x-content-type-options': 'nosniff',
  'referrer-policy': 'no-referrer',
  'x-frame-options': 'DENY',
  'x-robots-tag': 'noindex, nofollow',
  'content-security-policy': "default-src 'none'; img-src 'self'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'",
};

function html(status, title, body, extra = {}) {
  const page = `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><meta name="robots" content="noindex">
<title>${esc(title)}</title><style>${CSS}</style></head><body><main>${body}</main></body></html>`;
  return new Response(page, { status, headers: { 'content-type': 'text/html; charset=utf-8', ...SEC_HEADERS, ...extra } });
}
const redirect = (to, extra = {}) => new Response(null, { status: 303, headers: { location: to, ...SEC_HEADERS, ...extra } });

const CSS = `
:root{--bg:#14100c;--panel:#221a12;--panel2:#2b2117;--line:#4f3d29;--text:#efe4d2;--dim:#b3a189;--gold:#e5b95c;--red:#e0705a;--green:#8fc46a;--blue:#79aee0}
*{box-sizing:border-box}html{background:var(--bg)}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:64rem;margin:0 auto;padding:16px}
h1{font-size:1.3rem;color:var(--gold);margin:.2rem 0 1rem;letter-spacing:.02em}
h2{font-size:1.05rem;color:var(--gold);margin:1.2rem 0 .5rem}
a{color:var(--gold)}
.top{display:flex;gap:.6rem;align-items:center;justify-content:space-between;flex-wrap:wrap}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin:0 0 12px}
form.filters{display:flex;gap:.5rem;flex-wrap:wrap;align-items:end}
label{display:flex;flex-direction:column;font-size:.8rem;color:var(--dim);gap:2px}
input,select,button{font:inherit;color:var(--text);background:var(--panel2);border:1px solid var(--line);border-radius:7px;padding:.45rem .6rem;min-height:2.4rem}
button{cursor:pointer;background:#3a2b1a;border-color:#7a5d33;color:var(--gold);font-weight:600}
button:hover{background:#4a3720}
.list{list-style:none;margin:0;padding:0}
.item{display:block;text-decoration:none;color:inherit;background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:10px 12px;margin:0 0 8px}
.item:hover{border-color:var(--gold)}
.item.done{opacity:.55}
.row{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;font-size:.85rem;color:var(--dim)}
.txt{margin-top:4px;overflow-wrap:anywhere}
.badge{display:inline-block;font-size:.75rem;font-weight:700;text-transform:uppercase;letter-spacing:.04em;padding:1px 8px;border-radius:999px;background:#3a2b1a;color:var(--gold);border:1px solid #7a5d33}
.badge.crash,.badge.freeze{background:#3d1d17;color:var(--red);border-color:#7a3a2e}
.badge.desync,.badge.join,.badge.blank{background:#172a3d;color:var(--blue);border-color:#2e557a}
.badge.test{background:#262626;color:#aaa;border-color:#555}
.badge.ok{background:#1d3317;color:var(--green);border-color:#3f6a2e}
dl.kv{display:grid;grid-template-columns:max-content 1fr;gap:4px 14px;margin:0}
dl.kv dt{color:var(--dim)}dl.kv dd{margin:0;overflow-wrap:anywhere}
pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#0e0b08;border:1px solid var(--line);border-radius:8px;padding:10px;margin:.4rem 0 0;font:12.5px/1.4 ui-monospace,Menlo,Consolas,monospace;max-height:70vh;overflow:auto}
pre.bugtext{font:15px/1.5 system-ui,sans-serif;max-height:none}
details{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:8px 12px;margin:0 0 8px}
summary{cursor:pointer;color:var(--gold);font-weight:600}
img.shot{display:block;max-width:100%;height:auto;border:1px solid var(--line);border-radius:8px}
.actions{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center}
.btn{display:inline-flex;align-items:center;min-height:2.4rem;padding:.45rem .8rem;border-radius:7px;background:#3a2b1a;border:1px solid #7a5d33;color:var(--gold);font-weight:600;text-decoration:none}
.err{color:var(--red)}.dim{color:var(--dim)}
.pager{display:flex;gap:.6rem;justify-content:space-between;margin-top:8px}
`;

async function readBlob(path) {
  const r = await get(path, { access: 'private', useCache: false });
  if (!r || r.statusCode !== 200) return null;
  const chunks = [];
  for await (const c of r.stream) chunks.push(Buffer.from(c));
  return { buf: Buffer.concat(chunks), size: r.blob.size, type: r.blob.contentType };
}

async function listAll(prefix) {
  const out = [];
  let cursor;
  do {
    const r = await list({ prefix, cursor, limit: 1000 });
    out.push(...r.blobs);
    cursor = r.hasMore ? r.cursor : undefined;
  } while (cursor && out.length < 20000);
  return out;
}

async function mapLimit(items, n, fn) {
  const out = new Array(items.length);
  let i = 0;
  await Promise.all(Array.from({ length: Math.min(n, items.length) }, async () => {
    while (i < items.length) {
      const k = i++;
      out[k] = await fn(items[k]);
    }
  }));
  return out;
}

async function readMeta(id) {
  try {
    const b = await readBlob(`${baseOf(id)}.meta.json`);
    return b ? JSON.parse(b.buf.toString('utf8')) : null;
  } catch {
    return null;
  }
}

function sameOrigin(request) {
  const o = request.headers.get('origin');
  const host = request.headers.get('host');
  return !!o && !!host && o === `https://${host}`;
}

function loginPage(msg = '') {
  return html(msg ? 401 : 200, 'Reports: sign in', `
<h1>DS Modded reports</h1>
<form class="panel" method="post" action="/admin/login" style="display:flex;flex-direction:column;gap:.7rem;max-width:26rem">
<label>Admin token<input type="password" name="token" autocomplete="current-password" required autofocus></label>
${msg ? `<p class="err" style="margin:0">${esc(msg)}</p>` : ''}
<button type="submit">Sign in</button>
</form>`);
}

async function listPage(url) {
  const q = url.searchParams;
  const kind = KIND_LIST.includes(q.get('kind')) ? q.get('kind') : '';
  const ver = (q.get('ver') || '').trim().slice(0, 40);
  const state = ['open', 'done'].includes(q.get('state')) ? q.get('state') : '';
  const page = Math.max(0, Math.min(1000, parseInt(q.get('p') || '0', 10) || 0));

  const [blobs, marks] = await Promise.all([listAll('reports/'), listAll('state/handled/')]);
  const done = new Set(marks.map((b) => fileOf(b.pathname).replace(/\.json$/, '')));
  let ids = blobs.map((b) => b.pathname).filter((p) => ID_RE.test(p));
  const sizes = new Map(blobs.map((b) => [b.pathname, b.size]));
  ids.sort((a, b) => (fileOf(a) < fileOf(b) ? 1 : -1));
  const isDone = (id) => done.has(fileOf(id).replace(/\.txt\.gz$/, ''));
  if (kind) ids = ids.filter((id) => kindOf(id) === kind);
  if (state) ids = ids.filter((id) => (state === 'done') === isDone(id));

  let rows;
  let total;
  if (ver) {
    const cand = ids.slice(0, 600);
    const metas = await mapLimit(cand, 24, readMeta);
    const hit = cand.map((id, i) => [id, metas[i]]).filter(([, m]) => m && String(m.loader).includes(ver));
    total = hit.length;
    rows = hit.slice(page * PAGE, page * PAGE + PAGE);
  } else {
    total = ids.length;
    const slice = ids.slice(page * PAGE, page * PAGE + PAGE);
    const metas = await mapLimit(slice, 24, readMeta);
    rows = slice.map((id, i) => [id, metas[i]]);
  }

  const qs = (p) => {
    const u = new URLSearchParams();
    if (kind) u.set('kind', kind);
    if (ver) u.set('ver', ver);
    if (state) u.set('state', state);
    if (p) u.set('p', String(p));
    const s = u.toString();
    return '/admin' + (s ? '?' + s : '');
  };
  const items = rows.map(([id, m]) => {
    const k = kindOf(id);
    const txt = m ? (m.text || m.reason || '') : '';
    return `<li><a class="item${isDone(id) ? ' done' : ''}" href="/admin/r?id=${encodeURIComponent(id)}">
<div class="row"><span class="badge ${esc(k)}">${esc(k)}</span>${isDone(id) ? '<span class="badge ok">handled</span>' : ''}
<span>${esc(timeOf(id))}</span>${m ? `<span>kit ${esc(m.loader || '?')}</span><span>${esc(m.platform || m.os || '')}</span>` : ''}
${m && m.shot ? '<span>screenshot</span>' : ''}${m && m.contact ? '<span>contact</span>' : ''}<span>${Math.ceil((sizes.get(id) || 0) / 1024)} KB</span></div>
${txt ? `<div class="txt">${esc(txt.slice(0, 200))}${txt.length > 200 ? '…' : ''}</div>` : ''}</a></li>`;
  }).join('');

  const opt = (v, cur, label) => `<option value="${esc(v)}"${v === cur ? ' selected' : ''}>${esc(label)}</option>`;
  return html(200, 'Reports', `
<div class="top"><h1>DS Modded reports</h1>
<form method="post" action="/admin/logout"><button type="submit">Sign out</button></form></div>
<form class="panel filters" method="get" action="/admin">
<label>Kind<select name="kind">${opt('', kind, 'All')}${KIND_LIST.map((k) => opt(k, kind, k)).join('')}</select></label>
<label>Kit version<input name="ver" value="${esc(ver)}" placeholder="for example 0.20" size="10"></label>
<label>State<select name="state">${opt('', state, 'All')}${opt('open', state, 'Open')}${opt('done', state, 'Handled')}</select></label>
<button type="submit">Filter</button></form>
<p class="dim">${total} report(s), newest first.</p>
<ul class="list">${items || '<li class="dim">No reports.</li>'}</ul>
<div class="pager"><span>${page > 0 ? `<a class="btn" href="${qs(page - 1)}">Newer</a>` : ''}</span>
<span>${(page + 1) * PAGE < total ? `<a class="btn" href="${qs(page + 1)}">Older</a>` : ''}</span></div>`);
}

async function detailPage(id) {
  const b = await readBlob(id);
  if (!b) return html(404, 'Not found', '<h1>Report not found</h1><p><a href="/admin">Back to the list</a></p>');
  let text;
  try {
    text = gunzipSync(b.buf, { maxOutputLength: 32 * 1024 * 1024 }).toString('utf8');
  } catch {
    return html(500, 'Bad report', '<h1>This report could not be unpacked</h1><p><a href="/admin">Back</a></p>');
  }
  const parsed = parseReport(text);
  text = null;
  const k = kindOf(id);
  let meta = await readMeta(id);
  if (!meta) {
    meta = summary(parsed, k, { size: b.size, shot: null });
    put(`${baseOf(id)}.meta.json`, JSON.stringify(meta), { access: 'private', addRandomSuffix: false, contentType: 'application/json', allowOverwrite: true }).catch(() => {});
  }
  const handled = !!(await list({ prefix: markerOf(id), limit: 1 })).blobs.length;
  const f = parsed.fields;
  const kv = [
    ['Date', timeOf(id)], ['Kit (loader)', f.loader], ['Game build', f.game_build], ['Platform', f.platform],
    ['OS', f.os], ['CPU', f.cpu], ['Sent', f.sent], ['Contact', f.contact], ['Reason', f.reason],
    ['Install', f.install ? f.install.slice(0, 8) + '…' : ''], ['Mods', f.mods],
  ].filter(([, v]) => v).map(([a, v]) => `<dt>${esc(a)}</dt><dd>${esc(v)}</dd>`).join('');
  const other = Object.entries(f).filter(([key]) => !['loader', 'game_build', 'platform', 'os', 'cpu', 'sent', 'contact', 'reason', 'install', 'mods', 'kind'].includes(key));
  const bug = parsed.sections.find((s) => s.name === 'bug.txt');
  const logs = parsed.sections.filter((s) => s.name !== 'bug.txt' && !(s.name in SHOT_NAMES)).map((s) => {
    const cut = s.body.length > SHOW_MAX;
    const body = cut ? s.body.slice(-SHOW_MAX) : s.body;
    return `<details><summary>${esc(s.name)} <span class="dim">${esc(s.info)}</span></summary>
${cut ? `<p class="dim">Showing the last ${SHOW_MAX / 1024} KB. Download the gzip for all of it.</p>` : ''}<pre>${esc(body)}</pre></details>`;
  }).join('');
  const qid = encodeURIComponent(id);
  return html(200, `Report ${k}`, `
<p><a href="/admin">&larr; All reports</a></p>
<div class="top"><h1><span class="badge ${esc(k)}">${esc(k)}</span> ${esc(timeOf(id))}</h1>
<div class="actions"><a class="btn" href="/admin/dl?id=${qid}">Download gzip</a>
<form method="post" action="/admin/handled"><input type="hidden" name="id" value="${esc(id)}"><input type="hidden" name="on" value="${handled ? '0' : '1'}">
<button type="submit">${handled ? 'Mark open again' : 'Mark handled'}</button></form>${handled ? '<span class="badge ok">handled</span>' : ''}</div></div>
${bug ? `<h2>Player text</h2><pre class="bugtext">${esc(bug.body)}</pre>` : ''}
${meta.shot ? `<h2>Screenshot</h2><a href="/admin/shot?id=${qid}"><img class="shot" src="/admin/shot?id=${qid}" alt="Player screenshot"${meta.shotW ? ` width="${meta.shotW}" height="${meta.shotH}"` : ''}></a>` : ''}
<h2>Details</h2><div class="panel"><dl class="kv">${kv}${other.map(([a, v]) => `<dt>${esc(a)}</dt><dd>${esc(v)}</dd>`).join('')}</dl></div>
<h2>Logs</h2>${logs || '<p class="dim">No log sections.</p>'}
<p class="dim">${esc(id)}</p>`);
}

export async function GET(request) {
  const url = new URL(request.url);
  const r = url.searchParams.get('r') || url.pathname.replace(/^\/(api\/web|admin)\/?/, '');
  if (!isAdminCookie(request)) {
    if (r === '' || r === 'r') return loginPage();
    return new Response('unauthorized', { status: 401, headers: SEC_HEADERS });
  }
  const id = url.searchParams.get('id') || '';
  if (r === '') return listPage(url);
  if (!ID_RE.test(id)) return html(400, 'Bad id', '<h1>Bad report id</h1><p><a href="/admin">Back</a></p>');
  if (r === 'r') return detailPage(id);
  if (r === 'dl') {
    const g = await get(id, { access: 'private', useCache: false });
    if (!g || g.statusCode !== 200) return new Response('not found', { status: 404, headers: SEC_HEADERS });
    return new Response(g.stream, { status: 200, headers: { ...SEC_HEADERS, 'content-type': 'application/gzip', 'content-length': String(g.blob.size), 'content-disposition': `attachment; filename="${fileOf(id)}"` } });
  }
  if (r === 'shot') {
    const meta = await readMeta(id);
    const p = meta && meta.shot;
    if (!p || p !== `${baseOf(id)}.shot.png` && p !== `${baseOf(id)}.shot.jpg`) return new Response('not found', { status: 404, headers: SEC_HEADERS });
    const g = await get(p, { access: 'private', useCache: false });
    if (!g || g.statusCode !== 200) return new Response('not found', { status: 404, headers: SEC_HEADERS });
    return new Response(g.stream, { status: 200, headers: { ...SEC_HEADERS, 'content-type': p.endsWith('.png') ? 'image/png' : 'image/jpeg', 'content-length': String(g.blob.size), 'cache-control': 'private, max-age=3600' } });
  }
  return new Response('not found', { status: 404, headers: SEC_HEADERS });
}

export async function POST(request) {
  const url = new URL(request.url);
  const r = url.searchParams.get('r') || url.pathname.replace(/^\/(api\/web|admin)\/?/, '');
  if (!sameOrigin(request)) return new Response('bad origin', { status: 403, headers: SEC_HEADERS });
  const ctype = (request.headers.get('content-type') || '').split(';')[0].trim();
  if (ctype !== 'application/x-www-form-urlencoded') return new Response('bad form', { status: 415, headers: SEC_HEADERS });
  const raw = await request.text();
  if (raw.length > 4096) return new Response('too large', { status: 413, headers: SEC_HEADERS });
  const form = new URLSearchParams(raw);

  if (r === 'login') {
    const retry = await checkLogin(ipKey(ipAddress(request) || request.headers.get('x-forwarded-for') || ''));
    if (retry) return html(429, 'Wait', '<h1>Too many tries</h1><p>Wait an hour, then try again.</p>', { 'retry-after': String(retry) });
    const want = process.env.ADMIN_TOKEN;
    if (!want || !safeEqual((form.get('token') || '').trim(), want)) return loginPage('Wrong token.');
    return redirect('/admin', { 'set-cookie': `${COOKIE}=${sessionValue()}; Path=/; Max-Age=2592000; HttpOnly; Secure; SameSite=Strict` });
  }
  if (r === 'logout') {
    return redirect('/admin', { 'set-cookie': `${COOKIE}=; Path=/; Max-Age=0; HttpOnly; Secure; SameSite=Strict` });
  }
  if (!isAdminCookie(request)) return new Response('unauthorized', { status: 401, headers: SEC_HEADERS });
  if (r === 'handled') {
    const id = form.get('id') || '';
    if (!ID_RE.test(id)) return new Response('bad id', { status: 400, headers: SEC_HEADERS });
    const m = markerOf(id);
    if (form.get('on') === '1') {
      await put(m, JSON.stringify({ handled: true, at: new Date().toISOString() }), { access: 'private', addRandomSuffix: false, contentType: 'application/json', allowOverwrite: true });
    } else {
      const found = (await list({ prefix: m, limit: 1 })).blobs;
      if (found.length) await del(found.map((x) => x.url));
    }
    return redirect(`/admin/r?id=${encodeURIComponent(id)}`);
  }
  return new Response('not found', { status: 404, headers: SEC_HEADERS });
}
