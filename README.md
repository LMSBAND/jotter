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
| `packaging/` | the AppImage build |
| `data/icons/` | Breeze icons (LGPL), used where the desktop's theme lacks one |
