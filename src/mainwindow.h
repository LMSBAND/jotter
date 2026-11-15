// The window around the page: menus, toolbar, find bar, files.
#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QTimer>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;

namespace jot {

class Editor;

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
    void chooseFont();
    void insertPicture();
    void insertTable();
    void insertPlot();

    QString recoveryFile() const;
    void writeRecovery();
    void clearRecovery();
    bool restoreFrom(const QString &file, const QString &path);

    Editor *m_editor = nullptr;
    QString m_path;
    QSettings m_settings;
    QTimer m_recoveryTimer, m_wordTimer;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_find = nullptr, *m_replace = nullptr;
    QComboBox *m_style = nullptr;
    QLabel *m_words = nullptr, *m_kernel = nullptr;
    QMenu *m_recent = nullptr;
    QAction *m_bold = nullptr, *m_italic = nullptr, *m_underline = nullptr, *m_strike = nullptr, *m_code = nullptr,
            *m_bullets = nullptr, *m_numbers = nullptr, *m_checks = nullptr, *m_stop = nullptr, *m_dark = nullptr,
            *m_spell = nullptr, *m_hideAll = nullptr;
};

}  // namespace jot
