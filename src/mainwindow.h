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

namespace jot {

class Editor;
class Storyboard;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
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
    QString blank() const;
    void insertPicture();
    void insertTable();
    void insertPlot();
    void insertDatabase();
    void editEnvironment();
    void checkForUpdates();

    QString recoveryFile() const;
    void writeRecovery();
    void clearRecovery();
    bool restoreFrom(const QString &file, const QString &path);

    Editor *m_editor = nullptr;
    Storyboard *m_storyboard = nullptr;     // frames to doodle in, down the right side...
    QDockWidget *m_board = nullptr;         // ...and the panel it is in
    QString m_path;
    QSettings m_settings;
    QTimer m_recoveryTimer, m_wordTimer;
    QWidget *m_findBar = nullptr;
    QDockWidget *m_shortcuts = nullptr;
    QLineEdit *m_find = nullptr, *m_replace = nullptr;
    QComboBox *m_style = nullptr, *m_face = nullptr;
    QMenu *m_faces = nullptr;
    QLabel *m_words = nullptr, *m_kernel = nullptr;
    QMenu *m_recent = nullptr;
    QAction *m_bold = nullptr, *m_italic = nullptr, *m_underline = nullptr, *m_strike = nullptr, *m_code = nullptr,
            *m_bullets = nullptr, *m_numbers = nullptr, *m_checks = nullptr, *m_stop = nullptr, *m_dark = nullptr,
            *m_spell = nullptr, *m_hideAll = nullptr;
};

}  // namespace jot
