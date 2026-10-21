# Crash-report receiver (ds-crashrep)

A small private receiver for "Dungeon Settlers Modded" crash reports and player bug reports. Plain Web-style handlers (`api/*.js`), run as one Cloudflare Worker (`receiver/cloudflare/`), no framework.

Production: https://crashrep.dungeonsettlers.wiki (Cloudflare Worker `ds-crashrep`, since 2026-10-21).

Old address https://ds-crashrep.vercel.app (compiled into loaders v0.1.15 to v0.1.16.1): `crashrep/vercel-proxy/` forwards `POST /api/report` to the new address (with the player's IP and `PROXY_SECRET`) and answers 308 to the new address for every other path.

## Endpoints

| Method | Path | Notes |
| --- | --- | --- |
| POST | `/api/report` | Upload one report. Answers `201 {"id": "<path>"}`. |
| GET | `/privacy` | Privacy page (same text as `PRIVACY.md`). |
| GET | `/admin` | Private web page: all reports, newest first, filters kind / kit version / state. Cookie login. |
| GET | `/admin/r?id=reports/...` | One report: text, screenshot, details, logs (collapsed), download, "mark handled". |
| GET | `/api/admin/list?prefix=&cursor=` | Admin. JSON list: `path`, `size`, `uploadedAt`, `url`. |
| GET | `/api/admin/get?path=reports/...` | Admin. Streams the stored gzip. |
| POST | `/api/admin/reset-limits` | Admin. Clears the per-IP counters of the caller's IP (for tests). |
| GET | `/api/cron/expire` | Daily cron. Deletes reports older than 90 days. |

