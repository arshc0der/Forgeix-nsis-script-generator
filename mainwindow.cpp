#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QScreen>
#include <QGuiApplication>
#include <QEasingCurve>
#include <QMessageBox>
#include <QFileDialog>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QTreeWidgetItem>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDateTime>
#include <QCheckBox>
#include <QComboBox>

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#endif

// Detects Windows "cloud-only" placeholder files (OneDrive/Dropbox/etc.
// Files On-Demand). QFileInfo::exists() and QFileInfo::size() both report
// correct-looking results for these because the metadata is local — only
// the actual byte content is missing until re-downloaded. A compiler like
// makensis that reads raw bytes will fail with a "no files found" style
// error on a file that Qt considers perfectly present.
static bool isCloudPlaceholder(const QString &path)
{
#ifdef Q_OS_WIN
    DWORD attrs = GetFileAttributesW(reinterpret_cast<const wchar_t *>(path.utf16()));
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false; // let the normal "missing file" check handle this case

    return (attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) || (attrs & FILE_ATTRIBUTE_OFFLINE);
#else
    Q_UNUSED(path);
    return false; // not detectable off Windows; nothing to flag here
#endif
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Frameless + transparent background
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // Fade effect setup
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacityEffect);
    m_opacityEffect->setOpacity(1.0);

    ui->filesTreeWidget->setColumnCount(2);
    QStringList headers;
    headers << "Name" << "Relative Path";
    ui->filesTreeWidget->setHeaderLabels(headers);

    ui->readmeFileLineEdit->setEnabled(false);
    ui->browseReadmeButton->setEnabled(false);

    if (ui->finishWebsiteLineEdit)
        ui->finishWebsiteLineEdit->setEnabled(false);

    if (ui->compressionComboBox && ui->compressionComboBox->count() == 0)
    {
        ui->compressionComboBox->addItems({"lzma", "zlib", "bzip2", "off"});
    }

    if (ui->installModeComboBox && ui->installModeComboBox->count() == 0)
    {
        ui->installModeComboBox->addItems({"Per-machine (all users, needs admin)",
                                           "Per-user (current user only)"});
    }
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

// ===================== WINDOW CHROME =====================

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

void MainWindow::on_minimizeButton_clicked()
{
    QRect start = geometry();
    QRect end(start.x() + start.width() / 2, start.y() + start.height(), 0, 0);

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

// ===================== FILE / FOLDER TREE =====================

void MainWindow::on_addFilesButton_clicked()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Files");

    for (const QString &file : files)
    {
        QFileInfo info(file);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setCheckState(0, Qt::Checked);
        item->setText(0, info.fileName());
        item->setText(1, info.absoluteFilePath());
        item->setData(0, Qt::UserRole, "FILE");

        ui->filesTreeWidget->addTopLevelItem(item);
    }
}

void MainWindow::on_addFolderButton_clicked()
{
    QString folder = QFileDialog::getExistingDirectory(this, "Select Folder");
    if (folder.isEmpty())
        return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Include Subfolders",
        "Include all subfolders and files?",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
        );

    if (reply == QMessageBox::Cancel)
        return;

    bool recursive = (reply == QMessageBox::Yes);
    addFolderToTree(folder, recursive);
}

void MainWindow::addFolderToTree(const QString &folder, bool recursive)
{
    QFileInfo rootInfo(folder);

    QTreeWidgetItem *rootItem = new QTreeWidgetItem();
    rootItem->setCheckState(0, Qt::Checked);
    rootItem->setText(0, rootInfo.fileName());
    rootItem->setText(1, folder);
    rootItem->setData(0, Qt::UserRole, "FOLDER");

    ui->filesTreeWidget->addTopLevelItem(rootItem);

    if (recursive)
    {
        QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext())
        {
            QString file = it.next();
            QFileInfo info(file);

            QTreeWidgetItem *child = new QTreeWidgetItem();
            child->setCheckState(0, Qt::Checked);
            child->setText(0, info.fileName());
            child->setText(1, file);
            child->setData(0, Qt::UserRole, "FILE");

            rootItem->addChild(child);
        }
    }
    else
    {
        QDir dir(folder);
        const QFileInfoList files = dir.entryInfoList(QDir::Files);

        for (const QFileInfo &info : files)
        {
            QTreeWidgetItem *child = new QTreeWidgetItem();
            child->setCheckState(0, Qt::Checked);
            child->setText(0, info.fileName());
            child->setText(1, info.absoluteFilePath());
            child->setData(0, Qt::UserRole, "FILE");

            rootItem->addChild(child);
        }
    }

    rootItem->setExpanded(true);
}

