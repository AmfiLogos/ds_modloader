# Crash-report receiver (ds-crashrep)

A small private receiver for "Dungeon Settlers Modded" crash reports. Plain Vercel functions (Node runtime), no framework.

Production: https://ds-crashrep.vercel.app

## Endpoints

| Method | Path | Notes |
| --- | --- | --- |
| POST | `/api/report` | Upload one report. Answers `201 {"id": "<path>"}`. |
| GET | `/privacy` | Privacy page (same text as `PRIVACY.md`). |
| GET | `/api/admin/list?prefix=&cursor=` | Admin. JSON list: `path`, `size`, `uploadedAt`, `url`. |
| GET | `/api/admin/get?path=reports/...` | Admin. Streams the stored gzip. |
| POST | `/api/admin/reset-limits` | Admin. Clears the per-IP counters of the caller's IP (for tests). |
| GET | `/api/cron/expire` | Daily cron. Deletes reports older than 90 days. |

Admin calls need `Authorization: Bearer <ADMIN_TOKEN>`. The cron needs `Authorization: Bearer <CRON_SECRET>` (Vercel sends it).

### POST /api/report

- `Content-Type: application/gzip` (else 415).
- `X-DSCR-Key`: the public app key (else 401).
- `X-DSCR-Install`: 32 lower-case hex chars (else 400).
- `X-DSCR-Kind`: `crash`, `freeze`, `desync`, `join`, `blank` (multiplayer view showed nothing) or `test` (else 400).
- Body: gzip of UTF-8 text. First line must be `DSCRASH 1` (else 400).
- Stored at `reports/YYYY/MM/DD/<time>-<kind>-<install8>-<random>.txt.gz` (original gzip bytes, private Blob store). Time is ISO 8601 basic format, for example `20261018T001137Z`.
- `X-DSCR-Admin: <ADMIN_TOKEN>` skips the rate limits (tests only).

## Limits

- Compressed body: 4 MB (413).
- Unpacked size: 32 MB, checked while streaming (zip-bomb guard, 413).
- Per client IP: 6 per hour, 20 per day. Every request with a valid key counts.
- Per install id: 5 per day. Global: 2000 reports per day. These count stored reports.
- Over a limit: 429 with `Retry-After` (seconds to the end of the window).
- The IP is never stored or logged. Only an HMAC-SHA256 of it (salt `IP_SALT`) is used as a counter key.

Counter store: Vercel Runtime Cache (`getCache` from `@vercel/functions`), because it is part of the plan, needs no Marketplace install and no extra Blob operations; counters are fixed windows with a TTL, so they clean themselves.

## Storage

Private Vercel Blob store `ds-crashrep-reports` (connected to the project, gives `BLOB_READ_WRITE_TOKEN`). Blob URLs answer 403 without the token.

## Env vars (names only)

- `BLOB_READ_WRITE_TOKEN` (set by the store connection)
- `ADMIN_TOKEN` (sensitive; copy in macOS Keychain, service `ds-crashrep-admin`, account `ds-crashrep`)
- `CRON_SECRET` (sensitive; copy in Keychain, service `ds-crashrep-cron`)
- `IP_SALT` (sensitive)

## Cron

`vercel.json` runs `/api/cron/expire` once per day at 04:17 UTC. It walks `reports/YYYY/MM/DD/` folders and deletes days older than 90 days.

## Deploy

```sh
cd crashrep/receiver
npm install
vercel deploy --prod
```

Production domain is public. Previews and per-deployment URLs stay behind Vercel deployment protection.

## Read reports on the Mac

```sh
python3 crashrep/tools/dscr_read.py list [--prefix reports/2026/10/]
python3 crashrep/tools/dscr_read.py get reports/2026/10/18/<file>.txt.gz [--out DIR]
python3 crashrep/tools/dscr_read.py latest 5
```

The token comes from the Keychain. Base URL: `--url`, or env `DSCR_URL`, or the production URL.

## Tests

`python3 crashrep/receiver/test/receiver_test.py` runs against production with kind `test`. It resets the per-IP counters at the end. Uploaded test reports expire with the cron.
