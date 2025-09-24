#!/usr/bin/env python3
import socket, sys, time

HOST = "127.0.0.1"
PORT = 8080

print(f"[probe] connecting to {HOST}:{PORT} ...", flush=True)
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(3.0)  # 3s connect timeout
try:
    s.connect((HOST, PORT))
    print("[probe] connected!", flush=True)
    s.settimeout(1.0)
    # Send a tiny HTTP/1.1 request to see if anything comes back
    s.sendall(b"HEAD / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    print("[probe] sent HEAD /", flush=True)
    data = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    print("[probe] received bytes:", len(data), flush=True)
    if data:
        print("--- response start ---")
        print(data.decode("utf-8", errors="replace"))
        print("--- response end ---")
    else:
        print("[probe] no response bytes", flush=True)
    s.close()
    sys.exit(0)
except Exception as e:
    print(f"[probe] connect/send failed: {e}", flush=True)
    sys.exit(1)
