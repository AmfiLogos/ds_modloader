#!/usr/bin/env python3
"""Read crash reports from the ds-crashrep receiver (admin only).

  dscr_read.py list [--prefix P]
  dscr_read.py get <path> [--out DIR]
  dscr_read.py latest [N] [--out DIR]

Token: macOS Keychain (service ds-crashrep-admin, account ds-crashrep).
Base URL: --url, else env DSCR_URL, else https://crashrep.dungeonsettlers.wiki
"""
import argparse
import gzip
import json
import os
import ssl
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_URL = "https://crashrep.dungeonsettlers.wiki"


def _ssl_context():
    # python.org builds on macOS ship without CA certs; fall back to the system bundle.
    ctx = ssl.create_default_context()
    if not ctx.get_ca_certs() and os.path.exists("/etc/ssl/cert.pem"):
        ctx = ssl.create_default_context(cafile="/etc/ssl/cert.pem")
    return ctx


SSL_CTX = _ssl_context()


def token():
    try:
        out = subprocess.run(
            ["security", "find-generic-password", "-s", "ds-crashrep-admin", "-a", "ds-crashrep", "-w"],
            check=True, capture_output=True, text=True,
        )
    except subprocess.CalledProcessError:
        sys.exit("admin token not found in Keychain (service ds-crashrep-admin)")
    return out.stdout.strip()


def request(base, path, params):
    url = base.rstrip("/") + path + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + token()})
    try:
        return urllib.request.urlopen(req, timeout=60, context=SSL_CTX)
    except urllib.error.HTTPError as e:
        sys.exit(f"HTTP {e.code} for {path}: {e.read()[:200].decode('utf-8', 'replace')}")


def list_all(base, prefix):
    blobs, cursor = [], None
    while True:
        params = {"prefix": prefix}
        if cursor:
            params["cursor"] = cursor
        data = json.load(request(base, "/api/admin/list", params))
        blobs.extend(data["blobs"])
        if not data.get("hasMore"):
            return blobs
        cursor = data["cursor"]


def get(base, path, out_dir):
    raw = request(base, "/api/admin/get", {"path": path}).read()
    os.makedirs(out_dir, exist_ok=True)
    name = os.path.basename(path)
    gz_path = os.path.join(out_dir, name)
    with open(gz_path, "wb") as f:
        f.write(raw)
    txt_path = gz_path[:-3] if gz_path.endswith(".gz") else gz_path + ".txt"
    with open(txt_path, "wb") as f:
        f.write(gzip.decompress(raw))
    print(txt_path)
    return gz_path, txt_path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=os.environ.get("DSCR_URL", DEFAULT_URL))
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("--prefix", default="reports/")
    p = sub.add_parser("get")
    p.add_argument("path")
    p.add_argument("--out", default="./crash_reports")
    p = sub.add_parser("latest")
    p.add_argument("n", nargs="?", type=int, default=1)
    p.add_argument("--out", default="./crash_reports")
    a = ap.parse_args()

    if a.cmd == "list":
        for b in list_all(a.url, a.prefix):
            print(f"{b['uploadedAt']}  {b['size']:>9}  {b['path']}")
    elif a.cmd == "get":
        get(a.url, a.path, a.out)
    elif a.cmd == "latest":
        blobs = sorted((b for b in list_all(a.url, "reports/") if b["path"].endswith(".txt.gz")), key=lambda b: b["uploadedAt"], reverse=True)
        for b in blobs[: a.n]:
            get(a.url, b["path"], a.out)


if __name__ == "__main__":
    main()
