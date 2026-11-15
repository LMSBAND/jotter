#!/usr/bin/env python3
"""Jotter's stand-in formula renderer for machines without LaTeX: matplotlib's mathtext.

  mathtext_worker.py DIR
reads   DIR/batch.json   [{"tex": "...", "display": bool}, ...]
writes  DIR/<n>.gray     the formula as raw 8-bit coverage, row by row
        DIR/out.json     [{"w": px, "h": px, "depth": px} | {"error": "..."}]
Rendered at 12pt, 300 dpi: 50 px per em, the same as the LaTeX route.
"""
import json
import os
import sys


def main(folder):
    import numpy as np
    from matplotlib.font_manager import FontProperties
    from matplotlib.mathtext import MathTextParser

    parser = MathTextParser("agg")
    prop = FontProperties(size=12, math_fontfamily="cm")
    with open(os.path.join(folder, "batch.json"), encoding="utf-8") as f:
        batch = json.load(f)
    out = []
    for n, item in enumerate(batch):
        tex = " ".join(item["tex"].split())
        result, error = None, "cannot be typeset"
        for attempt in ([r"\displaystyle " + tex] if item.get("display") else []) + [tex]:
            try:
                result = parser.parse("$" + attempt + "$", dpi=300, prop=prop)
                break
            except Exception as e:      # mathtext explains itself on the last line
                error = str(e).strip().split("\n")[-1] or error
                error = error.split(", found ")[0].replace("ParseFatalException: ", "").replace("ParseException: ", "")
        if result is None and "\\begin" in tex:
            error = "matrices and aligned equations need LaTeX installed (%s)" % (
                "pdflatex and pdftocairo, as in MiKTeX" if os.name == "nt" else "texlive-latex-base")
        if result is None:
            out.append({"error": error})
            continue
        # newer matplotlib returns a named tuple, older a plain one
        image = result.image if hasattr(result, "image") else result[5]
        depth = result.depth if hasattr(result, "depth") else result[4]
        pixels = np.ascontiguousarray(np.asarray(image), dtype=np.uint8)
        with open(os.path.join(folder, "%d.gray" % n), "wb") as f:
            f.write(pixels.tobytes())
        out.append({"w": int(pixels.shape[1]), "h": int(pixels.shape[0]), "depth": float(depth)})
    with open(os.path.join(folder, "out.json"), "w", encoding="utf-8") as f:
        json.dump(out, f)


if __name__ == "__main__":
    main(sys.argv[1])