void MainWindow::on_removeSelectedButton_clicked()
{
    const QList<QTreeWidgetItem *> selected = ui->filesTreeWidget->selectedItems();
    for (QTreeWidgetItem *item : selected)
        delete item;
}

// ===================== RESOURCE PICKERS =====================

void MainWindow::on_SetupIconButton_clicked()
{
    QString file = QFileDialog::getOpenFileName(this, "Select Setup Icon", QString(), "Icon Files (*.ico)");
    if (!file.isEmpty())
        ui->SetupIconLineEdit->setText(file);
}

void MainWindow::on_SetupUninstallIconButton_clicked()
{
    QString file = QFileDialog::getOpenFileName(this, "Select Uninstall Icon", QString(), "Icon Files (*.ico)");
    if (!file.isEmpty())
        ui->SetupUninstallIconLineEdit->setText(file);
}

void MainWindow::on_SetupBPMButton_clicked()
{
    QString file = QFileDialog::getOpenFileName(this, "Select Setup Bitmap", QString(), "Bitmap Files (*.bmp)");
    if (!file.isEmpty())
        ui->SetupBPMLineEdit->setText(file);
}

void MainWindow::on_SetupUninstallBPMButton_clicked()
{
    QString file = QFileDialog::getOpenFileName(this, "Select Uninstall Bitmap", QString(), "Bitmap Files (*.bmp)");
    if (!file.isEmpty())
        ui->SetupUninstallBPMLineEdit->setText(file);
}

void MainWindow::on_browseBuildFolderButton_clicked()
{
    QString folder = QFileDialog::getExistingDirectory(this, "Select Build Folder");
    if (!folder.isEmpty())
        ui->buildFolderLineEdit->setText(folder);
}

void MainWindow::on_defaultDirButton_clicked()
{
    QString folder = QFileDialog::getExistingDirectory(this, "Select Default Installation Directory");
    if (!folder.isEmpty())
        ui->defaultDirLineEdit->setText(folder);
}

void MainWindow::on_browseReadmeButton_clicked()
{
    QString file = QFileDialog::getOpenFileName(
        this, "Select Readme File", QString(),
        "Text Files (*.txt);;PDF Files (*.pdf);;All Files (*.*)");
    if (!file.isEmpty())
        ui->readmeFileLineEdit->setText(file);
}

void MainWindow::on_browseLicenseButton_clicked()
{
    QString file = QFileDialog::getOpenFileName(
        this, "Select License File", QString(), "Text Files (*.txt);;All Files (*.*)");
    if (!file.isEmpty())
        ui->licenseFileLineEdit->setText(file);
}

void MainWindow::on_browseMainExeButton_clicked()
{
    QString file = QFileDialog::getOpenFileName(
        this, "Select Main Application EXE", QString(), "Executable Files (*.exe)");
    if (!file.isEmpty())
    {
        QFileInfo info(file);
        ui->mainExeLineEdit->setText(info.fileName());
    }
}

// ===================== TOGGLES =====================

void MainWindow::on_showReadmeCheckBox_toggled(bool checked)
{
    ui->readmeFileLineEdit->setEnabled(checked);
    ui->browseReadmeButton->setEnabled(checked);
}

void MainWindow::on_openWebsiteCheckBox_toggled(bool checked)
{
    if (ui->finishWebsiteLineEdit)
        ui->finishWebsiteLineEdit->setEnabled(checked);
}

// ===================== SCRIPT SAVE =====================

void MainWindow::on_scriptSavePathButton_clicked()
{
    QString file = QFileDialog::getSaveFileName(this, "Save NSIS Script", "installer.nsi", "NSIS Script (*.nsi)");
    if (!file.isEmpty())
        ui->scriptSavePathLineEdit->setText(file);
}

void MainWindow::on_saveScriptButton_clicked()
{
    QString path = ui->scriptSavePathLineEdit->text();

    if (path.isEmpty())
    {
        QMessageBox::warning(this, "Error", "Please select a save location.");
        return;
    }

    if (ui->scriptPreviewEdit->toPlainText().trimmed().isEmpty())
    {
        QMessageBox::warning(this, "Error", "Nothing to save yet. Click \"Generate\" first.");
        return;
    }

    QFile file(path);

    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&file);
        out << ui->scriptPreviewEdit->toPlainText();
        file.close();

        QMessageBox::information(this, "Saved", "Script saved successfully.");
    }
    else
    {
        QMessageBox::warning(this, "Error", "Could not write to that location:\n" + path);
    }
}

// ===================== SECURITY / CORRECTNESS HELPERS =====================

