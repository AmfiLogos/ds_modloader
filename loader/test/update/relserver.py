#!/usr/bin/env python3
"""relserver.py <dir> <port>: a local stand-in for the GitHub releases API (auto-update tests; DSL_UPDATE_API=http://127.0.0.1:<port>).

<dir>/<owner>/<repo>/<tag>/<asset files>  ->  GET /repos/<owner>/<repo>/releases  (newest tag first, by version)
                                            GET /dl/<owner>/<repo>/<tag>/<asset>  (the file)
Each asset gets "digest": "sha256:<hex>" like GitHub, except when <tag>/NO_DIGEST exists. A file <tag>/BAD_SHA makes the
digest wrong (a broken download). Every request is logged to <dir>/requests.log (method, path, status).

Signed updates (loader/src/dsl_sig.c): every release gets release-manifest.json + .sig (tools/release_sign/
make_manifest.py), signed by DSL_SIGN_TOOL (a dsl_sign binary; unset = built at the first listing) with DSL_SIGN_KEY
(default dev_keys/dev.key: the public dev key that only test builds trust, loader built with -DDSL_SIGN_DEV_KEY, e.g.
EXTRA_CFLAGS=-DDSL_SIGN_DEV_KEY zsh loader/build.sh), made again at each listing. Markers in <tag>/: NO_SIG = no manifest;
KEEP_MANIFEST = the test put its own manifest + .sig there (tampered, replayed, other key); BAD_SHA = the signed manifest
names a wrong sha256 (the file differs from what was signed)."""
import hashlib, http.server, importlib.util, json, os, re, subprocess, sys, tempfile

D, PORT = sys.argv[1], int(sys.argv[2])
HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, '../../..'))
SIGN_TOOL = os.environ.get('DSL_SIGN_TOOL', '')
SIGN_KEY = os.environ.get('DSL_SIGN_KEY', os.path.join(TOP, 'dev_keys/dev.key'))
MARKERS = ('NO_DIGEST', 'BAD_SHA', 'NO_SIG', 'KEEP_MANIFEST')
MAN = 'release-manifest.json'
_spec = importlib.util.spec_from_file_location('make_manifest', os.path.join(TOP, 'tools/release_sign/make_manifest.py'))
make_manifest = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(make_manifest)


def sign_tool():
    global SIGN_TOOL
    if not SIGN_TOOL:
        t = os.path.join(tempfile.mkdtemp(prefix='relserver_sign.'), 'dsl_sign'); src = os.path.join(TOP, 'loader/src')
        subprocess.run(['clang', '-O2', '-o', t, os.path.join(TOP, 'tools/release_sign/dsl_sign.c'),
                        os.path.join(src, 'dsl_sig.c'), os.path.join(src, 'dsl_sig_mc.c')], check=True)
        SIGN_TOOL = t
    return SIGN_TOOL


def sign_release(repo, tag, td):
    """(re)make <td>/release-manifest.json + .sig with the test key, unless a marker says otherwise"""
    if os.path.exists(os.path.join(td, 'KEEP_MANIFEST')):
        return
    for f in (MAN, MAN + '.sig'):
        if os.path.exists(os.path.join(td, f)):
            os.replace(os.path.join(td, f), os.path.join(td, '.' + f + '.old'))
    if os.path.exists(os.path.join(td, 'NO_SIG')):
        return
    files = [os.path.join(td, a) for a in sorted(os.listdir(td)) if a not in MARKERS and not a.startswith('.')]
    try:
        text = make_manifest.manifest(repo, tag, files)
    except ValueError as e:
        print('relserver: not signed:', e, file=sys.stderr); return
    if os.path.exists(os.path.join(td, 'BAD_SHA')):
        text = re.sub(r'"sha256": "[0-9a-f]{64}"', '"sha256": "' + '0' * 64 + '"', text)
    mp = os.path.join(td, MAN)
    open(mp, 'w').write(text)
    sig = subprocess.run([sign_tool(), 'sign', mp], stdin=open(SIGN_KEY), capture_output=True, text=True, check=True).stdout
    open(mp + '.sig', 'w').write(sig)


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
                sign_release(f'{m.group(1)}/{m.group(2)}', tag, td)
                assets = []
                for a in sorted(os.listdir(td)):
                    if a in MARKERS or a.startswith('.'):
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
