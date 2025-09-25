#!/usr/bin/env python3
# rate_limit_probe.py
# Simple tester for per-IP token-bucket rate limiting in your webserv.
#
# Features:
#   - Sequential "burst" of requests (fast loop or paced with --interval)
#   - Optional concurrent hammer (--concurrency > 1)
#   - Detects 429 and prints Retry-After
#   - Sleeps Retry-After and verifies that requests recover to 200
#
# Usage examples:
#   python3 rate_limit_probe.py
#   python3 rate_limit_probe.py --url http://127.0.0.1:8080/api/hello --count 12
#   python3 rate_limit_probe.py --url http://127.0.0.1:8080/static/index.html --count 12 --interval 0.05
#   python3 rate_limit_probe.py --url http://127.0.0.1:8080/api/hello --count 40 --concurrency 8
#
# No external dependencies (uses stdlib http.client / urllib.parse).
import argparse
import http.client
import time
import sys
from urllib.parse import urlparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import Counter


def do_request(url, timeout=5.0, method="GET", body=None, headers=None):
    """
    Returns: (status:int, headers:dict, body_len:int, err:str|None, elapsed_ms:int)
    """
    headers = headers or {}
    parsed = urlparse(url)
    scheme = parsed.scheme or "http"
    host = parsed.hostname or "127.0.0.1"
    port = parsed.port or (443 if scheme == "https" else 80)
    path = parsed.path or "/"
    if parsed.query:
        path += "?" + parsed.query

    t0 = time.time()
    conn = None
    try:
        if scheme == "https":
            conn = http.client.HTTPSConnection(host, port, timeout=timeout)
        else:
            conn = http.client.HTTPConnection(host, port, timeout=timeout)
        conn.request(method, path, body=body, headers=headers)
        resp = conn.getresponse()
        data = resp.read()  # we don't print it, just consume to avoid socket reuse issues
        elapsed = int(round((time.time() - t0) * 1000))
        return resp.status, dict(resp.getheaders()), len(data), None, elapsed
    except Exception as e:
        elapsed = int(round((time.time() - t0) * 1000))
        return 0, {}, 0, str(e), elapsed
    finally:
        if conn:
            try:
                conn.close()
            except Exception:
                pass


def run_sequential(url, count, interval, timeout):
    print(f"== Sequential burst: {count} requests to {url} (interval={interval}s) ==")
    results = []
    first_429_retry_after = None

    for i in range(1, count + 1):
        status, hdrs, blen, err, ms = do_request(url, timeout=timeout)
        if status == 429 and first_429_retry_after is None:
            ra = hdrs.get("Retry-After") or hdrs.get("retry-after")
            first_429_retry_after = int(ra) if (ra and ra.isdigit()) else None
        results.append((status, ms, err))
        print(f"{i:02d}: {status} ({ms} ms){'  ERR: '+err if err else ''}")
        if interval > 0:
            time.sleep(interval)

    return results, first_429_retry_after


def run_concurrent(url, count, concurrency, timeout):
    print(f"== Concurrent hammer: {count} requests to {url} (concurrency={concurrency}) ==")
    results = []
    first_429_retry_after = None

    def task(_):
        return do_request(url, timeout=timeout)

    with ThreadPoolExecutor(max_workers=concurrency) as ex:
        futs = [ex.submit(task, i) for i in range(count)]
        idx = 0
        for fut in as_completed(futs):
            idx += 1
            status, hdrs, blen, err, ms = fut.result()
            if status == 429 and first_429_retry_after is None:
                ra = hdrs.get("Retry-After") or hdrs.get("retry-after")
                first_429_retry_after = int(ra) if (ra and ra.isdigit()) else None
            results.append((status, ms, err))
            print(f"{idx:02d}: {status} ({ms} ms){'  ERR: '+err if err else ''}")

    return results, first_429_retry_after


def summarize(results):
    codes = [s for (s, _, _) in results]
    err_count = sum(1 for (s, _, e) in results if s == 0 or e)
    c = Counter(codes)
    print("\n== Summary ==")
    for k in sorted(c.keys()):
        print(f"  {k}: {c[k]}")
    if err_count:
        print(f"  errors: {err_count}")
    # Simple hints
    if 429 in c and (200 in c or 201 in c or 204 in c):
        print("  ✔ Observed throttling (429) and successful responses (200).")
    elif 429 in c and len(c) == 1:
        print("  ⚠ Only 429s observed; your rate might be far above the limit or burst=0.")
    elif 200 in c and 429 not in c:
        print("  ℹ No throttling observed; increase request rate or lower the limit.")


def main():
    ap = argparse.ArgumentParser(description="Rate limit probe for webserv")
    ap.add_argument("--url", default="http://127.0.0.1:8080/static/index.html",
                    help="Target URL to hit repeatedly")
    ap.add_argument("--count", type=int, default=12,
                    help="Number of requests to send")
    ap.add_argument("--interval", type=float, default=0.0,
                    help="Seconds to sleep between sequential requests (0 = as fast as possible)")
    ap.add_argument("--concurrency", type=int, default=1,
                    help=">1 to use a thread pool and send requests concurrently")
    ap.add_argument("--timeout", type=float, default=5.0,
                    help="Per-request timeout (seconds)")
    ap.add_argument("--verify-retry-after", action="store_true",
                    help="After first 429 with Retry-After, sleep and probe once more")
    args = ap.parse_args()

    if args.concurrency > 1 and args.interval > 0:
        print("Note: --interval is ignored for concurrent mode.", file=sys.stderr)

    if args.concurrency > 1:
        results, retry_after = run_concurrent(args.url, args.count, args.concurrency, args.timeout)
    else:
        results, retry_after = run_sequential(args.url, args.count, args.interval, args.timeout)

    summarize(results)

    if args.verify_retry_after and retry_after:
        print(f"\n== Verify Retry-After ({retry_after}s) ==")
        time.sleep(retry_after)
        s, hdrs, blen, err, ms = do_request(args.url, timeout=args.timeout)
        print(f"after sleep: {s} ({ms} ms){'  ERR: '+err if err else ''}")


if __name__ == "__main__":
    main()