// Prevents user-entered text (app name, publisher, website, etc.) from
// breaking out of a quoted NSIS string. Without this, a value containing a
// literal " could inject arbitrary script directives into the compiled
// installer, and an unescaped $ can trigger unintended NSIS variable
// expansion (e.g. "$INSTDIR" typed into a text field).
QString MainWindow::escapeNsisString(const QString &input)
{
    QString out = input;
    out.replace('$', "$$");        // literal dollar sign
    out.replace('"', "$\\\"");     // literal double quote
    out.remove('\r');
    out.remove('\n');
    return out;
}

// Same idea, but for values that end up inside a NSIS registry key name.
QString MainWindow::sanitizeForRegistryKey(const QString &input)
{
    QString out = escapeNsisString(input);
    out.replace('\\', "/"); // backslash is a key-path separator in the registry
    return out;
}

// Strips characters that are illegal (or awkward) in Windows file/shortcut
// names, since appName is reused to build .lnk names and folder names.
QString MainWindow::sanitizeFileName(const QString &input)
{
    QString out = input;
    static const QRegularExpression illegal(R"([\\/:*?"<>|])");
    out.remove(illegal);
    out = out.trimmed();
    return out.isEmpty() ? QStringLiteral("Application") : out;
}

// IMPORTANT: Qt's file dialogs return paths using forward slashes even on
// Windows (that's Qt's own internal convention) — so without this, every
// path reaching the script would still be "C:/IMS_Deploy/..." regardless
// of anything done at the call site. Forward slashes were confirmed to
// break the `File` instruction specifically when invoked from inside the
// MUI_PAGE_WELCOME / MUI_UNPAGE_WELCOME macros (welcome/finish bitmaps) on
// this NSIS/MUI2 build — it reported "no files found" for a file that
// verifiably existed, and started working the moment the same path was
// written with backslashes instead. So paths are normalized to native
// Windows backslashes here, matching how HM NIS Edit and other native
// NSIS tooling always write them.
QString MainWindow::toNsisPath(const QString &path)
{
    QString out = path;
    out.replace('/', '\\');
    return out;
}

// VIProductVersion requires exactly four numeric, dot-separated fields.
// User-entered versions like "1.0" or "2.3-beta" are normalized/clamped
// instead of being passed through, which would otherwise fail the compile.
QString MainWindow::normalizedFourPartVersion(const QString &version)
{
    static const QRegularExpression nonDigitDot(R"([^0-9.])");
    QString cleaned = version;
    cleaned.remove(nonDigitDot);

    QStringList parts = cleaned.split('.', Qt::SkipEmptyParts);
    while (parts.size() < 4)
        parts.append("0");
    while (parts.size() > 4)
        parts.removeLast();

    for (QString &p : parts)
    {
        bool ok = false;
        int v = p.toInt(&ok);
        if (!ok || v < 0 || v > 65535)
            p = "0";
    }

    return parts.join('.');
}

// Refuses to generate a script whose uninstaller would RMDir /r a directory
// that is dangerously broad (a drive root, or a well-known system folder).
bool MainWindow::isSafeInstallDir(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return true; // caller substitutes a safe default in that case

    QString normalized = QDir::toNativeSeparators(dir).toLower();
    normalized.replace('\\', '/');
    while (normalized.endsWith('/'))
        normalized.chop(1);

    static const QStringList forbidden = {
        "c:", "c:/windows", "c:/windows/system32", "c:/program files",
        "c:/program files (x86)", "c:/users", "d:", "e:"
    };

    for (const QString &bad : forbidden)
    {
        if (normalized == bad)
            return false;
    }

    // A bare drive letter with nothing else (e.g. "C:/") is also unsafe.
    static const QRegularExpression driveRootOnly(R"(^[a-z]:$)");
    if (driveRootOnly.match(normalized).hasMatch())
        return false;

    return true;
}

// ===================== VALIDATION =====================

