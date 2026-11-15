#include "mainwindow.h"

#include "editor.h"
#include "exporter.h"
#include "markdown.h"
#include "render.h"

#include <QAbstractItemView>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStylePainter>
#include <QStyleOptionComboBox>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace jot {

namespace {

const char *kPlot = "import numpy as np\n"
                    "import matplotlib.pyplot as plt\n"
                    "\n"
                    "x = np.linspace(0, 10, 200)\n"
                    "plt.plot(x, np.sin(x))\n"
                    "plt.xlabel(\"x\")\n"
                    "plt.ylabel(\"sin(x)\")";

QString recoveryDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/jotter/recovery";
    QDir().mkpath(dir);
    return dir;
}

// The font box in the toolbar. Its list shows each face in its own letters,
// which takes having them all loaded: that waits until the list is opened.
class FaceBox : public QComboBox
{
public:
    explicit FaceBox(QWidget *parent) : QComboBox(parent)
    {
        for (const Face &face : faces())
            addItem(face.name);
        setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        setMinimumContentsLength(11);       // leaves room on the toolbar: a longer name is shortened (see paintEvent)
    }
    void showPopup() override
    {
        if (!itemData(0, Qt::FontRole).isValid()) {
            for (int i = 0; i < count(); ++i)
                setItemData(i, fontOf(faceNamed(itemText(i)), QFontInfo(font()).pointSizeF()), Qt::FontRole);
            view()->setMinimumWidth(view()->sizeHintForColumn(0) + 28);
        }
        QComboBox::showPopup();
    }

protected:
    void paintEvent(QPaintEvent *) override     // as QComboBox paints itself, but a name too long for the box ends in "..."
    {
        QStylePainter painter(this);
        painter.setPen(palette().color(QPalette::Text));
        QStyleOptionComboBox option;
        initStyleOption(&option);
        const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
        option.currentText = fontMetrics().elidedText(option.currentText, Qt::ElideRight, field.width() - 2);
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        painter.drawControl(QStyle::CE_ComboBoxLabel, option);
    }
};

// Is the process with this id a Jotter that is still running?
bool jotterRuns(const QString &pid)
{
#ifdef Q_OS_WIN
    bool runs = false;
    if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid.toULong())) {
        wchar_t name[MAX_PATH];
        DWORD size = MAX_PATH, code = 0;
        runs = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE && QueryFullProcessImageNameW(process, 0, name, &size)
            && QFileInfo(QString::fromWCharArray(name, int(size))).completeBaseName().compare("jotter", Qt::CaseInsensitive) == 0;
        CloseHandle(process);
    }
    return runs;
#else
    QFile comm("/proc/" + pid + "/comm");
    return comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "jotter";
#endif
}

}  // namespace

MainWindow::MainWindow()
{
    m_editor = new Editor(this);
    buildUi();
    applySettings();
    if (!blank().isEmpty())
        m_editor->loadMarkdown(blank());
    restoreGeometry(m_settings.value("geometry").toByteArray());
    if (!m_settings.contains("geometry"))
        resize(1100, 900);

    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &QWidget::setWindowModified);
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, &MainWindow::updateState);
    connect(m_editor, &QTextEdit::currentCharFormatChanged, this, &MainWindow::updateState);
    connect(m_editor, &Editor::cellsChanged, this, &MainWindow::updateState);
    connect(m_editor, &Editor::faceChanged, this, &MainWindow::updateFace);
    connect(m_editor, &Editor::status, this, [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    connect(m_editor, &Editor::zoomRequested, this, &MainWindow::zoomBy);
    connect(m_editor, &Editor::filesDropped, this, &MainWindow::dropped);
    connect(m_editor->document(), &QTextDocument::contentsChanged, &m_wordTimer, QOverload<>::of(&QTimer::start));
    connect(m_editor, &QTextEdit::selectionChanged, &m_wordTimer, QOverload<>::of(&QTimer::start));
    m_wordTimer.setSingleShot(true);
    m_wordTimer.setInterval(400);
    connect(&m_wordTimer, &QTimer::timeout, this, &MainWindow::updateWords);
    m_recoveryTimer.setInterval(30000);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &MainWindow::writeRecovery);
    m_recoveryTimer.start();

    updateTitle();
    updateState();
    updateFace();
    updateWords();
    m_editor->setFocus();
}

