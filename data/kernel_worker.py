#!/usr/bin/env python3
"""Jotter's Python kernel: one long-lived process, one shared namespace.

Runs as a plain script under whatever Python the user points Jotter at, so it
uses the standard library only.

  stdin    one JSON request per line:
             {"op": "exec", "id": n, "code": "...", "figdir": "/path"}
             {"op": "chdir", "path": "/path"}
  stdout   whatever the cell prints (stderr is merged in by the parent),
           interleaved with control records of the form  \\x1e{json}\\n :
             {"type": "image", "name": "out-<hash>.png"}   written into figdir
             {"type": "error", "text": "traceback…"}
             {"type": "done", "id": n, "ok": bool}
"""
import ast
import hashlib
import importlib.abc
import importlib.util
import io
import json
import linecache
import os
import sys
import traceback

MARK = "\x1e"
DPI = 192           # 2x the 96 px/inch the page is laid out at, so plots stay crisp
FIGDIR = "."
_out = sys.stdout


def send(**msg):
    sys.stdout.flush()
    sys.stderr.flush()
    _out.write(MARK + json.dumps(msg) + "\n")
    _out.flush()


# ---- figures ---------------------------------------------------------------

def emit_image(data):
    name = "out-" + hashlib.sha1(data).hexdigest()[:12] + ".png"
    path = os.path.join(FIGDIR, name)
    if not os.path.exists(path):
        os.makedirs(FIGDIR, exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
    send(type="image", name=name)


def emit_figure(fig):
    if len(fig.get_children()) <= 1:      # just the background: nothing was drawn
        return
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=DPI, bbox_inches="tight")
    emit_image(buf.getvalue())


def flush_figures():
    """Show every open matplotlib figure, then close them (notebook behaviour)."""
    plt = sys.modules.get("matplotlib.pyplot")
    if plt is None:
        return
    for num in plt.get_fignums():
        emit_figure(plt.figure(num))
    plt.close("all")


class _Backend(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    """Provides the `jotter_mpl` matplotlib backend on demand: Agg rendering,
    with plt.show() sending figures to the document instead of opening a window.
    Built lazily so matplotlib is only imported if the user imports it."""

    def find_spec(self, name, path=None, target=None):
        if name == "jotter_mpl":
            return importlib.util.spec_from_loader(name, self)
        return None

    def create_module(self, spec):
        return None

    def exec_module(self, module):
        from matplotlib.backend_bases import FigureManagerBase
        from matplotlib.backends.backend_agg import FigureCanvasAgg
        module.FigureCanvas = FigureCanvasAgg
        module.FigureManager = FigureManagerBase
        module.show = lambda *args, **kwargs: flush_figures()


# ---- showing values --------------------------------------------------------

def _is_mpl(value):
    if isinstance(value, (list, tuple)):
        return bool(value) and all(_is_mpl(v) for v in value)
    return type(value).__module__.split(".")[0] == "matplotlib"


def show(value):
    """What a notebook does with a cell's last expression."""
    if hasattr(value, "savefig") and hasattr(value, "get_axes"):       # a Figure
        emit_figure(value)
        plt = sys.modules.get("matplotlib.pyplot")
        if plt is not None:
            plt.close(value)
        return
    if _is_mpl(value):          # plt.plot(...) returns artists; their repr is noise
        return
    png = getattr(value, "_repr_png_", None)
    if callable(png):
        try:
            data = png()
        except Exception:
            data = None
        if data:
            emit_image(data)
            return
    print(repr(value))


def display(*values):
    for v in values:
        show(v)


# ---- running cells ---------------------------------------------------------

def preprocess(code):
    """Accept the two notebook-isms people paste most: `!shell command` lines
    and `%matplotlib inline`."""
    lines = []
    for line in code.split("\n"):
        body = line.lstrip()
        indent = line[:len(line) - len(body)]
        if body.startswith("!"):
            line = "%s__import__('subprocess').run(%r, shell=True)" % (indent, body[1:])
        elif body.startswith("%matplotlib"):
            line = indent + "pass"
        lines.append(line)
    return "\n".join(lines)


def run_cell(code, n, ns):
    name = "<cell %d>" % n
    code = preprocess(code)
    linecache.cache[name] = (len(code), None, code.splitlines(True), name)
    ok = True
    try:
        tree = ast.parse(code, name, "exec")
        last = None
        if tree.body and isinstance(tree.body[-1], ast.Expr) and not code.rstrip().endswith(";"):
            last = ast.Expression(tree.body.pop().value)
        exec(compile(tree, name, "exec"), ns)
        if last is not None:
            value = eval(compile(last, name, "eval"), ns)
            if value is not None:
                ns["_"] = value
                show(value)
    except SystemExit:
        pass
    except BaseException:
        ok = False
        etype, exc, tb = sys.exc_info()
        while tb is not None and not tb.tb_frame.f_code.co_filename.startswith("<cell"):
            tb = tb.tb_next       # hide the kernel's own frames
        send(type="error", text="".join(traceback.format_exception(etype, exc, tb)).rstrip())
    try:
        flush_figures()
    except Exception:
        ok = False
        send(type="error", text=traceback.format_exc().rstrip())
    return ok


def main():
    global FIGDIR
    # Keep the control channel to ourselves: the cell sees an empty stdin.
    ctrl = os.fdopen(os.dup(0), "r", encoding="utf-8")
    null = os.open(os.devnull, os.O_RDONLY)
    os.dup2(null, 0)
    os.close(null)

    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:] = [""] + [p for p in sys.path if p and os.path.abspath(p) != here]
    sys.meta_path.insert(0, _Backend())
    os.environ["MPLBACKEND"] = "module://jotter_mpl"

    ns = {"__name__": "__main__", "display": display}
    send(type="ready", pid=os.getpid(), python=sys.version.split()[0])
    n = 0
    while True:
        try:
            line = ctrl.readline()
        except KeyboardInterrupt:
            continue
        if not line:
            break
        try:
            req = json.loads(line)
        except ValueError:
            continue
        op = req.get("op")
        if op == "exec":
            n += 1
            FIGDIR = req.get("figdir") or "."
            try:
                ok = run_cell(req.get("code", ""), n, ns)
            except KeyboardInterrupt:
                ok = False
                send(type="error", text="KeyboardInterrupt")
            send(type="done", id=req.get("id"), ok=ok)
        elif op == "chdir":
            try:
                os.chdir(req.get("path", "."))
            except OSError:
                pass


if __name__ == "__main__":
    main()
