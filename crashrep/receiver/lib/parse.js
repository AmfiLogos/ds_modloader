// Parse the plain text of a DSCRASH 1 report: header fields + "===== name (...) =====" sections.

const FIELDS = ['install', 'loader', 'game_build', 'platform', 'os', 'cpu', 'sent', 'mods', 'kind', 'reason', 'contact'];
const HEAD_RE = /^===== (\S+)(?: (.*))? =====\r?$/;

export function parseReport(text) {
  const lines = text.split('\n');
  const fields = {};
  const sections = [];
  let cur = null;
  for (const raw of lines) {
    const line = raw.replace(/\r$/, '');
    const m = HEAD_RE.exec(line);
    if (m) {
      cur = { name: m[1], info: m[2] || '', lines: [] };
      sections.push(cur);
      continue;
    }
    if (cur) {
      cur.lines.push(line);
      continue;
    }
    const i = line.indexOf(': ');
    const k = i > 0 ? line.slice(0, i) : line.endsWith(':') ? line.slice(0, -1) : '';
    if (FIELDS.includes(k) && !(k in fields)) fields[k] = i > 0 ? line.slice(i + 2) : '';
    else if (k && !(k in fields) && /^[a-z_]{2,32}$/.test(k)) fields[k] = i > 0 ? line.slice(i + 2) : '';
  }
  for (const s of sections) {
    // The writer adds one "\n" after each file; drop that trailing empty line.
    if (s.lines.length && s.lines[s.lines.length - 1] === '') s.lines.pop();
    s.body = s.lines.join('\n');
    delete s.lines;
  }
  return { fields, sections };
}

export const SHOT_NAMES = { 'screenshot.png.b64': 'png', 'screenshot.jpg.b64': 'jpg' };
export const MAX_SHOT = 3 * 1024 * 1024;
export const MAX_W = 3840;
export const MAX_H = 2160;
export const MAX_TEXT = 8000;

// Width/height of a PNG or JPEG buffer, or null.
export function imageInfo(buf) {
  if (buf.length >= 24 && buf.readUInt32BE(0) === 0x89504e47 && buf.readUInt32BE(4) === 0x0d0a1a0a
      && buf.toString('latin1', 12, 16) === 'IHDR') {
    return { type: 'png', w: buf.readUInt32BE(16), h: buf.readUInt32BE(20) };
  }
  if (buf.length >= 4 && buf[0] === 0xff && buf[1] === 0xd8 && buf[2] === 0xff) {
    let p = 2;
    while (p + 4 <= buf.length) {
      if (buf[p] !== 0xff) return null;
      const mk = buf[p + 1];
      if (mk === 0xff) { p++; continue; }
      if (mk === 0xd8 || mk === 0x01 || (mk >= 0xd0 && mk <= 0xd7)) { p += 2; continue; }
      if (mk === 0xd9 || mk === 0xda) return null; // no frame header before the scan
      const len = buf.readUInt16BE(p + 2);
      if (len < 2) return null;
      const sof = mk >= 0xc0 && mk <= 0xcf && mk !== 0xc4 && mk !== 0xc8 && mk !== 0xcc;
      if (sof) {
        if (p + 9 > buf.length) return null;
        return { type: 'jpg', h: buf.readUInt16BE(p + 5), w: buf.readUInt16BE(p + 7) };
      }
      p += 2 + len;
    }
  }
  return null;
}

// Checks a "bug" report. Returns { error } or { text, shot: {buf, ext} | null }.
export function checkBug(parsed) {
  const t = parsed.sections.find((s) => s.name === 'bug.txt');
  if (!t || !t.body.trim()) return { error: 'bug text missing' };
  if (t.body.length > MAX_TEXT) return { error: 'bug text too long' };
  const shots = parsed.sections.filter((s) => s.name in SHOT_NAMES);
  if (shots.length > 1) return { error: 'bad screenshot' };
  let shot = null;
  if (shots.length) {
    const b64 = shots[0].body.replace(/\s+/g, '');
    if (!/^[A-Za-z0-9+/]+={0,2}$/.test(b64) || b64.length > Math.ceil(MAX_SHOT / 3) * 4) return { error: 'bad screenshot' };
    const buf = Buffer.from(b64, 'base64');
    const info = imageInfo(buf);
    if (!info || info.type !== SHOT_NAMES[shots[0].name] || buf.length > MAX_SHOT
        || info.w < 1 || info.h < 1 || info.w > MAX_W || info.h > MAX_H) return { error: 'bad screenshot' };
    shot = { buf, ext: info.type, w: info.w, h: info.h };
  }
  return { text: t.body, shot };
}

// Small summary kept next to each report (used by the admin list).
export function summary(parsed, kind, extra = {}) {
  const f = parsed.fields;
  const cut = (v, n) => (typeof v === 'string' ? v.slice(0, n) : '');
  const bug = parsed.sections.find((s) => s.name === 'bug.txt');
  return {
    kind,
    loader: cut(f.loader, 64),
    game_build: cut(f.game_build, 64),
    platform: cut(f.platform, 64),
    os: cut(f.os, 160),
    cpu: cut(f.cpu, 160),
    sent: cut(f.sent, 40),
    mods: cut(f.mods, 2000),
    reason: cut(f.reason, 300),
    contact: cut(f.contact, 200),
    text: bug ? bug.body.slice(0, 600) : '',
    sections: parsed.sections.map((s) => s.name).slice(0, 40),
    ...extra,
  };
}