// ---- menus and toolbar --------------------------------------------------------

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_editor, 1);
    setCentralWidget(central);

    // find bar
    m_findBar = new QWidget(central);
    auto *row = new QHBoxLayout(m_findBar);
    row->setContentsMargins(8, 4, 8, 4);
    m_find = new QLineEdit(m_findBar);
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(true);
    m_replace = new QLineEdit(m_findBar);
    m_replace->setPlaceholderText(tr("Replace with"));
    auto button = [this, row](const QString &iconName, const QString &text, const QString &tip, auto slot) {
        auto *b = new QToolButton(m_findBar);
        const QIcon picture = icon(iconName);
        if (picture.isNull())
            b->setText(text);
        else
            b->setIcon(picture);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, slot);
        row->addWidget(b);
        return b;
    };
    row->addWidget(m_find, 2);
    button("go-up", tr("Previous"), tr("Previous match (Shift+Enter)"), [this] { find(true); });
    button("go-down", tr("Next"), tr("Next match (Enter)"), [this] { find(false); });
    row->addSpacing(12);
    row->addWidget(m_replace, 2);
    button(QString(), tr("Replace"), tr("Replace this match"), [this] { replaceOne(); });
    button(QString(), tr("All"), tr("Replace every match"), [this] { replaceAll(); });
    row->addStretch(1);
    button("window-close", tr("Close"), tr("Close (Esc)"), [this] {
        m_findBar->hide();
        m_editor->setFocus();
    });
    column->addWidget(m_findBar);
    m_findBar->hide();
    connect(m_find, &QLineEdit::returnPressed, this, [this] { find(false); });
    connect(m_replace, &QLineEdit::returnPressed, this, [this] { replaceOne(); });
    auto *prev = new QShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Return), m_find, nullptr, nullptr, Qt::WidgetShortcut);
    connect(prev, &QShortcut::activated, this, [this] { find(true); });
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), m_findBar, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, [this] {
        m_findBar->hide();
        m_editor->setFocus();
    });

    // One helper for every menu entry: text, icon, shortcut, what it does.
    auto add = [this](QMenu *menu, const QString &text, const QString &iconName, const QKeySequence &key, auto slot) {
        QAction *a = menu->addAction(icon(iconName), text);
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    Editor *ed = m_editor;

    QMenu *file = menuBar()->addMenu(tr("&File"));
    add(file, tr("&New Window"), "window-new", QKeySequence::New, [this] { newWindow(); });
    add(file, tr("&Open…"), "document-open", QKeySequence::Open, [this] { openDialog(); });
    m_recent = file->addMenu(icon("document-open-recent"), tr("Open &Recent"));
    connect(m_recent, &QMenu::aboutToShow, this, &MainWindow::fillRecent);
    add(file, tr("&Import…"), "document-import", QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_O), [this] { importDialog(); });
    file->addSeparator();
    add(file, tr("&Save"), "document-save", QKeySequence::Save, [this] { save(); });
    add(file, tr("Save &As…"), "document-save-as", QKeySequence::SaveAs, [this] { saveAs(); });
    file->addSeparator();
    add(file, tr("Export as &PDF…"), "application-pdf", QKeySequence(Qt::CTRL + Qt::Key_P), [this] { exportPdf(); });
    add(file, tr("Export as &HTML…"), "text-html", QKeySequence(), [this] { exportHtml(); });
    file->addSeparator();
    add(file, tr("&Close"), "window-close", QKeySequence::Close, [this] { close(); });

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    add(edit, tr("&Undo"), "edit-undo", QKeySequence::Undo, [ed] { ed->undoStep(); });
    add(edit, tr("&Redo"), "edit-redo", QKeySequence::Redo, [ed] { ed->redoStep(); });
    edit->addSeparator();
    add(edit, tr("Cu&t"), "edit-cut", QKeySequence::Cut, [ed] { ed->cut(); });
    add(edit, tr("&Copy"), "edit-copy", QKeySequence::Copy, [ed] { ed->copy(); });
    add(edit, tr("&Paste"), "edit-paste", QKeySequence::Paste, [ed] { ed->paste(); });
    add(edit, tr("Paste as Plain &Text"), QString(), QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_V), [ed] { ed->pastePlain(); });
    add(edit, tr("Paste &Markdown"), QString(), QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_V), [ed] { ed->pasteMarkdown(); });
    add(edit, tr("Select &All"), "edit-select-all", QKeySequence::SelectAll, [ed] { ed->selectAll(); });
    edit->addSeparator();
    add(edit, tr("&Find…"), "edit-find", QKeySequence::Find, [this] { showFind(false); });
    add(edit, tr("R&eplace…"), "edit-find-replace", QKeySequence(Qt::CTRL + Qt::Key_H), [this] { showFind(true); });

    QMenu *insert = menuBar()->addMenu(tr("&Insert"));
    QAction *cell = add(insert, tr("&Code Cell"), "code-context", QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_C), [ed] { ed->insertCell(); });
    QAction *plot = add(insert, tr("&Plot"), "office-chart-line", QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_P), [this] { insertPlot(); });
    QAction *picture = add(insert, tr("P&icture…"), "insert-image", QKeySequence(), [this] { insertPicture(); });
    QAction *table = add(insert, tr("&Table…"), "insert-table", QKeySequence(), [this] { insertTable(); });
    QAction *formula = add(insert, tr("&Formula…"), "insert-math-expression", QKeySequence(Qt::CTRL + Qt::Key_M), [ed] { ed->editMath(); });
    QAction *link = add(insert, tr("&Link…"), "insert-link", QKeySequence(Qt::CTRL + Qt::Key_K), [ed] { ed->editLink(); });
    add(insert, tr("Horizontal &Rule"), "insert-horizontal-rule", QKeySequence(), [ed] { ed->insertRule(); });
    insert->addSeparator();
    add(insert, tr("Paragraph &Below"), QString(), QKeySequence(Qt::ALT + Qt::Key_Return), [ed] { ed->paragraphBelow(); });

    QMenu *format = menuBar()->addMenu(tr("F&ormat"));
    m_faces = format->addMenu(icon("preferences-desktop-font"), tr("&Font"));
    auto *faceGroup = new QActionGroup(this);
    for (const Face &face : faces()) {
        QAction *a = add(m_faces, face.name, QString(), QKeySequence(), [this, name = face.name] { pickFace(name); });
        a->setData(face.name);
        a->setCheckable(true);
        faceGroup->addAction(a);
    }
    connect(m_faces, &QMenu::aboutToShow, this, [this] {        // each in its own letters, as in the toolbar's list
        for (QAction *a : m_faces->actions())
            a->setFont(fontOf(faceNamed(a->data().toString()), QFontInfo(m_faces->font()).pointSizeF()));
    });
    format->addSeparator();
    add(format, tr("Body &Text"), QString(), QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_0), [ed] { ed->setBlockKind(Para); });
    for (int level = 1; level <= 3; ++level)
        add(format, tr("Heading &%1").arg(level), QString(), QKeySequence(Qt::CTRL + Qt::ALT + (Qt::Key_0 + level)),
            [ed, level] { ed->setBlockKind(Heading, level); });
    QAction *quote = add(format, tr("&Quote"), "format-text-blockquote", QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Q), [ed] { ed->setBlockKind(Quote); });
    add(format, tr("&Code Block"), "format-text-code", QKeySequence(), [ed] { ed->setBlockKind(Code); });
    format->addSeparator();
    m_bold = add(format, tr("&Bold"), "format-text-bold", QKeySequence::Bold, [ed] { ed->toggleStyle(Bold); });
    m_italic = add(format, tr("&Italic"), "format-text-italic", QKeySequence::Italic, [ed] { ed->toggleStyle(Italic); });
    m_underline = add(format, tr("&Underline"), "format-text-underline", QKeySequence::Underline, [ed] { ed->toggleStyle(Underline); });
    m_strike = add(format, tr("&Strikethrough"), "format-text-strikethrough", QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_X), [ed] { ed->toggleStyle(Strike); });
    m_code = add(format, tr("Inline C&ode"), "format-text-code", QKeySequence(Qt::CTRL + Qt::Key_E), [ed] { ed->toggleStyle(CodeSpan); });
    format->addSeparator();
    m_bullets = add(format, tr("Bulleted &List"), "format-list-unordered", QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_8), [ed] { ed->toggleList(false); });
    m_numbers = add(format, tr("&Numbered List"), "format-list-ordered", QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_7), [ed] { ed->toggleList(true); });
    m_checks = add(format, tr("Chec&klist"), "checkbox", QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_9), [ed] { ed->toggleList(false, true); });
    format->addSeparator();
    add(format, tr("Clear &Formatting"), "edit-clear", QKeySequence(Qt::CTRL + Qt::Key_Backslash), [ed] { ed->clearFormatting(); });
    for (QAction *a : {m_bold, m_italic, m_underline, m_strike, m_code, m_bullets, m_numbers, m_checks})
        a->setCheckable(true);

    QMenu *cells = menuBar()->addMenu(tr("&Cell"));
    QAction *run = add(cells, tr("&Run"), "media-playback-start", QKeySequence(Qt::CTRL + Qt::Key_Return), [ed] { ed->runCell(false); });
    add(cells, tr("Run and &Move On"), QString(), QKeySequence(Qt::SHIFT + Qt::Key_Return), [ed] { ed->runCell(true); });
    QAction *runAll = add(cells, tr("Run &All"), "media-seek-forward", QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Return), [ed] { ed->runAll(); });
    m_stop = add(cells, tr("&Stop"), "media-playback-stop", QKeySequence(Qt::CTRL + Qt::Key_Period), [ed] { ed->interrupt(); });
    QAction *restart = add(cells, tr("Restart &Python"), "view-refresh", QKeySequence(), [ed] { ed->restartKernel(); });
    cells->addSeparator();
    add(cells, tr("&Hide / Show Code"), "view-hidden", QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_H), [ed] { ed->toggleFold(); });
    m_hideAll = add(cells, tr("Hide All Code"), "view-hidden", QKeySequence(), [this] {
        m_editor->foldAll(m_editor->hasVisibleCode());      // anything showing: hide it all; otherwise bring it back
        updateHideAll();
    });
    add(cells, tr("&Clear Output"), QString(), QKeySequence(), [ed] { ed->clearOutputs(false); });
    add(cells, tr("Clear All &Outputs"), QString(), QKeySequence(), [ed] { ed->clearOutputs(true); });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    m_dark = add(view, tr("&Dark Paper"), QString(), QKeySequence(), [this](bool on) {
        m_settings.setValue("dark", on);
        applySettings();
    });
    m_dark->setCheckable(true);
    m_spell = add(view, tr("Check &Spelling"), "tools-check-spelling", QKeySequence(), [this](bool on) {
        m_settings.setValue("spell", on);
        applySettings();
    });
    m_spell->setCheckable(true);
    view->addSeparator();
    add(view, tr("Zoom &In"), "zoom-in", QKeySequence(), [this] { zoomBy(1); })
        ->setShortcuts({QKeySequence(Qt::CTRL + Qt::Key_Equal), QKeySequence(Qt::CTRL + Qt::Key_Plus)});
    add(view, tr("Zoom &Out"), "zoom-out", QKeySequence(Qt::CTRL + Qt::Key_Minus), [this] { zoomBy(-1); });
    add(view, tr("&Actual Size"), "zoom-original", QKeySequence(Qt::CTRL + Qt::Key_0), [this] { zoomBy(0); });

    // Editing shortcuts belong to the page: Shift+Enter in the find box must not run a cell.
    for (QMenu *menu : {insert, format, cells}) {
        for (QAction *a : menu->actions()) {
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            m_editor->addAction(a);
        }
    }

    // toolbar: the things reached for while writing
    QToolBar *bar = addToolBar(tr("Tools"));
    bar->setObjectName("tools");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_style = new QComboBox(bar);
    m_style->addItems({tr("Body Text"), tr("Heading 1"), tr("Heading 2"), tr("Heading 3"), tr("Quote"), tr("Code")});
    m_style->setToolTip(tr("Paragraph style"));
    m_style->setFocusPolicy(Qt::NoFocus);
    connect(m_style, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i == 0)
            m_editor->setBlockKind(Para);
        else if (i <= 3)
            m_editor->setBlockKind(Heading, i);
        else if (m_editor->currentKind() != (i == 4 ? Quote : Code))
            m_editor->setBlockKind(i == 4 ? Quote : Code);
        m_editor->setFocus();
    });
    bar->addWidget(m_style);
    m_face = new FaceBox(bar);
    m_face->setToolTip(tr("Font of the whole document"));
    m_face->setFocusPolicy(Qt::NoFocus);
    connect(m_face, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { pickFace(m_face->itemText(i)); });
    bar->addWidget(m_face);
    bar->addSeparator();
    bar->addActions({m_bold, m_italic, m_underline, m_strike, m_code});
    bar->addSeparator();
    bar->addActions({m_bullets, m_numbers, m_checks, quote});
    bar->addSeparator();
    bar->addActions({cell, plot, picture, table, formula, link});
    bar->addSeparator();
    bar->addActions({run, runAll, m_stop, restart});
    bar->addSeparator();
    bar->addAction(m_hideAll);
    if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(m_hideAll)))
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);      // this one says what it does

    m_kernel = new QLabel(this);
    m_words = new QLabel(this);
    statusBar()->addPermanentWidget(m_kernel);
    statusBar()->addPermanentWidget(m_words);
    connect(m_editor->kernel(), &Kernel::stateChanged, this, [this](const QString &state) {
        m_kernel->setText(state == "busy" ? tr("Python: running…") : state == "idle" ? tr("Python: ready") : QString());
    });
}

