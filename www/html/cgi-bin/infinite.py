#!/usr/bin/env python3

import time

print("Content-Type: text/html")
print()
print("<html><body>")
print("<h1>Infinite Loop</h1>")
print("</body></html>")

while True:
    time.sleep(1)