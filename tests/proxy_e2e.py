#!/usr/bin/env python3
# proxy_e2e.py — starts upstream on :9000 and runs proxy tests via webserv on :8080

import sys, os, time, threading, tempfile, subprocess, shlex, json
from http.server import BaseHTTPRequestHandler, HTTPServer

WEBSERV_BASE = "http://127.0.0.1:8080"
UPSTREAM_PORT = 9000
UPSTREAM_BASE = f"http://127.0.0.1:{UPSTREAM_PORT}"

# ---------- Upstream Echo Server ----------
class Handler(BaseHTTPRequestHandler):
    def _read_body(self):
        length = int(self.headers.get('Content-Length') or 0)
        return self.rfile.read(length) if length > 0 else b''

    def _send(self, code, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        # quiet (uncomment for verbose)
        # sys.stderr.write("%s - - [%s] %s\n" % (self.client_address[0], self.log_date_time_string(), fmt%args))
        pass

    def do_GET(self):
        if self.path.startswith("/slow"):
            time.sleep(15)  # trigger proxy_read_timeout (~10s) if configured
            return self._send(200, {"ok": True})
        self._send(200, {"method":"GET","path":self.path,"headers":dict(self.headers)})

    def do_POST(self):
        data = self._read_body()
        self._send(200, {"method":"POST","path":self.path,"len":len(data),"headers":dict(self.headers)})

    def do_DELETE(self):
        self._send(200, {"method":"DELETE","path":self.path})

def start_upstream(port):
    srv = HTTPServer(("127.0.0.1", port), Handler)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    return srv, t

# ---------- Test helpers ----------
def run(cmd, input_bytes=None):
    p = subprocess.Popen(cmd, shell=True,
                         stdin=subprocess.PIPE if input_bytes is not None else None,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out, _ = p.communicate(input=input_bytes)
    return p.returncode, out.decode('utf-8', errors='replace')

def curl(url, method="GET", data_path=None, data_bytes=None, headers=None,
         chunked=False, max_time=None):
    parts = ["curl -i -sS"]
    if max_time is not None:
        parts += ["--max-time", str(int(max_time))]
    if method != "GET":
        parts += ["-X", shlex.quote(method)]
    if headers:
        for h in headers:
            parts += ["-H", shlex.quote(h)]
    if chunked:
        # FIX: quote the header as one argument
        parts += ["-H", shlex.quote("Transfer-Encoding: chunked")]
    if data_path:
        parts += ["--data-binary", f"@{shlex.quote(data_path)}"]
    elif data_bytes is not None:
        parts += ["--data-binary", "@-"]
    parts += [shlex.quote(url)]
    cmd = " ".join(parts)
    code, out = run(cmd, input_bytes=data_bytes)
    line = out.splitlines()[0] if out else ""
    print(f"$ {cmd}\n{line}")
    body = out.split("\r\n\r\n", 1)[1] if "\r\n\r\n" in out else ""
    if body:
        preview = body[:120].replace("\n", "\\n")
        print(f"  body: {preview}{'...' if len(body)>120 else ''}")
    return code, out

def expect(out, *prefixes):
    line = out.splitlines()[0] if out else ""
    ok = any(line.startswith(p) for p in prefixes)
    print(f"  expect {prefixes} -> {'OK' if ok else 'FAIL'}")
    return ok

# ---------- Test suite ----------
def main():
    print("== Start upstream echo on :9000 ==")
    srv, thread = start_upstream(UPSTREAM_PORT)
    time.sleep(0.3)
    fails = 0

    print("\n== 0) Direct upstream sanity ==")
    _, out = curl(f"{UPSTREAM_BASE}/hello")
    if not expect(out, "HTTP/1.0 200", "HTTP/1.1 200"): fails += 1

    print("\n== 1) Proxy GET /api/hello (prefix stripped to /hello) ==")
    _, out = curl(f"{WEBSERV_BASE}/api/hello")
    if not expect(out, "HTTP/1.0 200", "HTTP/1.1 200"): fails += 1

    print("\n== 2) Proxy POST small body /api/echo ==")
    _, out = curl(f"{WEBSERV_BASE}/api/echo", method="POST",
                  data_bytes=b"hello proxy",
                  headers=["Content-Type: application/x-www-form-urlencoded"])
    if not expect(out, "HTTP/1.0 200", "HTTP/1.1 200"): fails += 1

    print("\n== 3) Proxy POST large body (5MB) -> expect 200 or 413 depending on limit ==")
    big = tempfile.NamedTemporaryFile(delete=False)
    try:
        big.write(b"X" * (5 * 1024 * 1024))
        big.close()
        _, out = curl(f"{WEBSERV_BASE}/api/echo", method="POST", data_path=big.name)
        if out.startswith("HTTP/1.0 200") or out.startswith("HTTP/1.1 200"):
            print("  large upload proxied: OK")
        elif out.startswith("HTTP/1.0 413") or out.startswith("HTTP/1.1 413"):
            print("  large upload blocked (413): OK")
        else:
            print("  unexpected status"); fails += 1
    finally:
        os.unlink(big.name)

    print("\n== 4) Proxy chunked upload /api/echo ==")
    _, out = curl(f"{WEBSERV_BASE}/api/echo", method="POST",
                  data_bytes=b"chunked works\n", chunked=True)
    if not expect(out, "HTTP/1.0 200", "HTTP/1.1 200"): fails += 1

    print("\n== 5) Proxy DELETE /api/item/42 ==")
    _, out = curl(f"{WEBSERV_BASE}/api/item/42", method="DELETE")
    if not expect(out, "HTTP/1.0 200", "HTTP/1.1 200"): fails += 1

    print("\n== 6) Proxy read-timeout using GET /api/slow (upstream sleeps 15s) ==")
    start = time.time()
    # FIX: cap curl runtime so the test doesn't hang forever if no 504
    code, out = curl(f"{WEBSERV_BASE}/api/slow", max_time=12)
    elapsed = int(time.time() - start)
    if out.startswith("HTTP/1.0 504") or out.startswith("HTTP/1.1 504"):
        print(f"  got 504 in ~{elapsed}s: OK")
    elif code == 28:
        print(f"  curl timed out (~{elapsed}s) — likely proxy_read_timeout not enforced; treat as FAIL here")
        fails += 1
    else:
        print(f"  expected 504, got: {out.splitlines()[0] if out else 'NO OUTPUT'}")
        fails += 1

    print("\n== Summary ==")
    print("All proxy tests passed ✅" if fails == 0 else f"{fails} test(s) failed ❌")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