void MainWindow::applySettings()
{
    Theme th(m_settings.contains("dark") ? m_settings.value("dark").toBool() : Theme::systemIsDark());
    th.zoom = qBound(0.6, m_settings.value("zoom", 1.0).toDouble(), 2.5);
    th.size = qBound(8.0, m_settings.value("fontSize", 11.0).toDouble(), 24.0);
    m_editor->theme = th;
    m_editor->applyTheme();
    m_editor->renderer()->setSpellCheck(m_settings.value("spell", true).toBool());
    m_editor->kernel()->python = m_settings.value("python").toString();     // empty: Jotter chooses
    m_dark->setChecked(th.dark);
    m_spell->setChecked(m_editor->renderer()->spellCheck());
}

void MainWindow::updateTitle()
{
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
    setWindowTitle(name + QString::fromUtf8("[*] \xE2\x80\x94 Jotter"));
    setWindowFilePath(m_path);
    setWindowModified(m_editor->document()->isModified());
}

void MainWindow::updateState()
{
    const Kind kind = m_editor->currentKind();
    const int level = m_editor->currentHeading();
    m_style->setCurrentIndex(kind == Heading ? qBound(1, level, 3) : kind == Quote ? 4 : (kind == Code || kind == Output || kind == Raw) ? 5 : 0);
    m_bold->setChecked(m_editor->hasStyle(Bold));
    m_italic->setChecked(m_editor->hasStyle(Italic));
    m_underline->setChecked(m_editor->hasStyle(Underline));
    m_strike->setChecked(m_editor->hasStyle(Strike));
    m_code->setChecked(m_editor->hasStyle(CodeSpan));
    const int list = m_editor->listState();
    m_bullets->setChecked(list == 1);
    m_numbers->setChecked(list == 2);
    m_checks->setChecked(list == 3);
    m_stop->setEnabled(m_editor->cellBusy());
}

