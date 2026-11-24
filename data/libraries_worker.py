#!/usr/bin/env python3
"""What this Python can import, for Jotter's Libraries panel. Prints one JSON object:

  {"python": "3.12.3",
   "installed": [[name, version, summary], ...],     what was added to this Python, by the name to import
   "builtin": [name, ...]}                            what every Python comes with

Standard library only.
"""
import json
import sys


def installed():
    try:
        from importlib import metadata
    except ImportError:         # Python before 3.8
        return []
    about = {}
    for dist in metadata.distributions():
        name = dist.metadata.get("Name")
        if name:
            about[name] = (dist.version or "", (dist.metadata.get("Summary") or "").strip())
    rows = {}
    try:
        tops = metadata.packages_distributions()        # import name -> what it came in (Python 3.10 on)
    except Exception:
        tops = {}
    placed = set()
    for top, dists in tops.items():
        placed.update(dists)
        if top.startswith("_") or not top.isidentifier():
            continue
        version, summary = about.get(dists[0], ("", ""))
        rows[top] = [top, version, summary]
    for name, (version, summary) in about.items():      # a package that does not say what it holds (the system's
        if name not in placed:                          # own often do not), or an older Python: the name it goes by
            rows.setdefault(name, [name, version, summary])
    return sorted(rows.values(), key=lambda row: row[0].lower())


if __name__ == "__main__":
    builtin = sorted((n for n in getattr(sys, "stdlib_module_names", ()) if not n.startswith("_")), key=str.lower)
    print(json.dumps({"python": sys.version.split()[0], "installed": installed(), "builtin": builtin}))
