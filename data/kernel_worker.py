#!/usr/bin/env python3
"""Jotter's Python kernel: one long-lived process, one shared namespace.

Runs as a plain script under whatever Python the user points Jotter at, so it
uses the standard library only.

  stdin    one JSON request per line:
             {"op": "exec", "id": n, "code": "...", "figdir": "/path"}
             {"op": "chdir", "path": "/path"}
             {"op": "interrupt"}        (Windows; elsewhere Jotter sends SIGINT)
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
import time
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


# ---- !commands -------------------------------------------------------------

def shell(command):
    """Run a `!command` line; what it prints goes where print() goes."""
    import subprocess
    if os.name != "nt":
        subprocess.run(command, shell=True)
        return
    # Windows: commands print in whichever code page they please, and waiting
    # for one cannot be interrupted. So read what it prints and pass that on as
    # text, and wait in short steps.
    import threading
    proc = subprocess.Popen(command, shell=True, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def relay():
        for raw in proc.stdout:
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                text = raw.decode("oem", "replace")
            sys.stdout.write(text.replace("\r\n", "\n"))

    reader = threading.Thread(target=relay, daemon=True)
    reader.start()
    try:
        while proc.poll() is None:
            time.sleep(0.02)
    except BaseException:       # stopped: take down the command and whatever it started
        if subprocess.call(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
            proc.kill()
        raise
    finally:
        reader.join(1)          # (something it left running may keep the pipe open: do not wait for that)


# ---- running cells ---------------------------------------------------------

def preprocess(code):
    """Accept the two notebook-isms people paste most: `!shell command` lines
    and `%matplotlib inline`."""
    lines = []
    for line in code.split("\n"):
        body = line.lstrip()
        indent = line[:len(line) - len(body)]
        if body.startswith("!"):
            line = "%s__import__('__main__').shell(%r)" % (indent, body[1:])
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


# ---- requests --------------------------------------------------------------

def parse(line):
    try:
        req = json.loads(line)
    except ValueError:
        return {}
    return req if isinstance(req, dict) else {}


class WindowsInbox:
    """Windows cannot signal a busy process. There Jotter asks for an interrupt
    on the control channel, a thread reads that channel while the cell runs,
    and the interrupt is raised from in here."""

    def __init__(self, ctrl):
        import queue
        import threading
        self.lines = queue.Queue()
        self.lock = threading.Lock()
        self.busy = False
        threading.Thread(target=self.read, args=(ctrl,), daemon=True).start()

    def read(self, ctrl):
        import signal
        interrupt = getattr(signal, "raise_signal", None)
        if interrupt is None:               # Python before 3.8
            import _thread
            interrupt = lambda signum: _thread.interrupt_main()
        for line in iter(ctrl.readline, ""):
            if parse(line).get("op") == "interrupt":
                with self.lock:
                    if self.busy:           # between cells there is nothing to stop
                        interrupt(signal.SIGINT)
            else:
                self.lines.put(line)
        self.lines.put("")                  # Jotter has gone

    def readline(self):
        return self.lines.get()

    def start(self):
        with self.lock:
            self.busy = True

    def finish(self):
        # The cell is over. No interrupt may be raised from here on, and one
        # raised a moment ago has to land now rather than on the next request.
        while True:
            try:
                with self.lock:
                    self.busy = False
                for _ in range(10):
                    pass
                return
            except KeyboardInterrupt:
                pass


def main():
    global FIGDIR
    # Keep the control channel to ourselves: the cell sees an empty stdin.
    ctrl = os.fdopen(os.dup(0), "r", encoding="utf-8")
    null = os.open(os.devnull, os.O_RDONLY)
    os.dup2(null, 0)
    os.close(null)
    inbox = None
    if os.name == "nt":
        inbox = WindowsInbox(ctrl)
        for stream in (sys.stdout, sys.stderr):
            stream.reconfigure(newline="\n")    # print() ends lines the same way everywhere
    readline = inbox.readline if inbox else ctrl.readline

    here = os.path.normcase(os.path.dirname(os.path.abspath(__file__)))
    sys.path[:] = [""] + [p for p in sys.path if p and os.path.normcase(os.path.abspath(p)) != here]
    sys.meta_path.insert(0, _Backend())
    os.environ["MPLBACKEND"] = "module://jotter_mpl"

    ns = {"__name__": "__main__", "display": display}
    send(type="ready", pid=os.getpid(), python=sys.version.split()[0])
    n = 0
    while True:
        try:
            line = readline()
        except KeyboardInterrupt:
            continue
        if not line:
            break
        req = parse(line)
        op = req.get("op")
        if op == "exec":
            n += 1
            FIGDIR = req.get("figdir") or "."
            try:
                try:
                    if inbox:
                        inbox.start()
                    ok = run_cell(req.get("code", ""), n, ns)
                finally:
                    if inbox:
                        inbox.finish()
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