void MainWindow::updateHideAll()
{
    const bool visible = m_editor->hasVisibleCode(), hidden = m_editor->hasHiddenCode();
    const bool showing = !visible && hidden;        // everything is hidden: the button brings it back
    m_hideAll->setText(showing ? tr("Show All Code") : tr("Hide All Code"));
    m_hideAll->setIcon(icon(showing ? "view-visible" : "view-hidden"));
}

void MainWindow::updateWords()
{
    updateHideAll();        // same moment: the document just changed
    static const QRegularExpression word(QStringLiteral(R"(\w[\w'\x{2019}-]*)"), QRegularExpression::UseUnicodePropertiesOption);
    auto count = [](const QString &text) {
        int n = 0;
        auto it = word.globalMatch(text);
        while (it.hasNext()) {
            it.next();
            ++n;
        }
        return n;
    };
    const QLocale locale;
    const int total = count(m_editor->document()->toPlainText());
    const QTextCursor c = m_editor->textCursor();
    if (c.hasSelection())
        m_words->setText(tr("%1 of %2 words").arg(locale.toString(count(c.selectedText())), locale.toString(total)));
    else
        m_words->setText(tr("%1 words").arg(locale.toString(total)));
}

// ---- files --------------------------------------------------------------------

void MainWindow::newWindow()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
}

