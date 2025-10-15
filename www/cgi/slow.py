#!/usr/bin/env python3
import sys, time
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Connection: keep-alive\r\n")
sys.stdout.write("\r\n")
sys.stdout.flush()
for i in range(10):
    sys.stdout.write("tick %d\n" % i)
    sys.stdout.flush()
    time.sleep(0.5)