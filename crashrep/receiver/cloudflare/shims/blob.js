// @vercel/blob on R2: the handlers keep their Blob calls; objects live under <bucket>/ds-crashrep-reports/<pathname>.
import { env } from 'cloudflare:workers';

const PREFIX = 'ds-crashrep-reports/';
const STAMP = /\/(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})Z-/;

// Reports carry their upload time in the name; copied objects would otherwise show the copy date.
function uploadedAt(o) {
  const m = STAMP.exec(o.key);
  return m ? new Date(`${m[1]}-${m[2]}-${m[3]}T${m[4]}:${m[5]}:${m[6]}Z`) : o.uploaded;
}

// R2 has no public object URL. "url" is the admin download address (needs the token), and del() maps it back.
const urlOf = (pathname) => `${env.PUBLIC_URL}/api/admin/get?path=${encodeURIComponent(pathname)}`;
const pathOf = (u) => (u.startsWith('https://') ? new URL(u).searchParams.get('path') : u);

const asBlob = (o) => {
  const pathname = o.key.slice(PREFIX.length);
  return { pathname, url: urlOf(pathname), size: o.size, uploadedAt: uploadedAt(o), contentType: o.httpMetadata?.contentType };
};

export async function put(pathname, body, opts = {}) {
  await env.FILES.put(PREFIX + pathname, body, { httpMetadata: { contentType: opts.contentType } });
  return { pathname, url: urlOf(pathname) };
}

export async function get(pathname) {
  const o = await env.FILES.get(PREFIX + pathname);
  if (!o) return null;
  return { statusCode: 200, stream: o.body, blob: asBlob(o) };
}

export async function list({ prefix = '', cursor, limit = 1000, mode } = {}) {
  const r = await env.FILES.list({
    prefix: PREFIX + prefix, cursor, limit, include: ['httpMetadata'],
    ...(mode === 'folded' ? { delimiter: '/' } : {}),
  });
  return {
    blobs: r.objects.map(asBlob),
    folders: (r.delimitedPrefixes || []).map((p) => p.slice(PREFIX.length)),
    cursor: r.truncated ? r.cursor : undefined,
    hasMore: r.truncated,
  };
}

export async function del(urls) {
  const keys = (Array.isArray(urls) ? urls : [urls]).map((u) => PREFIX + pathOf(u));
  for (let i = 0; i < keys.length; i += 1000) await env.FILES.delete(keys.slice(i, i + 1000));
}