void MainWindow::open(const QString &path)
{
    QString text = blank();
    QFile file(path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Jotter"), tr("Could not open %1:\n%2").arg(path, file.errorString()));
            return;
        }
        text = QString::fromUtf8(file.readAll());
    }
    m_path = QFileInfo(path).absoluteFilePath();
    m_editor->loadMarkdown(text, m_path);
    if (file.exists())
        addRecent(m_path);
    updateTitle();
    updateWords();

    // Did Jotter go down with unsaved changes to this document?
    const QString recovery = recoveryFile();
    if (QFile::exists(recovery)) {
        if (QFileInfo(recovery).lastModified() > QFileInfo(m_path).lastModified()
            && QMessageBox::question(this, tr("Jotter"),
                                     tr("Jotter closed without saving changes to this document. Restore them?"),
                                     QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
            restoreFrom(recovery, m_path);
        QFile::remove(recovery);
    }
}

void MainWindow::openDialog()
{
    const QString dir = m_path.isEmpty() ? m_settings.value("dir", QDir::homePath()).toString() : QFileInfo(m_path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open"), dir, tr("Markdown (*.md *.markdown *.txt);;All files (*)"));
    if (path.isEmpty())
        return;
    m_settings.setValue("dir", QFileInfo(path).absolutePath());
    if (pristine())
        open(path);
    else
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {path});
}

bool MainWindow::pristine() const       // an empty, untitled, untouched window
{
    return m_path.isEmpty() && !m_editor->document()->isModified() && m_editor->document()->isEmpty();
}

// File > Import: another Markdown file's content, into this document.
void MainWindow::importDialog()
{
    const QString dir = m_path.isEmpty() ? m_settings.value("dir", QDir::homePath()).toString() : QFileInfo(m_path).absolutePath();
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Import"), dir, tr("Markdown and text (*.md *.markdown *.txt);;All files (*)"));
    for (const QString &path : paths)
        m_editor->importFile(path);
}

// A document dragged onto the page (or copied in the file manager and pasted):
// an empty window becomes that document, a window with work in it takes the
// file's content at that spot.
void MainWindow::dropped(const QStringList &paths)
{
    QStringList rest = paths;
    if (pristine())
        open(rest.takeFirst());
    for (const QString &path : rest)
        m_editor->importFile(path);
}

bool MainWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    QString error;
    if (!m_editor->save(m_path, &error)) {
        QMessageBox::warning(this, tr("Jotter"), tr("Could not save %1:\n%2").arg(m_path, error));
        return false;
    }
    clearRecovery();
    addRecent(m_path);
    statusBar()->showMessage(tr("Saved"), 2500);
    return true;
}

