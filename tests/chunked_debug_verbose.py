#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import socket, sys, time, traceback

HOST = "127.0.0.1"       # change if needed
PORT = 8080
PATH = "/upload"
HOST_HEADER = "127.0.0.1" # match your server_name if using vhosts
BOUNDARY = "----PyChunkedBoundaryDebug"
CRLF = "\r\n"

def part(name, filename, content, ctype="text/plain"):
    head = (
        f"--{BOUNDARY}{CRLF}"
        f'Content-Disposition: form-data; name="{name}"; filename="{filename}"{CRLF}'
        f"Content-Type: {ctype}{CRLF}{CRLF}"
    ).encode("utf-8")
    return head + content + CRLF.encode("utf-8")

# keep payload small to avoid 413 while debugging
P1 = b"hello"
P2 = b"world"
BODY = b"".join([part("file1", "a.txt", P1), part("file2", "b.txt", P2)]) + f"--{BOUNDARY}--{CRLF}".encode("utf-8")

def send_chunk(sock, data, label):
    try:
        sz = ("%x" % len(data)).encode("ascii") + b"\r\n"
        print(f"[send] {label}: size={len(data)}", flush=True)
        sock.sendall(sz)
        if data:
            sock.sendall(data)
        sock.sendall(b"\r\n")
        return True
    except Exception as e:
        print(f"[send] FAILED during {label}: {e}", flush=True)
        return False

def recv_some(sock, timeout=2.0):
    sock.settimeout(timeout)
    chunks = []
    while True:
        try:
            b = sock.recv(4096)
            if not b:
                break
            chunks.append(b)
            if len(b) < 4096:
                break
        except socket.timeout:
            break
        except Exception:
            break
    return b"".join(chunks)

def main():
    print(f"[info] POST {PATH} (chunked) to {HOST}:{PORT}", flush=True)
    try:
        s = socket.create_connection((HOST, PORT), timeout=3.0)
    except Exception as e:
        print(f"[error] TCP connect failed: {e}", flush=True)
        return 1

    req_head = (
        f"POST {PATH} HTTP/1.1{CRLF}"
        f"Host: {HOST_HEADER}{CRLF}"
        f"User-Agent: chunked-debug/1.0{CRLF}"
        f"Accept: */*{CRLF}"
        f"Transfer-Encoding: chunked{CRLF}"
        f"Content-Type: multipart/form-data; boundary={BOUNDARY}{CRLF}"
        f"Connection: close{CRLF}"
        f"{CRLF}"
    ).encode("utf-8")

    try:
        print("[send] headers", flush=True)
        s.sendall(req_head)
    except Exception as e:
        print(f"[error] sending headers failed: {e}", flush=True)
        s.close()
        return 1

    # stream the body in small chunks
    off = 0
    CH = 64
    while off < len(BODY):
        piece = BODY[off:off+CH]
        if not send_chunk(s, piece, f"chunk@{off}"):
            # grab any response the server might have already sent
            resp = recv_some(s, timeout=1.5)
            if resp:
                print("\n--- partial response ---")
                print(resp.decode("utf-8", errors="replace"))
                print("--- end partial ---")
            s.close()
            return 1
        off += CH
        time.sleep(0.02)

    # final zero-length chunk
    try:
        print("[send] final 0-chunk", flush=True)
        s.sendall(b"0\r\n\r\n")
    except Exception as e:
        print(f"[error] final chunk failed: {e}", flush=True)

    # read the response
    print("[recv] reading response...", flush=True)
    resp = recv_some(s, timeout=3.0)
    s.close()

    if not resp:
        print("[recv] no response bytes (closed with nothing)", flush=True)
        return 1

    print("\n===== RAW RESPONSE START =====")
    print(resp.decode("utf-8", errors="replace"))
    print("===== RAW RESPONSE END =====\n")

    # parse status
    try:
        first = resp.split(b"\r\n", 1)[0]
        code = int(first.split()[1])
        print(f"[info] HTTP status: {code}", flush=True)
        return 0 if 200 <= code < 400 else 1
    except Exception:
        print("[warn] could not parse status line", flush=True)
        return 1

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc()
        sys.exit(1)