QStringList MainWindow::collectMissingResourceFiles() const
{
    QStringList missing;

    struct ResourceCheck { QString label; QString path; };

    const QList<ResourceCheck> checks = {
                                          {"Setup Icon", ui->SetupIconLineEdit->text()},
                                          {"Uninstall Icon", ui->SetupUninstallIconLineEdit->text()},
                                          {"Setup Bitmap", ui->SetupBPMLineEdit->text()},
                                          {"Uninstall Bitmap", ui->SetupUninstallBPMLineEdit->text()},
                                          {"License File", ui->licenseFileLineEdit->text()},
                                          {"Readme File", ui->showReadmeCheckBox->isChecked() ? ui->readmeFileLineEdit->text() : QString()},
                                          };

    for (const ResourceCheck &c : checks)
    {
        if (c.path.trimmed().isEmpty())
            continue; // optional field left blank, fine

        QFileInfo fi(c.path);
        if (!fi.exists() || !fi.isFile())
        {
            missing << QString("%1: %2").arg(c.label, c.path);
        }
        else if (isCloudPlaceholder(c.path))
        {
            missing << QString("%1: %2  [cloud-only placeholder — right-click it in "
                               "File Explorer and choose \"Always keep on this device\", "
                               "then try again]").arg(c.label, c.path);
        }
    }

    // Also verify every file/folder queued for deployment still exists.
    // A file could have been deleted, or fallen out of sync (e.g. a
    // cloud-storage placeholder) between being added and Generate being
    // clicked.
    for (int i = 0; i < ui->filesTreeWidget->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = ui->filesTreeWidget->topLevelItem(i);
        QString type = item->data(0, Qt::UserRole).toString();
        QString path = item->text(1);

        QFileInfo fi(path);
        if (type == "FILE" && (!fi.exists() || !fi.isFile()))
            missing << QString("Deployment file: %1").arg(path);
        else if (type == "FILE" && isCloudPlaceholder(path))
            missing << QString("Deployment file: %1  [cloud-only placeholder, not fully downloaded]").arg(path);
        else if (type == "FOLDER" && (!fi.exists() || !fi.isDir()))
            missing << QString("Deployment folder: %1").arg(path);

        for (int c = 0; c < item->childCount(); ++c)
        {
            QTreeWidgetItem *child = item->child(c);
            QFileInfo cfi(child->text(1));
            if (!cfi.exists() || !cfi.isFile())
                missing << QString("Deployment file: %1").arg(child->text(1));
            else if (isCloudPlaceholder(child->text(1)))
                missing << QString("Deployment file: %1  [cloud-only placeholder, not fully downloaded]").arg(child->text(1));
        }
    }

    return missing;
}

bool MainWindow::validateBeforeGenerate(QStringList &errors) const
{
    if (ui->appNameLineEdit->text().trimmed().isEmpty())
        errors << "Application Name is required.";

    if (ui->mainExeLineEdit->text().trimmed().isEmpty())
        errors << "Main EXE is required.";

    if (ui->OutputExeLineEdit->text().trimmed().isEmpty())
        errors << "Output Setup EXE Name is required.";

    if (ui->filesTreeWidget->topLevelItemCount() == 0)
        errors << "Add at least one file or folder to deploy.";

    if (!isSafeInstallDir(ui->defaultDirLineEdit->text()))
        errors << "The chosen Installation Directory is a system/drive root. "
                  "The uninstaller removes this folder recursively, so a broad "
                  "path here is not allowed. Pick an app-specific subfolder.";

    const QStringList missingFiles = collectMissingResourceFiles();
    for (const QString &m : missingFiles)
        errors << ("Missing on disk - " + m);

    return errors.isEmpty();
}

// ===================== SCRIPT GENERATION =====================

