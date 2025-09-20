#!/usr/bin/env python3
import os, uuid, requests, sys

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8080")
EP = os.environ.get("UPLOAD_ENDPOINT", "/upload")
URL = BASE_URL + EP

boundary = "----PyChunkedBoundary" + uuid.uuid4().hex
CRLF = "\r\n"

def make_part(name, filename, content, ctype="text/plain"):
    hdrs = [
        f"--{boundary}",
        f'Content-Disposition: form-data; name="{name}"; filename="{filename}"',
        f"Content-Type: {ctype}",
        "",
        ""
    ]
    head = CRLF.join(hdrs).encode("utf-8")
    return head + content + CRLF.encode("utf-8")

parts = [
    make_part("file1", "c1.txt", b"hello"),
    make_part("file2", "c2.txt", b"world"),
]

tail = (f"--{boundary}--{CRLF}").encode("utf-8")

def gen():
    # stream in small chunks to simulate chunking
    for p in parts:
        for i in range(0, len(p), 7):
            yield p[i:i+7]
    for i in range(0, len(tail), 5):
        yield tail[i:i+5]

headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}

print("POST (chunked) →", URL)
r = requests.post(URL, data=gen(), headers=headers)  # generator => chunked
print("Status:", r.status_code)
print("Body:", r.text[:200])

sys.exit(0 if r.status_code in (200,201) else 1)
