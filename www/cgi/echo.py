#!/usr/bin/env python3
import os, sys
cl = int(os.environ.get("CONTENT_LENGTH","0") or 0)
data = sys.stdin.read(cl) if cl > 0 else ""
print("Content-Type: text/plain")
print()
print("method=" + os.environ.get("REQUEST_METHOD",""))
print("len=" + str(cl))
print("data=" + data)
