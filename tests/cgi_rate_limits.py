#!/usr/bin/env python3
import requests
import time
import sys

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080/cgi/hello.py"

def hammer(count=50, interval=0.0):
    print(f"== CGI Rate Limit Test: {count} requests to {URL} (interval={interval:.1f}s) ==")
    codes = {}
    for i in range(count):
        t0 = time.time()
        try:
            r = requests.get(URL, timeout=5)
            dt = int((time.time() - t0) * 1000)
            code = r.status_code
            print(f"{i+1:02d}: {code} ({dt} ms)")
            codes[code] = codes.get(code, 0) + 1
        except Exception as e:
            print(f"{i+1:02d}: ERROR {e}")
            codes["ERR"] = codes.get("ERR", 0) + 1
        time.sleep(interval)
    print("\n== Summary ==")
    for k, v in codes.items():
        print(f"  {k}: {v}")
    if 429 in codes:
        print("  ✔ Rate limiter enforced on CGI.")
    else:
        print("  ⚠ No 429s observed — limiter may be too loose.")

if __name__ == "__main__":
    hammer()
