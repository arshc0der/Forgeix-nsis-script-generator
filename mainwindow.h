#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

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
    void on_closeButton_clicked();
    void on_minimizeButton_clicked();
    void on_maximizeButton_clicked();

    void on_addFilesButton_clicked();

    void on_addFolderButton_clicked();

    void on_removeSelectedButton_clicked();

private:
    Ui::MainWindow *ui;

    bool m_dragging = false;
    QPoint m_dragPosition;

    bool m_isMaximized = false;
    QRect m_normalGeometry;

    QGraphicsOpacityEffect *m_opacityEffect;
    void addFolderToTree(const QString &folder,
                         bool recursive);
};

#endif // MAINWINDOW_H