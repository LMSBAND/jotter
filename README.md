# Jotter

A word processor with runnable code cells: write like in Word, run code like in
a notebook. C++/Qt, one small binary. Documents are plain Markdown files.

## Build

```
sudo apt install qtbase5-dev qtwebengine5-dev libkf5syntaxhighlighting-dev libmd4c-dev libhunspell-dev
./install.sh          # builds into build/, installs to ~/.local
```

`jotter [file.md]`, or "Jotter" in the application menu. `build/jotter-tests`
runs the tests.

Without those packages but with Docker: `packaging/local.sh` builds in the
container the AppImage is made in and installs to `~/.local/bin`, and
`packaging/local.sh test` runs the tests there.

Built this way it uses the machine's `python3` for Python cells (with whatever
numpy/matplotlib that Python has), `pdflatex` + `pdftocairo` for math (or
matplotlib if there is no LaTeX), and the tools you already have for other
languages (bash, node, lean, dot, gcc...). Anything missing just means that one
kind of cell does not run.

## One file for other machines

```
packaging/appimage.sh       # needs Docker; writes dist/Jotter-x86_64.AppImage and .tar.gz
```

The AppImage carries Qt, the PDF engine, a Python with numpy and matplotlib, an
English dictionary and its icons, so it runs on a machine with none of that:
`chmod +x Jotter-x86_64.AppImage`, then run it. It is built in an Ubuntu 22.04
container and runs there and on anything newer. LaTeX is not inside (too big):
where `pdflatex` exists formulas are set with it, elsewhere by a built-in
renderer that handles ordinary formulas but not matrices or `aligned`. If the
machine's own `python3` already has numpy and matplotlib, cells use that one
(and so see the other packages installed there) instead of the shipped one.

`Jotter-x86_64.tar.gz` is the same build as a folder, for a machine where an
AppImage will not start: unpack it anywhere and run `Jotter/AppRun`.

## Windows

```
packaging/windows.sh        # needs Docker; writes dist/Jotter-Setup-x86_64.exe
```

An installer for 64-bit Windows 10 and 11, cross-compiled with MinGW in an
Ubuntu container, so no Windows machine is involved in making it. It installs
into the user's own programs folder without asking for administrator rights,
adds a Start menu entry and an uninstaller, and carries Qt, a Python with
numpy, matplotlib and pip, and an English dictionary. Differences from Linux:
PDF export is printed by Edge (or Chrome) without showing a window, the shell
cells are `powershell` and `batch`, and matrices in formulas need a LaTeX with
`pdflatex` and `pdftocairo` on the PATH. The installer is not signed, so
Windows asks before running it ("More info", then "Run anyway").

## Using it

