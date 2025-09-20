#!/usr/bin/env python3
import requests, os, sys

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
EP = os.environ.get("UPLOAD_ENDPOINT", "/upload")
URL = BASE_URL + EP

# 10k parts, each 1 byte
files = [("f%05d" % i, ("p%05d.txt" % i, b"x")) for i in range(10000)]

print("POST", URL, "with", len(files), "parts…")
r = requests.post(URL, files=files, timeout=120)
print("Status:", r.status_code)
print("Body (first 200):", r.text[:200])
sys.exit(0 if r.status_code in (200,201) else 1)
