// Discord ping for new reports. Webhook URL in env DISCORD_WEBHOOK_URL; missing = no ping.
// DISCORD_PING_KINDS: comma list of kinds that ping (default "bug").

const BASE = process.env.PUBLIC_URL || 'https://crashrep.dungeonsettlers.wiki';

export function pingText(kind, meta, path) {
  const text = (meta.text || meta.reason || '').replace(/\s+/g, ' ').trim();
  const short = text.length > 120 ? text.slice(0, 119) + '…' : text;
  const os = (meta.platform || meta.os || '?').slice(0, 40);
  const what = kind === 'bug' ? 'bug report' : `${kind} report`;
  const link = `${BASE}/admin/r?id=${encodeURIComponent(path)}`;
  return `New ${what} v${meta.loader || '?'} ${os}: ${short}\n${link}`.slice(0, 1900);
}

export async function pingDiscord(kind, meta, path) {
  const url = process.env.DISCORD_WEBHOOK_URL;
  if (!url || !/^https:\/\/(discord\.com|discordapp\.com|ptb\.discord\.com|canary\.discord\.com)\/api\/webhooks\//.test(url)) return;
  const kinds = (process.env.DISCORD_PING_KINDS || 'bug').split(',').map((k) => k.trim());
  if (!kinds.includes(kind)) return;
  const ctl = new AbortController();
  const t = setTimeout(() => ctl.abort(), 8000);
  try {
    await fetch(url, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      // allowed_mentions empty: player text can never ping @everyone or a role.
      body: JSON.stringify({ content: pingText(kind, meta, path), allowed_mentions: { parse: [] } }),
      signal: ctl.signal,
    });
  } finally {
    clearTimeout(t);
  }
}
