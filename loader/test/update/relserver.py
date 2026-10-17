#!/usr/bin/env python3
"""relserver.py <dir> <port>: a local stand-in for the GitHub releases API (auto-update tests; DSL_UPDATE_API=http://127.0.0.1:<port>).

<dir>/<owner>/<repo>/<tag>/<asset files>  ->  GET /repos/<owner>/<repo>/releases  (newest tag first, by version)
                                            GET /dl/<owner>/<repo>/<tag>/<asset>  (the file)
Each asset gets "digest": "sha256:<hex>" like GitHub, except when <tag>/NO_DIGEST exists. A file <tag>/BAD_SHA makes the
digest wrong (a broken download). Every request is logged to <dir>/requests.log (method, path, status)."""
import hashlib, http.server, json, os, re, sys

D, PORT = sys.argv[1], int(sys.argv[2])


def vkey(t):
    return [int(x) for x in re.findall(r'\d+', t)]


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype='application/json'):
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)
        with open(os.path.join(D, 'requests.log'), 'a') as f:
            f.write(f'GET {self.path} {code}\n')

    def do_GET(self):
        p = self.path.split('?')[0]
        m = re.fullmatch(r'/repos/([^/]+)/([^/]+)/releases', p)
        if m:
            rd = os.path.join(D, m.group(1), m.group(2))
            if not os.path.isdir(rd):
                return self.send(404, b'{"message":"Not Found"}')
            rels = []
            for tag in sorted(os.listdir(rd), key=vkey, reverse=True):
                td = os.path.join(rd, tag)
                if not os.path.isdir(td):
                    continue
                assets = []
                for a in sorted(os.listdir(td)):
                    if a in ('NO_DIGEST', 'BAD_SHA') or a.startswith('.'):
                        continue
                    ap = os.path.join(td, a); h = hashlib.sha256(open(ap, 'rb').read()).hexdigest()
                    if os.path.exists(os.path.join(td, 'BAD_SHA')):
                        h = '0' * 64
                    e = {'name': a, 'size': os.path.getsize(ap),
                         'browser_download_url': f'http://127.0.0.1:{PORT}/dl/{m.group(1)}/{m.group(2)}/{tag}/{a}'}
                    if not os.path.exists(os.path.join(td, 'NO_DIGEST')):
                        e['digest'] = 'sha256:' + h
                    assets.append(e)
                rels.append({'tag_name': tag, 'draft': False, 'prerelease': False, 'assets': assets})
            return self.send(200, json.dumps(rels).encode())
        m = re.fullmatch(r'/dl/(.+)', p)
        if m:
            fp = os.path.join(D, m.group(1))
            if os.path.isfile(fp):
                return self.send(200, open(fp, 'rb').read(), 'application/octet-stream')
        self.send(404, b'{"message":"Not Found"}')


http.server.ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
