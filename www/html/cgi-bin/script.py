#!/usr/bin/env python3

import os

print("Content-Type: text/html")
print()
print("<html><body>")
print("<h1>CGI Script Works!</h1>")
print("<p>REQUEST_METHOD: {}</p>".format(os.environ.get("REQUEST_METHOD", "")))
print("<p>QUERY_STRING: {}</p>".format(os.environ.get("QUERY_STRING", "")))
print("<p>CONTENT_LENGTH: {}</p>".format(os.environ.get("CONTENT_LENGTH", "")))
print("<p>SCRIPT_NAME: {}</p>".format(os.environ.get("SCRIPT_NAME", "")))
print("</body></html>")