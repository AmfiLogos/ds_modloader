#!/usr/bin/env python3
"""Production checks for the ds-crashrep receiver. Uses kind "test".

Each check guards a real break: broken upload, open storage, missing key check,
size / zip-bomb guard off, rate limit off, privacy page down.
Functional checks send X-DSCR-Admin so they do not use the IP budget;
the rate-limit check runs without it and the per-IP counters are reset at the end.

Usage: python3 receiver_test.py [--url URL]
"""
import argparse
import base64
import gzip
import json
import os
import ssl
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import struct
import zlib

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


def report(base, body, key=APP_KEY, ctype="application/gzip", admin=None, kind="test"):
    h = {
        "Content-Type": ctype,
        "X-DSCR-Key": key,
        "X-DSCR-Install": os.urandom(16).hex(),
        "X-DSCR-Kind": kind,
    }
    if admin:
        h["X-DSCR-Admin"] = admin
    return http("POST", base + "/api/report", body, h)


def png(w, h):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    raw = b"".join(b"\0" + b"\x80\x40\x20" * w for _ in range(h))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def bug_body(text, shot=None, shot_name="screenshot.png.b64"):
    t = ("DSCRASH 1\ninstall: " + "0" * 32 + "\nloader: test\ngame_build: 0\nplatform: test\nos: test\n"
         "cpu: test\nsent: 2026-10-20T00:00:00Z\nmods: none\nkind: bug\ncontact: receiver_test\n"
         "reason: player bug report\n")
    if text is not None:
        t += f"===== bug.txt ({len(text)} of {len(text)} bytes) =====\n{text}\n"
    if shot is not None:
        b = base64.b64encode(shot).decode()
        t += f"===== {shot_name} ({len(b)} of {len(b)} bytes, base64) =====\n{b}\n"
    t += "===== dsload.log (3 of 3 bytes, the end of the file) =====\nabc\n"
    return gzip.compress(t.encode())


def web(method, base, path, form=None, cookie=None):
    h = {"Origin": base}
    data = None
    if form is not None:
        data = urllib.parse.urlencode(form).encode()
        h["Content-Type"] = "application/x-www-form-urlencoded"
    if cookie:
        h["Cookie"] = cookie

    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *a, **k):
            return None
    op = urllib.request.build_opener(NoRedirect, urllib.request.HTTPSHandler(context=SSL_CTX))
    req = urllib.request.Request(base + path, data=data, method=method, headers=h)
    try:
        with op.open(req, timeout=60) as r:
            return r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


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

    # 9. Bug report with a screenshot: stored; then the private pages show it (cookie login).
    shot = png(64, 36)
    st, _, body = report(base, bug_body("receiver_test bug <b>text</b>", shot), admin=tok, kind="bug")
    bug_id = json.loads(body).get("id", "") if st == 201 else ""
    check("bug report with PNG screenshot accepted (201)", st == 201 and "-bug-" in bug_id, f"{st} {body[:80]!r}")
    st1, _, _ = report(base, bug_body("x", b"GIF89a" + b"\0" * 40), admin=tok, kind="bug")
    st2, _, _ = report(base, bug_body("x", png(5000, 10)), admin=tok, kind="bug")
    st3, _, _ = report(base, bug_body("x", shot, "screenshot.jpg.b64"), admin=tok, kind="bug")
    st4, _, _ = report(base, bug_body(None, shot), admin=tok, kind="bug")
    check("bad image / too wide / PNG named jpg / no text -> 400", (st1, st2, st3, st4) == (400,) * 4,
          f"{st1}/{st2}/{st3}/{st4}")

    st, _, b = web("GET", base, "/admin")
    check("/admin without cookie shows sign-in, no report data", st == 200 and b"Admin token" in b
          and b"receiver_test" not in b, str(st))
    st, _, _ = web("GET", base, "/admin/shot?id=" + urllib.parse.quote(bug_id))
    check("screenshot without cookie -> 401", st == 401, str(st))
    st, _, _ = web("POST", base, "/admin/login", {"token": "wrong"})
    check("wrong token -> 401", st == 401, str(st))
    st, h, _ = web("POST", base, "/admin/login", {"token": tok})
    sc = h.get("Set-Cookie") or ""
    cookie = sc.split(";")[0]
    flags = sc.lower()
    check("login sets HttpOnly Secure SameSite=Strict cookie without the token",
          st == 303 and all(f in flags for f in ("httponly", "secure", "samesite=strict")) and tok not in sc, str(st))
    if bug_id and cookie:
        q = urllib.parse.quote(bug_id, safe="")
        st, _, b = web("GET", base, "/admin?kind=bug", cookie=cookie)
        check("list shows the bug report", st == 200 and q.encode() in b.replace(b"&amp;", b"&"), str(st))
        st, _, b = web("GET", base, "/admin/r?id=" + q, cookie=cookie)
        check("detail shows escaped text, screenshot and log",
              st == 200 and b"receiver_test bug &lt;b&gt;text&lt;/b&gt;" in b and b"/admin/shot?id=" in b
              and b"dsload.log" in b, str(st))
        st, h, b = web("GET", base, "/admin/shot?id=" + q, cookie=cookie)
        check("screenshot served, bytes equal", st == 200 and b == shot, f"{st} {len(b)}")
        st, _, b = web("GET", base, "/admin/dl?id=" + q, cookie=cookie)
        check("gzip download works", st == 200 and gzip.decompress(b).startswith(b"DSCRASH 1"), str(st))
        st, _, _ = web("POST", base, "/admin/handled", {"id": bug_id, "on": "1"}, cookie=cookie)
        _, _, b = web("GET", base, "/admin/r?id=" + q, cookie=cookie)
        check("mark handled persists", st == 303 and b"Mark open again" in b, str(st))
        st, _, _ = web("POST", base, "/admin/handled", {"id": bug_id, "on": "1"}, cookie="x=1")
        check("mark handled without cookie -> 401", st == 401, str(st))

    # 10. Privacy page.
    st, h, b = http("GET", base + "/privacy")
    html = b.decode("utf-8", "replace")
    check("/privacy 200, no cookies, no external assets",
          st == 200 and "set-cookie" not in {k.lower() for k in h} and "src=" not in html
          and "<link" not in html and "90 days" in html and "screenshot" in html, str(st))

    if path:
        print("left behind:", path)
    if bug_id:
        print("left behind (marked handled):", bug_id)
    print(f"{sum(RESULTS)}/{len(RESULTS)} passed")
    sys.exit(0 if all(RESULTS) else 1)


if __name__ == "__main__":
    main()
