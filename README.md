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

Built this way it uses the machine's `python3` for Python cells (with whatever
numpy/matplotlib that Python has), `pdflatex` + `pdftocairo` for math (or
matplotlib if there is no LaTeX), and the tools you already have for other
languages (bash, node, lean, dot, gcc...). Anything missing just means that one
kind of cell does not run.

## One file for other machines

```
packaging/appimage.sh       # needs Docker; writes dist/Jotter-x86_64.AppImage
```

The AppImage carries Qt, the PDF engine, a Python with numpy and matplotlib, an
English dictionary and its icons, so it runs on a machine with none of that:
`chmod +x Jotter-x86_64.AppImage`, then run it. It is built in an Ubuntu 22.04
container and runs there and on anything newer. LaTeX is not inside (too big):
where `pdflatex` exists formulas are set with it, elsewhere by a built-in
renderer that handles ordinary formulas but not matrices or `aligned`. If the
machine's own `python3` already has numpy and matplotlib, cells use that one
(and so see the other packages installed there) instead of the shipped one.

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
to change it; click the triangle to run. Paste or drop pictures straight in and
drag a corner to resize. Tab walks table cells.

The font box in the toolbar (or Format > Font) sets the whole document in one of
a few faces that are part of Jotter: Roboto, Atkinson Hyperlegible, EB Garamond,
Source Serif, Computer Modern (the LaTeX look: text and formulas then match),
Courier Prime and Comic Relief. The choice is saved in the document and goes
into the PDF or HTML exported from it, on any machine. A new document starts in
the face picked last.

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