Type. Markdown habits work as you go: `# ` heading, `- ` list, `1. ` numbered,
`[] ` checkbox, `> ` quote, `---` rule, `**bold**`, `*italic*`, `` `code` ``,
`$x^2$` math, and ```` ```python ```` + Enter for a code cell.

| | |
| --- | --- |
| Ctrl+Enter | run the cell |
| Shift+Enter | run it and move on |
| Ctrl+Shift+Enter | run every cell |
| Ctrl+. | stop |
| Alt+Enter | new paragraph below the cell or table you are in |
| Ctrl+Alt+C / Ctrl+Alt+P | new code cell / new plot |
| Ctrl+Shift+H | hide or show a cell's code (its output stays) |
| Hide All Code (toolbar) | hide every cell's code at once; click again to bring it back |
| Ctrl+M | formula (LaTeX) |
| Ctrl+K | link |
| Ctrl+Alt+1..3, Ctrl+Alt+0 | headings, body text |
| Ctrl+P | export PDF |

Python cells share one session per document, top to bottom, like a notebook:
variables carry over, plots appear under the cell. Click a cell's language tag
to change it; click the triangle to run; click the x to delete the cell and
what it printed (Ctrl+Z brings both back). Paste or drop pictures straight in and
drag a corner to resize. Tab walks table cells.

While you type in a cell, a small list offers to finish the line with what the
document already holds: lines of its other cells, `code` in its text, what its
tables say, and names its code uses. Tab takes the first offer, the arrows and
Enter pick another, Esc puts the list away. Nothing comes from outside the
document.

A `sql` cell runs in that same session, on SQLite. The rows a query gives back
are printed under the cell and are `_` in the next Python cell (`_["score"]` is
one column, `_.columns` the names), and `:name` in a query is the Python
variable `name`. Tables live in a scratch database that lasts as long as the
session, unless a line `.open shop.db` in a cell (as in the `sqlite3` shell) or
the `DATABASE` variable names a file to keep them in.

Insert > Database starts one without writing any SQL: it asks for the file, the
table's name, its columns and what kind of data each holds, and puts the answer
in the document as a `sql` cell, which it runs.

Cell > Environment Variables holds `NAME=value` lines that every cell finds in
its environment, in any language. They are kept in Jotter's settings on this
computer, not in the document, so a password or a key stays out of the file.

Help > Check for Updates asks GitHub whether a newer release is out. The
Windows installer and the AppImage then fetch it and put it in their own place;
a build from source or an unpacked tarball is shown the download page. Nothing
is asked of the network before that is chosen.

The list down the left side shows every shortcut the menus have. Its close
button or View > Keyboard Shortcuts hides it, and Jotter remembers that. The
list down the right side shows the libraries the Python behind the cells can
`import`, with their versions: the ones installed, then the ones every Python
comes with. Type in its box to find one; View > Libraries hides it.

The font box in the toolbar (or Format > Font) sets the whole document in one of
a few faces that are part of Jotter: Roboto, Atkinson Hyperlegible, EB Garamond,
Source Serif, Computer Modern (the LaTeX look: text and formulas then match),
Courier Prime and Comic Relief. The choice is saved in the document and goes
into the PDF or HTML exported from it, on any machine. A new document starts in
the face picked last.

Script > Script Mode makes the document a screenplay: Courier, sixty letters to
the line, and paragraphs that are scene headings, action, who speaks, how (a
parenthetical), what they say, and transitions. Enter goes on to what usually
comes next (what is said after who says it, action after that), and Tab changes
what an empty line is: action, who speaks, a transition, a scene heading. At the
end of a line Tab starts the other thing: who speaks after action, a
parenthetical after a name or a speech. `int.` or `ext.` starts a scene heading,
names and headings are put in capitals, and a name or place that has been used
is offered again (Tab takes it). Ctrl+1 to Ctrl+6, or the style box, make the
line any of the six. Exported, it is laid out as a screenplay is on paper.

An existing `.md` opens as a document (File > Open, or `jotter file.md`). To pull
one into the document you are in: File > Import, or drop the file on the page
(dropped on an empty window, it opens instead).

## The file

`notes.md` is ordinary Markdown; pictures live beside it in `notes.assets/`.

````
```python                 a cell
print(6 * 7)
```

```output                 what it printed (```error for a traceback)
42
```

![output](notes.assets/out-3fa1c2d4e5f6.png)     a figure a cell produced

<!--cell python           a cell whose code is hidden
plt.plot(x, y)
-->

$e^{i\pi} = -1$           math, inline or $$display$$

<!--font: EB Garamond-->  the font the document is set in (last line; none for the default)
````

A screenplay ends in `<!--script-->`, and is otherwise written as
[Fountain](https://fountain.io) writes one:

````
INT. KITCHEN - NIGHT      a scene heading (.ANYTHING ELSE, with a full stop before it)

Rain on the window.       action

MARA                      who speaks, with what they say right under it
(not looking up)          how
You're late.

CUT TO:                   a transition (> ANYTHING NOT ENDING IN TO:)
````

Opening a document never runs anything; outputs are stored in the file.

## Source

| | |
| --- | --- |
| `src/model.*` | what a block is (code, output, heading...) and the spacing that follows from it |
| `src/markdown.*` | Markdown reader (md4c) and self-checking writer |
| `src/editor.cpp` | typing, mouse, clipboard, painting |
| `src/editor_commands.cpp` | what menus and shortcuts do; running cells |
| `src/render.*` | theme, syntax colours (KSyntaxHighlighting), spelling (hunspell) |
| `src/objects.*` | figures, formulas and hidden cells inside the text |
| `src/kernel.*`, `data/kernel_worker.py` | running cells; the Python session |
| `src/mathtex.*`, `data/mathtext_worker.py` | formulas to images: LaTeX, or matplotlib where there is none |
| `src/exporter.*`, `src/pdfhelper.cpp` | HTML and PDF export |
| `src/mainwindow.*` | window, menus, files, crash recovery |
| `packaging/` | the AppImage build, and the Windows installer (`packaging/windows/`) |
| `data/icons/` | Breeze icons (LGPL), used where the desktop's theme lacks one |
| `data/fonts/` | the text faces, built into the program; each folder has its licence (SIL Open Font License) |
