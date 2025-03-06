#!/usr/bin/env python3

print("Content-Type: text/html")
print()
print("<html><body>")
print("<h1>Error Script</h1>")
print("</body></html>")

raise Exception("This is an intentional error.")