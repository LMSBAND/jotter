#!/usr/bin/env python3
"""Jotter's updater: asks where the newest release is and fetches a file from it.

  update_worker.py latest           prints {"version": "0.2.0", "page": url, "files": {name: url}}
  update_worker.py fetch URL FILE   downloads URL into FILE, printing percentages as it goes

Standard library only. On failure: the reason on stderr and a non-zero exit.
Releases are GitHub's; JOTTER_UPDATE_URL names another place that answers in
the same form.
"""
import json
import os
import sys
import urllib.request

LATEST = "https://api.github.com/repos/LMSBAND/jotter/releases/latest"


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "Jotter"}), timeout=30)


def latest():
    with get(os.environ.get("JOTTER_UPDATE_URL") or LATEST) as reply:
        release = json.loads(reply.read().decode("utf-8"))
    print(json.dumps({
        "version": str(release.get("tag_name", "")).lstrip("v"),
        "page": release.get("html_url", ""),
        "files": {a["name"]: a["browser_download_url"] for a in release.get("assets", [])},
    }))


def fetch(url, path):
    with get(url) as reply, open(path, "wb") as out:
        total = int(reply.headers.get("Content-Length") or 0)
        done, shown = 0, -1
        while True:
            chunk = reply.read(1 << 16)
            if not chunk:
                break
            out.write(chunk)
            done += len(chunk)
            percent = done * 100 // total if total else 0
            if percent != shown:
                shown = percent
                print(percent, flush=True)


if __name__ == "__main__":
    try:
        if sys.argv[1:2] == ["latest"]:
            latest()
        elif sys.argv[1:2] == ["fetch"] and len(sys.argv) == 4:
            fetch(sys.argv[2], sys.argv[3])
        else:
            sys.exit(__doc__)
    except Exception as e:      # no network, no release yet, a disk that is full...
        sys.exit("%s: %s" % (type(e).__name__, e))