Admin calls need `Authorization: Bearer <ADMIN_TOKEN>`. The cron needs `Authorization: Bearer <CRON_SECRET>` (the Worker's Cron Trigger adds it).

### POST /api/report

- `Content-Type: application/gzip` (else 415).
- `X-DSCR-Key`: the public app key (else 401).
- `X-DSCR-Install`: 32 lower-case hex chars (else 400).
- `X-DSCR-Kind`: `bug` (player bug report, see below), `crash`, `freeze`, `desync`, `join`, `blank` (multiplayer view showed nothing) or `test` (else 400).
- Body: gzip of UTF-8 text. First line must be `DSCRASH 1` (else 400).
- Stored at `reports/YYYY/MM/DD/<time>-<kind>-<install8>-<random>.txt.gz` (original gzip bytes, private R2 bucket `wiki-files`, under `ds-crashrep-reports/`). Time is ISO 8601 basic format, for example `20261018T001137Z`. Next to it: `<same>.meta.json` (parsed header + first 600 chars of the bug text, for the list page) and, for a bug with a screenshot, `<same>.shot.png` or `.shot.jpg` (decoded image).
- `X-DSCR-Admin: <ADMIN_TOKEN>` skips the rate limits (tests only).

### Kind "bug"

Same request as above, with `X-DSCR-Kind: bug`. The gzip text body has the usual header lines (`install`, `loader` = kit version, `game_build`, `platform`, `os`, `cpu`, `sent`, `mods`), then these meta lines:

```
kind: bug
contact: <optional, one line, max 200 chars; do not scrub this line>
reason: player bug report
```

Then sections with the usual `===== <name> (...) =====` header line (the name is the first word):

- `===== bug.txt (N of N bytes) =====`: the player text. Required, not empty, max 8000 characters. Else 400 `bug text missing` / `bug text too long`.
- `===== screenshot.jpg.b64 (N of N bytes, base64) =====` or `screenshot.png.b64`: optional, one only. Standard base64, line breaks allowed. Decoded: real PNG or JPEG matching the name, max 3 MB, max 3840 x 2160. Else 400 `bad screenshot`. The client must not scrub or tail-cut this section; when the report is too big it drops the screenshot first.
- The usual scrubbed log sections.

Each stored bug report pings Discord (see below), except uploads with `X-DSCR-Admin` (tests).

## Admin web page

`https://crashrep.dungeonsettlers.wiki/admin`. Type the `ADMIN_TOKEN` once (Keychain `ds-crashrep-admin`). The cookie `__Host-dscr_admin` is HttpOnly, Secure, SameSite=Strict, 30 days, and holds an HMAC of the token, never the token. Changing `ADMIN_TOKEN` signs everyone out. Login tries: 10 per hour per IP. Every POST checks the Origin header. "Mark handled" writes `state/handled/<report>.json`; the cron deletes markers older than 90 days.

## Discord ping

Env `DISCORD_WEBHOOK_URL` (a Discord webhook URL; missing = no ping). Message: `New bug report v<kit> <platform>: <first 120 chars>` + link to `/admin/r?id=...`. Mentions are disabled. `DISCORD_PING_KINDS` (optional, comma list, default `bug`) adds kinds, for example `bug,crash,freeze`.

## Limits

- Compressed body: 4 MB (413).
- Unpacked size: 32 MB, checked while streaming (zip-bomb guard, 413).
- Per client IP: 6 per hour, 20 per day. Every request with a valid key counts.
- Per install id: 5 per day. Global: 2000 reports per day. These count stored reports.
- Over a limit: 429 with `Retry-After` (seconds to the end of the window).
- The IP is never stored or logged. Only an HMAC-SHA256 of it (salt `IP_SALT`) is used as a counter key.

Counter store: R2 objects under `wiki-files/ds-crashrep-limits/` (`cloudflare/shims/functions.js` gives `getCache` the Vercel shape; the Workers Cache API did not keep counters on this zone). Fixed windows with an expiry time; the nightly cron deletes counters older than 2 days. Behind the old-address proxy the IP comes from `x-dscr-client-ip`, trusted only with `x-dscr-proxy` = `PROXY_SECRET`.

## Storage

Private R2 bucket `wiki-files`, keys `ds-crashrep-reports/<path>` (binding `FILES`). `cloudflare/shims/blob.js` gives the handlers the `@vercel/blob` calls they use (put, get, list, del). R2 has no public object URL; a listed `url` is the admin download address (needs the token). The old Vercel Blob store `ds-crashrep-reports` is kept, read-only, until the Vercel close-down.

## Env vars (names only)

- `ADMIN_TOKEN` (sensitive; copy in macOS Keychain, service `ds-crashrep-admin`, account `ds-crashrep`)
- `CRON_SECRET` (sensitive; copy in Keychain, service `ds-crashrep-cron`)
- `IP_SALT` (Keychain `ds-crashrep-ipsalt`; new on Cloudflare, it only keys the rate counters)
- `PROXY_SECRET` (Keychain `ds-crashrep-proxy`; also set on the Vercel project for the old-address proxy)
- `DISCORD_WEBHOOK_URL` (optional; Keychain `discord-webhook-bugreports`)
- `PUBLIC_URL` (plain var in `wrangler.jsonc`: base of the Discord links)
- `DISCORD_PING_KINDS` (optional)

## Cron

The Worker's Cron Trigger (`17 4 * * *`, `cloudflare/wrangler.jsonc`) runs the `/api/cron/expire` handler once per day at 04:17 UTC. It walks `reports/YYYY/MM/DD/` folders and deletes days older than 90 days (reports, `.meta.json`, screenshots), then handled markers older than 90 days.

## Deploy

```sh
cd crashrep/receiver/cloudflare
wrangler deploy --dry-run --outdir .wrangler/dist && cf-namescan . .wrangler/dist
wrangler deploy --message "<what changed>"
python3 ../test/receiver_test.py --url https://crashrep.dungeonsettlers.wiki
```

Secrets: `security find-generic-password -s <item> -w | wrangler secret put <NAME>`. Back: `wrangler rollback`.
`receiver/vercel.json` is the old Vercel setup, kept for reference. The old-address proxy: `cd crashrep/vercel-proxy && vercel deploy --prod` (project `ds-crashrep`).

## Read reports on the Mac

```sh
python3 crashrep/tools/dscr_read.py list [--prefix reports/2026/10/]
python3 crashrep/tools/dscr_read.py get reports/2026/10/18/<file>.txt.gz [--out DIR]
python3 crashrep/tools/dscr_read.py latest 5
```

The token comes from the Keychain. Base URL: `--url`, or env `DSCR_URL`, or the production URL.

## Tests

`python3 crashrep/receiver/test/receiver_test.py` runs against production with kind `test`. It resets the per-IP counters at the end. Uploaded test reports expire with the cron.
