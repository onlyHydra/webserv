# echo_upstream.py
import sys, time
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9000

class Handler(BaseHTTPRequestHandler):
    def _read_body(self):
        length = int(self.headers.get('Content-Length', '0') or '0')
        return self.rfile.read(length) if length > 0 else b''

    def _send(self, code, body_bytes=b'', extra_headers=None):
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body_bytes)))
        if extra_headers:
            for k, v in extra_headers.items():
                self.send_header(k, v)
        self.end_headers()
        if body_bytes:
            self.wfile.write(body_bytes)

    def do_GET(self):
        body = (f'{{"method":"GET","path":"{self.path}","headers":{dict(self.headers)}}}').encode()
        self._send(200, body)

    def do_POST(self):
        data = self._read_body()
        body = (f'{{"method":"POST","path":"{self.path}","len":{len(data)},"headers":{dict(self.headers)}}}').encode()
        self._send(200, body)

    def do_DELETE(self):
        body = (f'{{"method":"DELETE","path":"{self.path}"}}').encode()
        self._send(200, body)

    # slow endpoint for timeout test
    def do_PUT(self):
        time.sleep(15)  # simulate a slow upstream read/response
        self._send(200, b'{"ok":true}')

HTTPServer(("", PORT), Handler).serve_forever()
