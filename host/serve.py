"""Static server for the websim pages.

The simulator's audio worklet uses SharedArrayBuffer, which browsers only allow
on cross-origin isolated pages, so every response carries COOP/COEP headers.
Usage: serve.py <directory> <port>   (binds to localhost only)
"""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".wasm": "application/wasm", ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


directory, port = sys.argv[1], int(sys.argv[2])
server = http.server.ThreadingHTTPServer(
    ("127.0.0.1", port), functools.partial(Handler, directory=directory))
print(f"serving {directory} on http://localhost:{port}/", flush=True)
server.serve_forever()
