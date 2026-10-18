#!/usr/bin/env python3
"""Production checks for the ds-crashrep receiver. Uses kind "test".

Each check guards a real break: broken upload, open storage, missing key check,
size / zip-bomb guard off, rate limit off, privacy page down.
Functional checks send X-DSCR-Admin so they do not use the IP budget;
the rate-limit check runs without it and the per-IP counters are reset at the end.

Usage: python3 receiver_test.py [--url URL]
"""
import argparse
import gzip
import json
import os
import ssl
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
READER = os.path.join(HERE, "..", "..", "tools", "dscr_read.py")
APP_KEY = "dscr1-621ed5ae365845cd8ed0034a"
RESULTS = []


def _ssl_context():
    # python.org builds on macOS ship without CA certs; fall back to the system bundle.
    ctx = ssl.create_default_context()
    if not ctx.get_ca_certs() and os.path.exists("/etc/ssl/cert.pem"):
        ctx = ssl.create_default_context(cafile="/etc/ssl/cert.pem")
    return ctx


SSL_CTX = _ssl_context()


def admin_token():
    return subprocess.run(
        ["security", "find-generic-password", "-s", "ds-crashrep-admin", "-a", "ds-crashrep", "-w"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()


def http(method, url, body=None, headers=None):
    req = urllib.request.Request(url, data=body, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=60, context=SSL_CTX) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def report(base, body, key=APP_KEY, ctype="application/gzip", admin=None):
    h = {
        "Content-Type": ctype,
        "X-DSCR-Key": key,
        "X-DSCR-Install": os.urandom(16).hex(),
        "X-DSCR-Kind": "test",
    }
    if admin:
        h["X-DSCR-Admin"] = admin
    return http("POST", base + "/api/report", body, h)


def check(name, ok, detail=""):
    RESULTS.append(ok)
    print(("PASS " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default=os.environ.get("DSCR_URL", "https://ds-crashrep.vercel.app"))
    base = ap.parse_args().url.rstrip("/")
    tok = admin_token()
    auth = {"Authorization": "Bearer " + tok}

    # 1. Upload, then read back with dscr_read.py and compare bytes.
    gz = gzip.compress(b"DSCRASH 1\nreceiver_test upload\n" + os.urandom(64).hex().encode())
    st, _, body = report(base, gz, admin=tok)
    path = json.loads(body).get("id", "") if st == 201 else ""
    check("upload accepted (201)", st == 201 and path.startswith("reports/"), f"{st} {path}")
    if path:
        with tempfile.TemporaryDirectory() as d:
            env = dict(os.environ, DSCR_URL=base)
            r = subprocess.run([sys.executable, READER, "get", path, "--out", d],
                               capture_output=True, text=True, env=env)
            got = open(os.path.join(d, os.path.basename(path)), "rb").read() if r.returncode == 0 else b""
            check("read back via dscr_read.py, bytes equal", got == gz, f"rc={r.returncode}")

        # 2. Stored blob must NOT be readable without the token.
        st, _, lst = http("GET", base + "/api/admin/list?prefix=" + os.path.dirname(path) + "/", headers=auth)
        urls = [b["url"] for b in json.loads(lst)["blobs"] if b["path"] == path] if st == 200 else []
        if urls:
            st2, _, b2 = http("GET", urls[0])
            check("blob URL denied without token", st2 in (401, 403) and b2 != gz, f"{st2}")
        else:
            check("blob URL denied without token", False, "blob not listed")

    # 3. Admin endpoints need the token.
    st, _, _ = http("GET", base + "/api/admin/list?prefix=reports/")
    st2, _, _ = http("GET", base + "/api/admin/get?path=" + (path or "reports/x"),
                     headers={"Authorization": "Bearer wrong"})
    check("admin list/get refuse without valid token (401)", st == 401 and st2 == 401, f"{st}/{st2}")

    # 4. Wrong app key.
    st, _, _ = report(base, gzip.compress(b"DSCRASH 1\n"), key="dscr1-wrong")
    check("wrong key -> 401", st == 401, str(st))

    # 5. Oversize (> 4 MB compressed, under the platform 4.5 MB cap).
    st, _, _ = report(base, os.urandom(4 * 1024 * 1024 + 200 * 1024), admin=tok)
    check("oversize -> 413", st == 413, str(st))

    # 6. Not gzip.
    st1, _, _ = report(base, b"DSCRASH 1\nplain", ctype="text/plain", admin=tok)
    st2, _, _ = report(base, b"DSCRASH 1\nplain text, gzip label", admin=tok)
    check("wrong content type -> 415, non-gzip body -> 400", st1 == 415 and st2 == 400, f"{st1}/{st2}")

    # 7. Zip bomb: small gzip that inflates past 32 MB.
    bomb = gzip.compress(b"DSCRASH 1\n" + b"\0" * (40 * 1024 * 1024), 9)
    st, _, _ = report(base, bomb, admin=tok)
    check(f"zip bomb ({len(bomb)} bytes -> 40 MB) -> 413", st == 413, str(st))

    # 8. Rate limit: per-IP hourly cap of 6. Bad-header bodies count but store nothing.
    http("POST", base + "/api/admin/reset-limits", b"", auth)
    codes, retry = [], None
    for _ in range(7):
        st, h, _ = report(base, gzip.compress(b"NOT A REPORT\n"))
        codes.append(st)
        retry = h.get("Retry-After") or h.get("retry-after") or retry
    check("7th request in an hour -> 429 with Retry-After",
          codes[:6] == [400] * 6 and codes[6] == 429 and retry is not None, f"{codes} retry={retry}")
    st, _, rb = http("POST", base + "/api/admin/reset-limits", b"", auth)
    check("rate counters reset after test", st == 200, rb.decode()[:80])

    # 9. Privacy page.
    st, h, b = http("GET", base + "/privacy")
    html = b.decode("utf-8", "replace")
    check("/privacy 200, no cookies, no external assets",
          st == 200 and "set-cookie" not in {k.lower() for k in h} and "src=" not in html
          and "<link" not in html and "90 days" in html, str(st))

    if path:
        print("left behind:", path)
    print(f"{sum(RESULTS)}/{len(RESULTS)} passed")
    sys.exit(0 if all(RESULTS) else 1)


if __name__ == "__main__":
    main()
