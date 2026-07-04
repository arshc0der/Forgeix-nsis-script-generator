#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QScreen>
#include <QGuiApplication>
#include <QEasingCurve>
#include <QMessageBox>
#include <QFileDialog>
#include <QDirIterator>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 🍎 Frameless + transparent background
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // 🍎 Fade effect setup
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacityEffect);
    m_opacityEffect->setOpacity(1.0);

    ui->filesTreeWidget->setColumnCount(2);

    QStringList headers;
    headers << "Name" << "Path";

    ui->filesTreeWidget->setHeaderLabels(headers);
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ===================== DRAG SYSTEM =====================

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }

    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
    {
        if (m_isMaximized)
        {
            m_isMaximized = false;
            setGeometry(m_normalGeometry);
        }

        move(event->globalPosition().toPoint() - m_dragPosition);
    }

    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QMainWindow::mouseReleaseEvent(event);
}

// ===================== CLOSE (FADE ANIMATION) =====================

void MainWindow::on_closeButton_clicked()
{
    QPropertyAnimation *anim = new QPropertyAnimation(m_opacityEffect, "opacity");
    anim->setDuration(200);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);

    connect(anim, &QPropertyAnimation::finished, this, [=]() {
        close();
        anim->deleteLater();
    });

    anim->start();
}

// ===================== MINIMIZE (SMOOTH SHRINK EFFECT) =====================

void MainWindow::on_minimizeButton_clicked()
{
    QRect start = geometry();

    QRect end(
        start.x() + start.width() / 2,
        start.y() + start.height(),
        0,
        0
        );

    QPropertyAnimation *anim = new QPropertyAnimation(this, "geometry");
    anim->setDuration(180);
    anim->setEasingCurve(QEasingCurve::InBack);

    anim->setStartValue(start);
    anim->setEndValue(end);

    connect(anim, &QPropertyAnimation::finished, this, [=]() {
        showMinimized();
        setGeometry(start);
        anim->deleteLater();
    });

    anim->start();
}

// ===================== MAXIMIZE / RESTORE =====================

void MainWindow::on_maximizeButton_clicked()
{
    QPropertyAnimation *anim = new QPropertyAnimation(this, "geometry");
    anim->setDuration(250);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    if (!m_isMaximized)
    {
        m_normalGeometry = geometry();

        QRect target = screen()->availableGeometry();

        anim->setStartValue(geometry());
        anim->setEndValue(target);

        m_isMaximized = true;
    }
    else
    {
        anim->setStartValue(geometry());
        anim->setEndValue(m_normalGeometry);

        m_isMaximized = false;
    }

    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
void MainWindow::on_addFilesButton_clicked()
{
    QStringList files =
        QFileDialog::getOpenFileNames(
            this,
            "Select Files");

    for(const QString &file : files)
    {
        QFileInfo info(file);

        QTreeWidgetItem *item =
            new QTreeWidgetItem();

        item->setCheckState(0, Qt::Checked);

        item->setText(0, info.fileName());

        item->setText(1, info.absoluteFilePath());

        item->setData(
            0,
            Qt::UserRole,
            "FILE"
            );

        ui->filesTreeWidget
            ->addTopLevelItem(item);
    }
}


void MainWindow::on_addFolderButton_clicked()
{
    QString folder =
        QFileDialog::getExistingDirectory(
            this,
            "Select Folder");

    if(folder.isEmpty())
        return;

    QMessageBox::StandardButton reply;

    reply = QMessageBox::question(
        this,
        "Include Subfolders",
        "Include all subfolders and files?",
        QMessageBox::Yes |
            QMessageBox::No |
            QMessageBox::Cancel
        );

    if(reply == QMessageBox::Cancel)
        return;

    bool recursive =
        (reply == QMessageBox::Yes);

    addFolderToTree(folder, recursive);
}

void MainWindow::addFolderToTree(
    const QString &folder,
    bool recursive)
{
    QFileInfo rootInfo(folder);

    QTreeWidgetItem *rootItem =
        new QTreeWidgetItem();

    rootItem->setCheckState(0, Qt::Checked);

    rootItem->setText(
        0,
        rootInfo.fileName()
        );

    rootItem->setText(
        1,
        folder
        );

    rootItem->setData(
        0,
        Qt::UserRole,
        "FOLDER"
        );

    ui->filesTreeWidget
        ->addTopLevelItem(rootItem);

    if(recursive)
    {
        QDirIterator it(
            folder,
            QDir::Files,
            QDirIterator::Subdirectories
            );

        while(it.hasNext())
        {
            QString file =
                it.next();

            QFileInfo info(file);

            QTreeWidgetItem *child =
                new QTreeWidgetItem();

            child->setCheckState(
                0,
                Qt::Checked
                );

            child->setText(
                0,
                info.fileName()
                );

            child->setText(
                1,
                file
                );

            child->setData(
                0,
                Qt::UserRole,
                "FILE"
                );

            rootItem->addChild(child);
        }
    }
    else
    {
        QDir dir(folder);

        QFileInfoList files =
            dir.entryInfoList(
                QDir::Files
                );

        for(const QFileInfo &info : files)
        {
            QTreeWidgetItem *child =
                new QTreeWidgetItem();

            child->setCheckState(
                0,
                Qt::Checked
                );

            child->setText(
                0,
                info.fileName()
                );

            child->setText(
                1,
                info.absoluteFilePath()
                );

            child->setData(
                0,
                Qt::UserRole,
                "FILE"
                );

            rootItem->addChild(child);
        }
    }

    rootItem->setExpanded(true);
}

void MainWindow::on_removeSelectedButton_clicked()
{
    QList<QTreeWidgetItem*> selected =
        ui->filesTreeWidget->selectedItems();

    for(QTreeWidgetItem *item : selected)
    {
        delete item;
    }
}