bool MainWindow::saveAs()
{
    const QString dir = m_path.isEmpty() ? m_settings.value("dir", QDir::homePath()).toString() + "/" + documentTitle() + ".md" : m_path;
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), dir, tr("Markdown (*.md)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".md";
    clearRecovery();        // it is filed under the old name
    m_settings.setValue("dir", QFileInfo(path).absolutePath());
    m_path = QFileInfo(path).absoluteFilePath();
    updateTitle();
    return save();
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;
    const auto answer = QMessageBox::warning(this, tr("Jotter"), tr("Save changes to %1?").arg(m_path.isEmpty() ? tr("this document") : QFileInfo(m_path).fileName()),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    m_settings.setValue("geometry", saveGeometry());
    clearRecovery();
    m_editor->kernel()->shutdown();
    m_editor->store().tidy();
    e->accept();
}

QString MainWindow::documentTitle() const
{
    for (QTextBlock b = m_editor->document()->firstBlock(); b.isValid(); b = b.next()) {
        if (kindOf(b) == Heading) {
            QString t = b.text();
            t.remove(QChar::ObjectReplacementCharacter).remove(QRegularExpression(QStringLiteral(R"([/\\:*?"<>|])")));
            if (!t.trimmed().isEmpty())
                return t.trimmed();
        }
    }
    return m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName();
}

void MainWindow::addRecent(const QString &path)
{
    QStringList recent = m_settings.value("recent").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    m_settings.setValue("recent", QStringList(recent.mid(0, 12)));
}

void MainWindow::fillRecent()
{
    m_recent->clear();
    for (const QString &path : m_settings.value("recent").toStringList()) {
        if (!QFile::exists(path))
            continue;
        m_recent->addAction(QDir::toNativeSeparators(path).replace(QDir::homePath(), "~"), this, [this, path] {
            if (pristine())
                open(path);
            else
                QProcess::startDetached(QCoreApplication::applicationFilePath(), {path});
        });
    }
    if (m_recent->isEmpty())
        m_recent->addAction(tr("Nothing yet"))->setEnabled(false);
}

void MainWindow::exportHtml()
{
    const QString base = m_path.isEmpty() ? QDir::homePath() + "/" + documentTitle() : QFileInfo(m_path).absolutePath() + "/" + QFileInfo(m_path).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, tr("Export as HTML"), base + ".html", tr("HTML (*.html)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".html";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(pageHtml(m_editor->document(), m_editor->store(), documentTitle()).toUtf8()) < 0
        || !file.commit()) {
        QMessageBox::warning(this, tr("Jotter"), tr("Could not write %1:\n%2").arg(path, file.errorString()));
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(path), 6000);
}

void MainWindow::exportPdf()
{
    if (!canExportPdf()) {
        QMessageBox::information(this, tr("Jotter"), tr("PDF export needs either the jotter-pdf helper next to Jotter (it is built "
                                                        "when the Qt WebEngine development package is installed), or Chrome, "
                                                        "Chromium or Edge."));
        return;
    }
    const QString base = m_path.isEmpty() ? QDir::homePath() + "/" + documentTitle() : QFileInfo(m_path).absolutePath() + "/" + QFileInfo(m_path).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, tr("Export as PDF"), base + ".pdf", tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".pdf";
    const QString paper = QLocale().measurementSystem() == QLocale::MetricSystem ? "a4" : "letter";
    statusBar()->showMessage(tr("Exporting PDF…"));
    jot::exportPdf(m_editor->document(), m_editor->store(), documentTitle(), path, paper, this, [this, path](bool ok) {
        statusBar()->showMessage(ok ? tr("Exported %1").arg(path) : tr("PDF export failed"), 8000);
    });
}

// ---- find ---------------------------------------------------------------------

void MainWindow::showFind(bool replace)
{
    m_findBar->show();
    const QTextCursor c = m_editor->textCursor();
    if (c.hasSelection() && !c.selectedText().contains(QChar::ParagraphSeparator))
        m_find->setText(c.selectedText());
    QLineEdit *target = replace && !m_find->text().isEmpty() ? m_replace : m_find;
    target->setFocus();
    target->selectAll();
}

bool MainWindow::find(bool backward)
{
    const QString text = m_find->text();
    if (text.isEmpty())
        return false;
    const QTextDocument::FindFlags flags = backward ? QTextDocument::FindBackward : QTextDocument::FindFlags();
    if (m_editor->find(text, flags))
        return true;
    const QTextCursor saved = m_editor->textCursor();   // wrap around
    QTextCursor c = saved;
    c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    m_editor->setTextCursor(c);
    if (m_editor->find(text, flags))
        return true;
    m_editor->setTextCursor(saved);
    statusBar()->showMessage(tr("Not found: %1").arg(text), 3000);
    return false;
}

void MainWindow::replaceOne()
{
    QTextCursor c = m_editor->textCursor();
    if (c.hasSelection() && c.selectedText().compare(m_find->text(), Qt::CaseInsensitive) == 0)
        c.insertText(m_replace->text());
    find(false);
}

void MainWindow::replaceAll()
{
    const QString text = m_find->text();
    if (text.isEmpty())
        return;
    QTextDocument *doc = m_editor->document();
    QTextCursor edit(doc);
    edit.beginEditBlock();
    int n = 0;
    for (QTextCursor hit = doc->find(text, 0); !hit.isNull(); hit = doc->find(text, hit)) {
        hit.insertText(m_replace->text());
        ++n;
    }
    edit.endEditBlock();
    statusBar()->showMessage(tr("Replaced %n occurrence(s)", nullptr, n), 4000);
}

// ---- view and insert ------------------------------------------------------------

void MainWindow::zoomBy(int steps)
{
    const qreal zoom = steps == 0 ? 1.0 : qBound(0.6, m_editor->theme.zoom + 0.1 * steps, 2.5);
    m_settings.setValue("zoom", zoom);
    m_editor->theme.zoom = zoom;
    m_editor->applyTheme();
    statusBar()->showMessage(tr("Zoom %1%").arg(qRound(zoom * 100)), 1500);
}

// The text face belongs to the document (it is saved in it). A new document
// starts in the one that was picked last.
void MainWindow::pickFace(const QString &name)
{
    m_editor->setFace(name);
    m_settings.setValue("font", m_editor->face());
    updateFace();
    m_editor->setFocus();
}

void MainWindow::updateFace()
{
    const QString name = m_editor->face();
    m_face->setCurrentIndex(qMax(0, m_face->findText(name)));
    for (QAction *a : m_faces->actions())
        a->setChecked(a->data().toString() == name);
}

QString MainWindow::blank() const
{
    return faceComment(m_settings.value("font").toString());
}

void MainWindow::insertPicture()
{
    const QString dir = m_settings.value("pictureDir", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Insert Picture"), dir, tr("Pictures (*.png *.jpg *.jpeg *.gif *.svg *.webp *.bmp);;All files (*)"));
    if (path.isEmpty())
        return;
    m_settings.setValue("pictureDir", QFileInfo(path).absolutePath());
    m_editor->insertImageFile(path);
}

void MainWindow::insertTable()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Insert Table"));
    auto *form = new QFormLayout(&dialog);
    auto *rows = new QSpinBox(&dialog), *columns = new QSpinBox(&dialog);
    rows->setRange(1, 200);
    rows->setValue(3);
    columns->setRange(1, 20);
    columns->setValue(3);
    form->addRow(tr("Rows (the first is the header):"), rows);
    form->addRow(tr("Columns:"), columns);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        m_editor->insertTable(rows->value(), columns->value());
}

void MainWindow::insertPlot()
{
    m_editor->insertCell(QString::fromLatin1(kPlot), "python", true);
}

// ---- crash recovery -------------------------------------------------------------
// While there are unsaved changes, a copy is kept outside the document. A clean
// save or close removes it; finding one later means Jotter (or the machine) went
// down, and the changes are offered back.

QString MainWindow::recoveryFile() const
{
    if (m_path.isEmpty())
        return recoveryDir() + QString("/untitled-%1.md").arg(QCoreApplication::applicationPid());
    return recoveryDir() + "/" + QString::fromLatin1(QCryptographicHash::hash(m_path.toUtf8(), QCryptographicHash::Sha1).toHex()) + ".md";
}

void MainWindow::writeRecovery()
{
    if (!m_editor->document()->isModified())
        return;
    QSaveFile file(recoveryFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_editor->toMarkdown(true).toUtf8());    // pictures by absolute path
        file.commit();
    }
}

void MainWindow::clearRecovery()
{
    QFile::remove(recoveryFile());
}

bool MainWindow::restoreFrom(const QString &file, const QString &path)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    m_editor->loadMarkdown(QString::fromUtf8(f.readAll()), path, true);
    m_editor->document()->setModified(true);
    updateTitle();
    updateWords();
    return true;
}

void MainWindow::offerRecovery()
{
    QFileInfoList leftovers;
    for (const QFileInfo &fi : QDir(recoveryDir()).entryInfoList({"untitled-*.md"}, QDir::Files, QDir::Time)) {
        if (jotterRuns(fi.completeBaseName().section('-', 1)))
            continue;       // that window is still open
        leftovers.append(fi);
    }
    if (leftovers.isEmpty())
        return;
    const QFileInfo newest = leftovers.first();
    const auto answer = QMessageBox::question(this, tr("Jotter"),
                                              tr("An unsaved document from %1 was left behind when Jotter closed unexpectedly. Restore it?")
                                                  .arg(QLocale().toString(newest.lastModified(), QLocale::ShortFormat)),
                                              QMessageBox::Yes | QMessageBox::No);
    if (answer == QMessageBox::Yes)
        restoreFrom(newest.absoluteFilePath(), QString());
    for (const QFileInfo &fi : leftovers)
        QFile::remove(fi.absoluteFilePath());
}

}  // namespace jot
