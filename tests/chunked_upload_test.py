#!/usr/bin/env python3
import sys
import time
import requests

URL = "http://127.0.0.1:8080/upload"
boundary = "----PyChunkedBoundary123456"
CRLF = "\r\n"


def make_part(name, filename, content, ctype="text/plain"):
    head = (
        f"--{boundary}{CRLF}"
        f'Content-Disposition: form-data; name="{name}"; filename="{filename}"{CRLF}'
        f"Content-Type: {ctype}{CRLF}{CRLF}"
    ).encode("utf-8")
    return head + content + CRLF.encode("utf-8")


parts = [
    make_part("file1", "a.txt", b"hello"),
    make_part("file2", "b.txt", b"world"),
]
tail = (f"--{boundary}--{CRLF}").encode("utf-8")


def gen():
    # Stream in small pieces to force chunked behavior
    for p in parts:
        for i in range(0, len(p), 7):
            yield p[i:i+7]
            time.sleep(0.01)
    for i in range(0, len(tail), 5):
        yield tail[i:i+5]
        time.sleep(0.01)


headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}

print("POST (chunked) →", URL)
try:
    r = requests.post(URL, data=gen(), headers=headers)
    print("Status:", r.status_code)
    print("Body:", r.text[:500])
except Exception as e:
    print("Upload failed:", e)
    sys.exit(1)
