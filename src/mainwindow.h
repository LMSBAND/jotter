// The window around the page: menus, toolbar, find bar, files.
#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QTimer>

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QProcess;
class QTreeWidget;

namespace jot {

class Editor;
class Storyboard;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    void open(const QString &path);     // a file that does not exist yet becomes a new document with that name
    void offerRecovery();               // after a crash: bring back an unsaved, untitled document

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildUi();
    void addStoryboard(QMenu *view);
    void addFrame();
    void applySettings();
    void updateTitle();
    void updateState();
    void updateWords();
    void updateHideAll();

    void newWindow();
    void openDialog();
    void importDialog();
    void dropped(const QStringList &paths);
    bool pristine() const;
    bool save();
    bool saveAs();
    void page(QByteArray *text, QStringList *pictures) const;
    QString keep();                     // the page as it is now, into the document's history of saves
    void keepFound();
    void throughSaves(bool back);       // Undo and Redo, when they run out of what was typed
    bool maybeSave();
    void exportHtml();
    void exportPdf();
    QString documentTitle() const;
    void addRecent(const QString &path);
    void fillRecent();

    void showFind(bool replace);
    bool find(bool backward);
    void replaceOne();
    void replaceAll();

    void zoomBy(int steps);
    void pickFace(const QString &name);
    void updateFace();
    void setPart(int index);
    QString blank() const;
    void insertPicture();
    void insertTable();
    void insertPlot();
    void insertDatabase();
    void editEnvironment();
    void checkForUpdates();
    void loadLibraries();

    QString recoveryFile() const;
    void writeRecovery();
    void clearRecovery();
    bool restoreFrom(const QString &file, const QString &path);

    Editor *m_editor = nullptr;
    Storyboard *m_storyboard = nullptr;     // frames to doodle in, down the right side...
    QDockWidget *m_board = nullptr;         // ...and the panel it is in
    QString m_path;
    QString m_shown;                    // the save the page was last set to, going through them
    QByteArray m_found;                 // the document as it was opened, until it is known to be among its saves
    QStringList m_foundPictures;
    QSettings m_settings;
    QTimer m_recoveryTimer, m_wordTimer;
    QWidget *m_findBar = nullptr;
    QDockWidget *m_shortcuts = nullptr, *m_libraries = nullptr;
    QTreeWidget *m_libraryList = nullptr;
    QLabel *m_libraryPython = nullptr;
    QLineEdit *m_libraryFind = nullptr;
    QProcess *m_libraryLister = nullptr;
    QLineEdit *m_find = nullptr, *m_replace = nullptr;
    QComboBox *m_style = nullptr, *m_face = nullptr;
    QMenu *m_faces = nullptr, *m_script = nullptr;
    QLabel *m_words = nullptr, *m_kernel = nullptr;
    QMenu *m_recent = nullptr;
    QAction *m_bold = nullptr, *m_italic = nullptr, *m_underline = nullptr, *m_strike = nullptr, *m_code = nullptr,
            *m_bullets = nullptr, *m_numbers = nullptr, *m_checks = nullptr, *m_stop = nullptr, *m_dark = nullptr,
            *m_spell = nullptr, *m_hideAll = nullptr, *m_quote = nullptr, *m_scriptMode = nullptr;
};

}  // namespace jot
