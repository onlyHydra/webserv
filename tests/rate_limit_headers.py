#!/usr/bin/env python3
# tests/rate_limit_headers.py
import argparse, http.client, time
from urllib.parse import urlparse

def one(url, timeout=5.0):
    p = urlparse(url)
    host, port = p.hostname or "127.0.0.1", p.port or (443 if (p.scheme=="https") else 80)
    path = (p.path or "/") + (("?" + p.query) if p.query else "")
    Conn = http.client.HTTPSConnection if p.scheme == "https" else http.client.HTTPConnection
    t0 = time.time()
    c = Conn(host, port, timeout=timeout)
    try:
        c.request("GET", path)
        r = c.getresponse()
        body = r.read()
        ms = int((time.time() - t0) * 1000)
        hdrs = dict(r.getheaders())
        return r.status, hdrs, ms, len(body)
    finally:
        try: c.close()
        except: pass

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8080/static/index.html")
    ap.add_argument("--max", type=int, default=20, help="max requests before giving up")
    args = ap.parse_args()

    print(f"== Probe X-Rate headers at {args.url} ==")
    for i in range(1, args.max+1):
        s, h, ms, bl = one(args.url)
        print(f"{i:02d}: {s} ({ms} ms)")
        if s == 429:
            print("\n== 429 headers ==")
            # Show exactly what your server sets
            for k in ["Retry-After", "X-RateLimit-Limit", "X-RateLimit-Remaining"]:
                v = h.get(k) or h.get(k.lower()) or "<absent>"
                print(f"{k}: {v}")
            return
    print("\n(No 429 seen; increase load or lower limits.)")

if __name__ == "__main__":
    main()
