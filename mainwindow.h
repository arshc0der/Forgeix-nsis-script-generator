#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    // Window chrome
    void on_closeButton_clicked();
    void on_minimizeButton_clicked();
    void on_maximizeButton_clicked();

    // Files / folders to deploy
    void on_addFilesButton_clicked();
    void on_addFolderButton_clicked();
    void on_removeSelectedButton_clicked();

    // Resource pickers
    void on_SetupIconButton_clicked();
    void on_SetupUninstallIconButton_clicked();
    void on_SetupBPMButton_clicked();
    void on_SetupUninstallBPMButton_clicked();
    void on_browseBuildFolderButton_clicked();
    void on_defaultDirButton_clicked();
    void on_browseReadmeButton_clicked();
    void on_browseLicenseButton_clicked();
    void on_browseMainExeButton_clicked();

    // Toggles that enable/disable dependent fields
    void on_showReadmeCheckBox_toggled(bool checked);
    void on_openWebsiteCheckBox_toggled(bool checked);

    // Script actions
    void on_scriptSavePathButton_clicked();
    void on_saveScriptButton_clicked();
    void on_generateButton_clicked();

private:
    Ui::MainWindow *ui;

    // Frameless drag state
    bool m_dragging = false;
    QPoint m_dragPosition;
    bool m_isMaximized = false;
    QRect m_normalGeometry;
    QGraphicsOpacityEffect *m_opacityEffect;

    // Tree helpers
    void addFolderToTree(const QString &folder, bool recursive);

    // Script generation pipeline
    QString generateNsisScript();
    bool validateBeforeGenerate(QStringList &errors) const;
    QStringList collectMissingResourceFiles() const;

    // Security / correctness helpers
    static QString escapeNsisString(const QString &input);
    static QString sanitizeForRegistryKey(const QString &input);
    static QString sanitizeFileName(const QString &input);
    static QString toNsisPath(const QString &path);
    static QString normalizedFourPartVersion(const QString &version);
    static bool isSafeInstallDir(const QString &dir);
};

#endif // MAINWINDOW_H