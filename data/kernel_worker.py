#!/usr/bin/env python3
"""Jotter's Python kernel: one long-lived process, one shared namespace.

Runs as a plain script under whatever Python the user points Jotter at, so it
uses the standard library only.

  stdin    one JSON request per line:
             {"op": "exec", "id": n, "code": "...", "figdir": "/path"}
             {"op": "exec", "id": n, "lang": "sql", "code": "..."}
             {"op": "chdir", "path": "/path"}
             {"op": "env", "set": {"NAME": "value"}, "unset": ["NAME"]}
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


# ---- SQL cells -------------------------------------------------------------
# A sql cell runs in this process too, so it and the Python cells can hand
# things to each other: a query's rows are `_` in Python, and :name in a query
# is the Python variable of that name. A line `.open file`, as in the sqlite3
# shell, picks the database for that cell and the ones after it.

SHOWN_ROWS = 50
_databases = {}


class Rows(list):
    """What a query gave back: a list of rows. rows.columns has the column
    names and rows["name"] is one column, as a list."""

    def __init__(self, rows=(), columns=()):
        super().__init__(rows)
        self.columns = list(columns)

    def __getitem__(self, key):
        if isinstance(key, str):
            if key not in self.columns:
                raise KeyError(key)
            i = self.columns.index(key)
            return [row[i] for row in self]
        return super().__getitem__(key)

    def __repr__(self):
        def cell(value):
            if value is None:
                return "NULL"
            if isinstance(value, bytes):
                return "<%d bytes>" % len(value)
            return str(value).replace("\n", " ")

        shown = [[cell(v) for v in row] for row in self[:SHOWN_ROWS]]
        widths = [max([len(str(name))] + [len(row[i]) for row in shown]) for i, name in enumerate(self.columns)]
        right = [bool(self) and all(isinstance(row[i], (int, float)) or row[i] is None for row in self[:SHOWN_ROWS])
                 for i in range(len(self.columns))]

        def line(cells):
            return "  ".join(c.rjust(w) if r else c.ljust(w) for c, w, r in zip(cells, widths, right)).rstrip()

        lines = [line([str(name) for name in self.columns]), line(["-" * w for w in widths])]
        lines += [line(row) for row in shown]
        if len(self) > SHOWN_ROWS:
            lines.append("... %d rows in all" % len(self))
        return "\n".join(lines)


class _Variables(dict):
    """:name in a query, looked up among the Python variables."""

    def __init__(self, ns):
        super().__init__()
        self.ns = ns

    def __getitem__(self, name):
        value = self.ns[name]
        if type(value).__module__ == "numpy" and hasattr(value, "item"):
            value = value.item()        # a numpy number is not one of SQLite's types
        return value


def database():
    """The database sql cells talk to: the SQLite file named by the DATABASE
    environment variable, or without one a scratch database that lasts as long
    as this session."""
    import sqlite3
    name = os.environ.get("DATABASE", "").strip()
    path = os.path.abspath(os.path.expanduser(name)) if name else ":memory:"
    conn = _databases.get(path)
    if conn is None:
        conn = _databases[path] = sqlite3.connect(path)
        conn.set_progress_handler(lambda: 0, 20000)     # gives Stop a way into a long query
    return conn


def dot_commands(code):
    """Act on the `.open file` lines of a cell; gives back the SQL without them."""
    lines = []
    for line in code.split("\n"):
        word = line.strip()
        if word.startswith(".") and word[1:2].isalpha():
            command, _, name = word.partition(" ")
            if command != ".open":
                raise ValueError("A sql cell knows one dot command, .open file (not %s)." % command)
            name = name.strip()
            if len(name) >= 2 and name[0] == name[-1] and name[0] in "\"'":
                name = name[1:-1]
            if name:
                os.environ["DATABASE"] = name
            else:
                os.environ.pop("DATABASE", None)        # back to the scratch database
            line = ""
        lines.append(line)
    return "\n".join(lines)


def statements(code):
    import sqlite3
    parts = code.split(";")
    text = ""
    for i, part in enumerate(parts):
        text += part + (";" if i < len(parts) - 1 else "")
        if sqlite3.complete_statement(text):
            yield text
            text = ""
    if text.strip():
        yield text


def run_sql(code, ns):
    try:
        import sqlite3
    except ImportError:
        send(type="error", text="This Python has no sqlite3 module, which sql cells need.")
        return False
    try:
        code = dot_commands(code)
    except ValueError as e:
        send(type="error", text=str(e))
        return False
    conn = None
    try:
        conn = database()
        for statement in statements(code):
            if not statement.strip(" \t\r\n;"):
                continue
            cursor = conn.execute(statement, _Variables(ns))
            if cursor.description is not None:
                ns["_"] = Rows(cursor.fetchall(), [d[0] for d in cursor.description])
                print(repr(ns["_"]))
        conn.commit()
        return True
    except sqlite3.Error as e:
        if conn is not None:
            conn.rollback()
        send(type="error", text="%s: %s" % (type(e).__name__, e))
        return False


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
            FIGDIR = req.get("figdir") or "."
            try:
                try:
                    if inbox:
                        inbox.start()
                    if req.get("lang") == "sql":
                        ok = run_sql(req.get("code", ""), ns)
                    else:
                        n += 1
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
        elif op == "env":
            for name in req.get("unset") or []:
                os.environ.pop(name, None)
            os.environ.update(req.get("set") or {})


if __name__ == "__main__":
    main()
