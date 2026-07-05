#!/usr/bin/env python3
"""Dev server for the Gamma9000 web drum machine.

Serves the repo root (so /docs/amy.js + amy.wasm are reachable) with the
COOP/COEP headers the threaded wasm build requires. The app lives at
/docs/gamma9001/.
"""
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PORT = 8909


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=REPO_ROOT, **kwargs)

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def do_GET(self):
        if self.path in ('/', '/index.html'):
            self.send_response(302)
            self.send_header('Location', '/docs/gamma9001/index.html')
            self.end_headers()
            return
        super().do_GET()


if __name__ == '__main__':
    print(f"serving {REPO_ROOT} on http://localhost:{PORT}/")
    ThreadingHTTPServer(('127.0.0.1', PORT), Handler).serve_forever()