QString MainWindow::generateNsisScript()
{
    // ---------------------------------------------------------------
    // Read + sanitize user input up front. Every value that ends up
    // inside a quoted NSIS string goes through escapeNsisString() so a
    // stray " or $ typed into a field can't corrupt or inject into the
    // generated script.
    // ---------------------------------------------------------------
    const QString appNameRaw = ui->appNameLineEdit->text().trimmed();
    const QString appName    = escapeNsisString(appNameRaw);
    const QString safeName   = sanitizeFileName(appNameRaw); // for .lnk / folder names

    const QString version    = escapeNsisString(ui->appVersionLineEdit->text().trimmed().isEmpty()
                                                 ? "1.0.0" : ui->appVersionLineEdit->text().trimmed());
    const QString fourPartVer = normalizedFourPartVersion(version);

    const QString publisher  = escapeNsisString(ui->publisherLineEdit->text().trimmed());
    const QString website    = escapeNsisString(ui->websiteLineEdit->text().trimmed());
    const QString company    = escapeNsisString(ui->companyNameLineEdit->text().trimmed());

    const QString mainExe    = escapeNsisString(ui->mainExeLineEdit->text().trimmed());
    const QString outputExe  = escapeNsisString(ui->OutputExeLineEdit->text().trimmed());

    const bool perUser = ui->installModeComboBox &&
                         ui->installModeComboBox->currentIndex() == 1;
    const QString uninstRootKey = perUser ? "HKCU" : "HKLM";
    const QString execLevel     = perUser ? "user" : "admin";

    QString installDir = ui->defaultDirLineEdit->text().trimmed();
    if (installDir.isEmpty() || !isSafeInstallDir(installDir))
    {
        // Built-in NSIS variable, not user text — must NOT be escaped, or
        // "$PROGRAMFILES64" turns into the literal string "$$PROGRAMFILES64"
        // (NSIS's escape for a literal dollar sign) instead of expanding.
        installDir = (perUser ? "$LOCALAPPDATA\\" : "$PROGRAMFILES64\\") + safeName;
    }
    else
    {
        // This one genuinely came from a text field, so it does need escaping.
        installDir = escapeNsisString(installDir);
    }

    QString script;

    // -----------------------------------------------------------
    // Header
    // -----------------------------------------------------------
    script += "; ============================================\n";
    script += "; Generated by Custom NSIS Builder\n";
    script += "; Generated: " + QDateTime::currentDateTime().toString(Qt::ISODate) + "\n";
    script += "; ============================================\n\n";

    script += "!include \"MUI2.nsh\"\n\n";

    // -----------------------------------------------------------
    // Product information / registry keys used for Add-Remove Programs
    // -----------------------------------------------------------
    script += "!define PRODUCT_NAME \"" + appName + "\"\n";
    script += "!define PRODUCT_VERSION \"" + version + "\"\n";
    script += "!define PRODUCT_PUBLISHER \"" + publisher + "\"\n";
    script += "!define PRODUCT_WEB_SITE \"" + website + "\"\n";
    script += "!define PRODUCT_DIR_REGKEY \"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"
              + sanitizeForRegistryKey(mainExe) + "\"\n";
    script += "!define PRODUCT_UNINST_KEY \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${PRODUCT_NAME}\"\n";
    script += "!define PRODUCT_UNINST_ROOT_KEY \"" + uninstRootKey + "\"\n\n";

    // -----------------------------------------------------------
    // Compression
    // -----------------------------------------------------------
    QString compression = ui->compressionComboBox ? ui->compressionComboBox->currentText().trimmed().toLower() : "lzma";
    if (compression != "zlib" && compression != "bzip2" && compression != "off")
        compression = "lzma";

    if (compression == "off")
        script += "SetCompress off\n\n";
    else
        script += "SetCompressor /SOLID " + compression + "\n\n";

    // -----------------------------------------------------------
    // MUI settings
    // -----------------------------------------------------------
    script += "!define MUI_ABORTWARNING\n";

    const bool hasSetupIcon = !ui->SetupIconLineEdit->text().trimmed().isEmpty()
                              && QFileInfo::exists(ui->SetupIconLineEdit->text().trimmed());
    const bool hasUninstIcon = !ui->SetupUninstallIconLineEdit->text().trimmed().isEmpty()
                               && QFileInfo::exists(ui->SetupUninstallIconLineEdit->text().trimmed());
    const bool hasSetupBmp = !ui->SetupBPMLineEdit->text().trimmed().isEmpty()
                             && QFileInfo::exists(ui->SetupBPMLineEdit->text().trimmed());
    const bool hasUninstBmp = !ui->SetupUninstallBPMLineEdit->text().trimmed().isEmpty()
                              && QFileInfo::exists(ui->SetupUninstallBPMLineEdit->text().trimmed());
    const bool hasLicense = !ui->licenseFileLineEdit->text().trimmed().isEmpty()
                            && QFileInfo::exists(ui->licenseFileLineEdit->text().trimmed());

    // Only ever !define a MUI_ICON/BITMAP if the file genuinely exists on
    // disk right now. This is what was crashing the previous build: a
    // bitmap path was defined but the file wasn't actually present at
    // compile time, so makensis aborted deep inside MUI_PAGE_WELCOME.
    if (hasSetupIcon)
        script += "!define MUI_ICON \"" + toNsisPath(escapeNsisString(ui->SetupIconLineEdit->text().trimmed())) + "\"\n";
    if (hasUninstIcon)
        script += "!define MUI_UNICON \"" + toNsisPath(escapeNsisString(ui->SetupUninstallIconLineEdit->text().trimmed())) + "\"\n";
    if (hasSetupBmp)
        script += "!define MUI_WELCOMEFINISHPAGE_BITMAP \"" + toNsisPath(escapeNsisString(ui->SetupBPMLineEdit->text().trimmed())) + "\"\n";
    if (hasUninstBmp)
        script += "!define MUI_UNWELCOMEFINISHPAGE_BITMAP \"" + toNsisPath(escapeNsisString(ui->SetupUninstallBPMLineEdit->text().trimmed())) + "\"\n";

    if (ui->runAfterCheckBox->isChecked())
        script += "!define MUI_FINISHPAGE_RUN \"$INSTDIR\\" + mainExe + "\"\n";

    if (ui->openWebsiteCheckBox && ui->openWebsiteCheckBox->isChecked()
        && ui->finishWebsiteLineEdit && !ui->finishWebsiteLineEdit->text().trimmed().isEmpty())
    {
        script += "!define MUI_FINISHPAGE_LINK \"Visit our website\"\n";
        script += "!define MUI_FINISHPAGE_LINK_LOCATION \""
                  + escapeNsisString(ui->finishWebsiteLineEdit->text().trimmed()) + "\"\n";
    }

    script += "\n";

    // -----------------------------------------------------------
    // Installer pages
    // -----------------------------------------------------------
    script += "; Installer Pages\n";
    script += "!insertmacro MUI_PAGE_WELCOME\n";

    if (hasLicense)
        script += "!insertmacro MUI_PAGE_LICENSE \"" + toNsisPath(escapeNsisString(ui->licenseFileLineEdit->text().trimmed())) + "\"\n";

    script += "!insertmacro MUI_PAGE_DIRECTORY\n";
    script += "!insertmacro MUI_PAGE_INSTFILES\n";
    script += "!insertmacro MUI_PAGE_FINISH\n\n";

    // -----------------------------------------------------------
    // Uninstaller pages
    // -----------------------------------------------------------
    script += "; Uninstaller Pages\n";
    script += "!insertmacro MUI_UNPAGE_WELCOME\n";

    if (ui->showUninstallPromptCheckBox && ui->showUninstallPromptCheckBox->isChecked())
        script += "!insertmacro MUI_UNPAGE_CONFIRM\n";

    script += "!insertmacro MUI_UNPAGE_INSTFILES\n";
    script += "!insertmacro MUI_UNPAGE_FINISH\n\n";

    // -----------------------------------------------------------
    // Languages
    // -----------------------------------------------------------
    script += "; Languages\n";
    struct LangCheck { QCheckBox *box; QString nsisName; };
    const QList<LangCheck> langs = {
                                     {ui->englishCheckBox, "English"}, {ui->frenchCheckBox, "French"},
                                     {ui->germanCheckBox, "German"}, {ui->spanishCheckBox, "Spanish"},
                                     {ui->italianCheckBox, "Italian"}, {ui->portugueseCheckBox, "Portuguese"},
                                     {ui->russianCheckBox, "Russian"}, {ui->japaneseCheckBox, "Japanese"},
                                     {ui->koreanCheckBox, "Korean"}, {ui->chineseCheckBox, "SimpChinese"},
                                     };

    bool anyLanguageSelected = false;
    for (const LangCheck &l : langs)
    {
        if (l.box && l.box->isChecked())
        {
            script += "!insertmacro MUI_LANGUAGE \"" + l.nsisName + "\"\n";
            anyLanguageSelected = true;
        }
    }
    if (!anyLanguageSelected)
        script += "!insertmacro MUI_LANGUAGE \"English\"\n"; // NSIS requires at least one

    script += "\n";

    // -----------------------------------------------------------
    // General installer settings (kept below pages, matching typical
    // HM NIS Edit output ordering, but works fine either place in NSIS)
    // -----------------------------------------------------------
    script += "Name \"${PRODUCT_NAME} ${PRODUCT_VERSION}\"\n";
    script += "OutFile \"" + outputExe + "\"\n";
    script += "InstallDir \"" + installDir + "\"\n";
    script += "InstallDirRegKey ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_DIR_REGKEY}\" \"\"\n";
    script += "RequestExecutionLevel " + execLevel + "\n";
    script += "ShowInstDetails show\n";
    script += "ShowUnInstDetails show\n\n";

    // File/version info embedded in the compiled exe's Properties dialog
    script += "VIProductVersion \"" + fourPartVer + "\"\n";
    script += "VIAddVersionKey \"ProductName\" \"" + appName + "\"\n";
    script += "VIAddVersionKey \"FileVersion\" \"" + version + "\"\n";
    script += "VIAddVersionKey \"ProductVersion\" \"" + version + "\"\n";
    if (!company.isEmpty())
    {
        script += "VIAddVersionKey \"CompanyName\" \"" + company + "\"\n";
        script += "VIAddVersionKey \"LegalCopyright\" \"" + company + "\"\n";
    }
    script += "VIAddVersionKey \"FileDescription\" \"" + appName + " Setup\"\n\n";

    if (!company.isEmpty())
        script += "BrandingText \"" + company + "\"\n\n";

    // -----------------------------------------------------------
    // Main installation section
    // -----------------------------------------------------------
    const bool preserveRelative = ui->relativePathCheckBox && ui->relativePathCheckBox->isChecked();

    // Normalize the build folder once: forward slashes, no trailing slash,
    // so startsWith() comparisons below are consistent regardless of how
    // the user's picker returned the path.
    QString buildFolder = ui->buildFolderLineEdit->text().trimmed();
    buildFolder.replace('\\', '/');
    while (buildFolder.endsWith('/'))
        buildFolder.chop(1);

    script += "Section \"MainSection\" SEC01\n";
    script += "    SetOutPath \"$INSTDIR\"\n";
    script += "    SetOverwrite try\n\n";

    // Emits a single File instruction for `filePath`. When "preserve
    // relative path" is checked and a Build Folder is set, the file's
    // directory *relative to the Build Folder* is recreated under
    // $INSTDIR via SetOutPath; otherwise every file lands flat in
    // $INSTDIR (original behavior, kept as the default/fallback).
    QString lastOutPath; // avoid emitting a redundant SetOutPath for every single file
    auto emitFile = [&](const QString &filePath)
    {
        QString outPath = "$INSTDIR";

        if (preserveRelative && !buildFolder.isEmpty())
        {
            QString normalizedFile = filePath;
            normalizedFile.replace('\\', '/');

            if (normalizedFile.startsWith(buildFolder, Qt::CaseInsensitive))
            {
                QString rel = QFileInfo(normalizedFile).absolutePath();
                rel.replace('\\', '/');
                rel = rel.mid(buildFolder.length());
                if (rel.startsWith('/'))
                    rel.remove(0, 1);

                if (!rel.isEmpty())
                    outPath = "$INSTDIR\\" + escapeNsisString(rel).replace('/', '\\');
            }
            // If the file isn't under the Build Folder at all, there's no
            // meaningful relative path to compute — it falls back to
            // $INSTDIR flat, same as when the checkbox is off.
        }

        if (outPath != lastOutPath)
        {
            script += "    SetOutPath \"" + outPath + "\"\n";
            lastOutPath = outPath;
        }

        script += "    File \"" + toNsisPath(escapeNsisString(filePath)) + "\"\n";
    };

    for (int i = 0; i < ui->filesTreeWidget->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = ui->filesTreeWidget->topLevelItem(i);
        QString type = item->data(0, Qt::UserRole).toString();
        QString path = item->text(1);

        if (type == "FILE")
        {
            emitFile(path);
        }
        else if (type == "FOLDER")
        {
            if (preserveRelative)
            {
                // Recreate the folder's own subtree explicitly, file by
                // file, so it honors the same relative-path logic as
                // everything else instead of being a special case.
                for (int c = 0; c < item->childCount(); ++c)
                {
                    QTreeWidgetItem *child = item->child(c);
                    if (child->data(0, Qt::UserRole).toString() == "FILE")
                        emitFile(child->text(1));
                }
                continue; // children already handled above; skip the generic loop below
            }
            else
            {
                script += "    SetOutPath \"$INSTDIR\"\n";
                lastOutPath = "$INSTDIR";
                script += "    File /r \"" + toNsisPath(escapeNsisString(path)) + "\\*.*\"\n";
                continue; // File /r already pulled in all children; don't re-emit them below
            }
        }

        // Reached only for FILE top-level items with children (not expected
        // in the current tree model, but handled defensively) — and is
        // intentionally skipped for FOLDER items via the `continue`s above.
        for (int c = 0; c < item->childCount(); ++c)
        {
            QTreeWidgetItem *child = item->child(c);
            if (child->data(0, Qt::UserRole).toString() == "FILE")
                emitFile(child->text(1));
        }
    }

    if (lastOutPath != "$INSTDIR")
        script += "    SetOutPath \"$INSTDIR\"\n";
    script += "\n";

    if (ui->desktopShortcutCheckBox->isChecked())
    {
        script += "    CreateShortCut \"$DESKTOP\\" + safeName + ".lnk\" \"$INSTDIR\\" + mainExe + "\"\n";
    }

    if (ui->startMenuShortcutCheckBox->isChecked())
    {
        script += "    CreateDirectory \"$SMPROGRAMS\\" + safeName + "\"\n";
        script += "    CreateShortCut \"$SMPROGRAMS\\" + safeName + "\\" + safeName + ".lnk\" \"$INSTDIR\\" + mainExe + "\"\n";
    }

    if (ui->quickLaunchShortcutCheckBox && ui->quickLaunchShortcutCheckBox->isChecked())
    {
        script += "    CreateShortCut \"$QUICKLAUNCH\\" + safeName + ".lnk\" \"$INSTDIR\\" + mainExe + "\"\n";
    }

    script += "\n    WriteUninstaller \"$INSTDIR\\uninst.exe\"\n";
    script += "SectionEnd\n\n";

    // -----------------------------------------------------------
    // Additional icons (website shortcut, uninstall shortcut)
    // -----------------------------------------------------------
    if (ui->startMenuShortcutCheckBox->isChecked() && !website.trimmed().isEmpty())
    {
        script += "Section -AdditionalIcons\n";
        script += "    WriteIniStr \"$INSTDIR\\${PRODUCT_NAME}.url\" \"InternetShortcut\" \"URL\" \"${PRODUCT_WEB_SITE}\"\n";
        script += "    CreateShortCut \"$SMPROGRAMS\\" + safeName + "\\Website.lnk\" \"$INSTDIR\\${PRODUCT_NAME}.url\"\n";
        script += "    CreateShortCut \"$SMPROGRAMS\\" + safeName + "\\Uninstall.lnk\" \"$INSTDIR\\uninst.exe\"\n";
        script += "SectionEnd\n\n";
    }

    // -----------------------------------------------------------
    // Post-install: register in Add/Remove Programs
    // -----------------------------------------------------------
    script += "Section -Post\n";
    script += "    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_DIR_REGKEY}\" \"\" \"$INSTDIR\\" + mainExe + "\"\n";
    script += "    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\" \"DisplayName\" \"$(^Name)\"\n";
    script += "    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\" \"UninstallString\" \"$INSTDIR\\uninst.exe\"\n";
    if (hasSetupIcon)
        script += "    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\" \"DisplayIcon\" \"$INSTDIR\\" + mainExe + "\"\n";
    script += "    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\" \"DisplayVersion\" \"${PRODUCT_VERSION}\"\n";
    script += "    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\" \"URLInfoAbout\" \"${PRODUCT_WEB_SITE}\"\n";
    script += "    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\" \"Publisher\" \"${PRODUCT_PUBLISHER}\"\n";
    script += "    WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\" \"NoModify\" 1\n";
    script += "    WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\" \"NoRepair\" 1\n";
    script += "SectionEnd\n\n";

    // -----------------------------------------------------------
    // Uninstall confirmation / success messages
    // -----------------------------------------------------------
    if (ui->showUninstallPromptCheckBox && ui->showUninstallPromptCheckBox->isChecked())
    {
        script += "Function un.onInit\n";
        script += "    MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "
                  "\"Are you sure you want to completely remove $(^Name) and all of its components?\" IDYES +2\n";
        script += "    Abort\n";
        script += "FunctionEnd\n\n";
    }

    if (ui->showUninstallSuccessCheckBox && ui->showUninstallSuccessCheckBox->isChecked())
    {
        script += "Function un.onUninstSuccess\n";
        script += "    HideWindow\n";
        script += "    MessageBox MB_ICONINFORMATION|MB_OK \"$(^Name) was successfully removed from your computer.\"\n";
        script += "FunctionEnd\n\n";
    }

    // -----------------------------------------------------------
    // Uninstall section
    // -----------------------------------------------------------
    script += "Section Uninstall\n";
    script += "    Delete \"$INSTDIR\\${PRODUCT_NAME}.url\"\n";
    script += "    Delete \"$INSTDIR\\uninst.exe\"\n";
    script += "    RMDir /r \"$INSTDIR\"\n\n";

    if (ui->desktopShortcutCheckBox->isChecked())
        script += "    Delete \"$DESKTOP\\" + safeName + ".lnk\"\n";

    if (ui->quickLaunchShortcutCheckBox && ui->quickLaunchShortcutCheckBox->isChecked())
        script += "    Delete \"$QUICKLAUNCH\\" + safeName + ".lnk\"\n";

    if (ui->startMenuShortcutCheckBox->isChecked())
    {
        script += "    Delete \"$SMPROGRAMS\\" + safeName + "\\" + safeName + ".lnk\"\n";
        script += "    Delete \"$SMPROGRAMS\\" + safeName + "\\Website.lnk\"\n";
        script += "    Delete \"$SMPROGRAMS\\" + safeName + "\\Uninstall.lnk\"\n";
        script += "    RMDir \"$SMPROGRAMS\\" + safeName + "\"\n";
    }

    script += "\n    DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_UNINST_KEY}\"\n";
    script += "    DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} \"${PRODUCT_DIR_REGKEY}\"\n";
    script += "    SetAutoClose true\n";
    script += "SectionEnd\n";

    return script;
}

// ===================== GENERATE BUTTON =====================

void MainWindow::on_generateButton_clicked()
{
    QStringList errors;

    if (!validateBeforeGenerate(errors))
    {
        QMessageBox::warning(
            this,
            "Cannot Generate Script",
            "Please fix the following before generating:\n\n- " + errors.join("\n- ")
            );
        return;
    }

    QString script = generateNsisScript();
    ui->scriptPreviewEdit->setPlainText(script);

    QMessageBox::information(this, "Success", "NSIS script generated successfully.");
}