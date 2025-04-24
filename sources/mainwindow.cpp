#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidgetItem>
#include <QUrl>
#include "adbinstallworker.h"
#include "apkdecompiledialog.h"
#include "apkdecompileworker.h"
#include "apkrecompileworker.h"
#include "apksignworker.h"
#include "apkEverythingWorker.h"
#include "findreplacedialog.h"
#include "hexedit.h"
#include "imageviewerwidget.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "signingconfigdialog.h"
#include "sourcecodeedit.h"

#define COLOR_CODE 0x2ad2c9
#define COLOR_COMMAND 0xd0d2d3
#define COLOR_OUTPUT 0xffffff
#define COLOR_ERROR 0xfb0a2a

#define IMAGE_EXTENSIONS "gif|jpeg|jpg|png"
#define TEXT_EXTENSIONS "java|html|properties|smali|txt|xml|yaml|yml"

#define URL_CONTRIBUTE "https://github.com/vaibhavpandeyvpz/apkstudio"
#define URL_DOCUMENTATION "https://vaibhavpandey.com/apkstudio/"
#define URL_ISSUES "https://github.com/vaibhavpandeyvpz/apkstudio/issues"
#define URL_THANKS "https://forum.xda-developers.com/showthread.php?t=2493107"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

MainWindow::MainWindow(const QMap<QString, QString> &versions, QWidget *parent)
    : QMainWindow(parent), m_FindReplaceDialog(nullptr)
{
    addDockWidget(Qt::LeftDockWidgetArea, m_DockProject = buildProjectsDock());
    addDockWidget(Qt::LeftDockWidgetArea, m_DockFiles = buildFilesDock());
    addDockWidget(Qt::BottomDockWidgetArea, m_DockConsole = buildConsoleDock());
    addToolBar(Qt::LeftToolBarArea, buildMainToolBar());
    setCentralWidget(buildCentralWidget());
    setMenuBar(buildMenuBar());
    setMinimumSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    setStatusBar(buildStatusBar(versions));
    setWindowTitle(tr("APK Studio").append(" - https://vaibhavpandey.com/apkstudio/"));
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::handleClipboardDataChanged);
    QSettings settings;
    const bool dark = settings.value("dark_theme", false).toBool();
    QFile qss(QString(":/styles/%1.qss").arg(dark ? "dark" : "light"));
    qss.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream contents(&qss);
    setStyleSheet(contents.readAll());
    qss.close();
    if (settings.value("app_maximized").toBool()) {
        showMaximized();
    } else {
        resize(settings.value("app_size", QSize(WINDOW_WIDTH, WINDOW_HEIGHT)).toSize());
    }
    const QVariant state = settings.value("dock_state");
    if (state.isValid()) {
        restoreState(state.toByteArray());
    }
    m_ActionViewProject->setChecked(m_DockProject->isVisible());
    m_ActionViewFiles->setChecked(m_DockFiles->isVisible());
    m_ActionViewConsole->setChecked(m_DockConsole->isVisible());
    QTimer::singleShot(100, [=] {
        QSettings settings;
        const QStringList files = settings.value("open_files").toStringList();
        foreach (const QString &file, files) {
            if (QFile::exists(file)) {
                openFile(file);
            }
        }
        bool missing = false;
        foreach (const QString &binary, versions.keys()) {
            if (versions[binary].isEmpty()) {
#ifdef QT_DEBUG
                qDebug() << binary << "is missing";
#endif
                missing = true;
                break;
            }
        }
        if (missing) {
            int btn = QMessageBox::information(
                        this,
                        tr("Requirements"),
                        tr("One or more required 3rd-party binaries are missing. Please review settings first."),
                        tr("Settings"),
                        tr("Cancel"));
            if (btn == 0) {
                (new SettingsDialog(1, this))->exec();
            }
        }
    });
}

QWidget *MainWindow::buildCentralWidget()
{
    m_CentralStack = new QStackedWidget(this);
    auto empty = new QLabel(m_CentralStack);
    empty->setAlignment(Qt::AlignCenter);
    empty->setMargin(32);
    empty->setStyleSheet("QLabel { color: rgba(0, 0, 0, 50%) }");
    empty->setText(tr("<h1>%1</h1><p>%2</p>")
                .arg("No files open.")
                .arg("You need to decompile an APK first or open an already decompiled folder. Once done, click on any file in the tree on the left to view/edit it."));
    empty->setWordWrap(true);
    m_CentralStack->addWidget(empty);
    m_CentralStack->addWidget(m_TabEditors = new QTabWidget(this));
    connect(m_TabEditors, &QTabWidget::currentChanged, this, &MainWindow::handleTabChanged);
    connect(m_TabEditors, &QTabWidget::tabCloseRequested, this, &MainWindow::handleTabCloseRequested);
    m_TabEditors->setTabsClosable(true);
    m_CentralStack->setCurrentIndex(0);
    return m_CentralStack;
}

QDockWidget *MainWindow::buildConsoleDock()
{
    auto dock = new QDockWidget(tr("Console"), this);
    QFont font;
#ifdef Q_OS_WIN
    font.setFamily("Courier New");
#elif defined(Q_OS_MACOS)
    font.setFamily("Monaco");
#else
    font.setFamily("Ubuntu Mono");
#endif
    font.setFixedPitch(true);
    font.setPointSize(10);
    font.setStyleHint(QFont::Monospace);
    QFontMetrics metrics(font);
    QPalette palette;
    palette.setColor(QPalette::Active, QPalette::Base, QColor("#000000"));
    palette.setColor(QPalette::Inactive, QPalette::Base, QColor("#111111"));
    m_EditConsole = new QTextEdit(this);
    m_EditConsole->setFont(font);
    m_EditConsole->setFrameStyle(QFrame::NoFrame);
    m_EditConsole->setPalette(palette);
    m_EditConsole->setReadOnly(true);
    m_EditConsole->setTabStopWidth(4 * metrics.width('8'));
    m_EditConsole->setWordWrapMode(QTextOption::NoWrap);
    connect(ProcessOutput::instance(), &ProcessOutput::commandFinished, this, &MainWindow::handleCommandFinished);
    connect(ProcessOutput::instance(), &ProcessOutput::commandStarting, this, &MainWindow::handleCommandStarting);
    setContentsMargins(2, 2, 2, 2);
    dock->setObjectName("ConsoleDock");
    dock->setWidget(m_EditConsole);
    return dock;
}

QDockWidget *MainWindow::buildFilesDock()
{
    auto dock = new QDockWidget(tr("Files"), this);
    m_ListOpenFiles = new QListView(this);
    m_ListOpenFiles->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ListOpenFiles->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ListOpenFiles->setMinimumWidth(240);
    m_ListOpenFiles->setModel(m_ModelOpenFiles = new QStandardItemModel(m_ListOpenFiles));
    m_ListOpenFiles->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_ListOpenFiles->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_ListOpenFiles->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::handleFilesSelectionChanged);
    dock->setObjectName("FilesDock");
    dock->setWidget(m_ListOpenFiles);
    return dock;
}

QToolBar *MainWindow::buildMainToolBar()
{
    auto toolbar = new QToolBar(this);
    toolbar->addAction(QIcon(":/icons/icons8/icons8-android-os-48.png"), tr("Open APK"), this, &MainWindow::handleActionApk);
    toolbar->addAction(QIcon(":/icons/icons8/icons8-folder-48.png"), tr("Open Folder"), this, &MainWindow::handleActionFolder);
    toolbar->addSeparator();
    toolbar->addAction(QIcon(":/icons/icons8/icons8-gear-48.png"), tr("Settings"), this, &MainWindow::handleActionSettings);
    auto empty = new QWidget(this);
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolbar->addWidget(empty);
    m_ActionBuild2 = toolbar->addAction(QIcon(":/icons/icons8/icons8-hammer-48.png"), tr("Project Build"), this, &MainWindow::handleActionBuild);
    m_ActionBuild2->setEnabled(false);
    m_ActionInstall2 = toolbar->addAction(QIcon(":/icons/icons8/icons8-software-installer-48.png"), tr("Project Install"), this, &MainWindow::handleActionInstall);
    m_ActionInstall2->setEnabled(false);
    toolbar->setIconSize(QSize(48, 48));
    toolbar->setMovable(false);
    toolbar->setObjectName("Toolbar");
    return toolbar;
}

QMenuBar *MainWindow::buildMenuBar()
{
    auto menubar = new QMenuBar(this);
    auto file = menubar->addMenu(tr("File"));
    auto open = file->addMenu(tr("Open"));
    open->addAction(tr("APK"), this, &MainWindow::handleActionApk, QKeySequence::New);
    open->addAction(tr("Folder"), this, &MainWindow::handleActionFolder, QKeySequence::Open);
    open->addSeparator();
    open->addAction(tr("File"), this, &MainWindow::handleActionFile);
    file->addSeparator();
    m_ActionClose = file->addAction(tr("Close"), this, &MainWindow::handleActionClose, QKeySequence::Close);
    m_ActionClose->setEnabled(false);
    m_ActionCloseAll = file->addAction(tr("Close All"), this, &MainWindow::handleActionCloseAll);
    m_ActionCloseAll->setEnabled(false);
    file->addSeparator();
    m_ActionSave = file->addAction(tr("Save"), this, &MainWindow::handleActionSave, QKeySequence::Save);
    m_ActionSave->setEnabled(false);
    m_ActionSaveAll = file->addAction(tr("Save All"), this, &MainWindow::handleActionSaveAll);
    m_ActionSaveAll->setEnabled(false);
    file->addSeparator();
    file->addAction(tr("Quit"), this, &MainWindow::handleActionQuit, QKeySequence::Quit);
    auto edit = menubar->addMenu(tr("Edit"));
    m_ActionUndo = edit->addAction(tr("Undo"), this, &MainWindow::handleActionUndo, QKeySequence::Undo);
    m_ActionUndo->setEnabled(false);
    m_ActionRedo = edit->addAction(tr("Redo"), this, &MainWindow::handleActionRedo, QKeySequence::Redo);
    m_ActionRedo->setEnabled(false);
    edit->addSeparator();
    m_ActionCut = edit->addAction(tr("Cut"), this, &MainWindow::handleActionCut, QKeySequence::Cut);
    m_ActionCut->setEnabled(false);
    m_ActionCopy = edit->addAction(tr("Copy"), this, &MainWindow::handleActionCopy, QKeySequence::Copy);
    m_ActionCopy->setEnabled(false);
    m_ActionPaste = edit->addAction(tr("Paste"), this, &MainWindow::handleActionPaste, QKeySequence::Paste);
    m_ActionPaste->setEnabled(false);
    edit->addSeparator();
    m_ActionFind = edit->addAction(tr("Find"), this, &MainWindow::handleActionFind, QKeySequence::Find);
    m_ActionFind->setEnabled(false);
    m_ActionReplace = edit->addAction(tr("Replace"), this, &MainWindow::handleActionReplace, QKeySequence::Replace);
    m_ActionReplace->setEnabled(false);
    m_ActionGoto = edit->addAction(tr("Go To"), this, &MainWindow::handleActionGoto);
    m_ActionGoto->setEnabled(false);
    edit->addSeparator();
    edit->addAction(tr("Settings"), this, &MainWindow::handleActionSettings, QKeySequence::Preferences);
    auto view = menubar->addMenu(tr("View"));
    m_ActionViewProject = view->addAction(tr("Project"));
    m_ActionViewProject->setCheckable(true);
    connect(m_ActionViewProject, &QAction::toggled, m_DockProject, &QDockWidget::setVisible);
    connect(m_DockProject, &QDockWidget::visibilityChanged, m_ActionViewProject, &QAction::setChecked);
    m_ActionViewFiles = view->addAction(tr("Files"));
    m_ActionViewFiles->setCheckable(true);
    connect(m_ActionViewFiles, &QAction::toggled, m_DockFiles, &QDockWidget::setVisible);
    connect(m_DockFiles, &QDockWidget::visibilityChanged, m_ActionViewFiles, &QAction::setChecked);
    m_ActionViewConsole = view->addAction(tr("Console"));
    m_ActionViewConsole->setCheckable(true);
    connect(m_ActionViewConsole, &QAction::toggled, m_DockConsole, &QDockWidget::setVisible);
    connect(m_DockConsole, &QDockWidget::visibilityChanged, m_ActionViewConsole, &QAction::setChecked);
    auto project = menubar->addMenu(tr("Project"));
    m_ActionBuild1 = project->addAction(tr("Build"), this, &MainWindow::handleActionBuild);
    m_ActionBuild1->setEnabled(false);
    project->addSeparator();
    m_ActionSign = project->addAction(tr("Sign / Export"), this, &MainWindow::handleActionSign);
    m_ActionSign->setEnabled(false);
    m_ActionInstall1 = project->addAction(tr("Install"), this, &MainWindow::handleActionInstall);
    m_ActionInstall1->setEnabled(false);
    auto help = menubar->addMenu(tr("Help"));
    help->addAction(tr("About"), this, &MainWindow::handleActionAbout);
    help->addAction(tr("Documentation"), this, &MainWindow::handleActionDocumentation);
    help->addSeparator();
    auto feedback = help->addMenu(tr("Feedback"));
    feedback->addAction(tr("Say Thanks"), this, &MainWindow::handleActionSayThanks);
    feedback->addAction(tr("Report Issues"), this, &MainWindow::handleActionReportIssues);
    help->addAction(tr("Contribute"), this, &MainWindow::handleActionContribute);
    return menubar;
}

QDockWidget *MainWindow::buildProjectsDock()
{
    auto dock = new QDockWidget(tr("Projects"), this);
    m_ProjectsTree = new QTreeWidget(this);
    m_ProjectsTree->header()->hide();
    m_ProjectsTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ProjectsTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ProjectsTree->setMinimumWidth(240);
    m_ProjectsTree->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_ProjectsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ProjectsTree->setSortingEnabled(false);
    connect(m_ProjectsTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::handleTreeContextMenu);
    connect(m_ProjectsTree, &QTreeWidget::doubleClicked, this, &MainWindow::handleTreeDoubleClicked);
    connect(m_ProjectsTree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::handleTreeSelectionChanged);
    dock->setObjectName("ProjectsDock");
    dock->setWidget(m_ProjectsTree);
    return dock;
}

QStatusBar *MainWindow::buildStatusBar(const QMap<QString, QString> &versions)
{
    auto buildSeparator = [=] {
        auto frame = new QFrame(this);
        frame->setFrameStyle(QFrame::VLine);
        frame->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        return frame;
    };
    auto statusbar = new QStatusBar(this);
    statusbar->addPermanentWidget(new QLabel(tr("Java").append(": ").append(versions["java"]), this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(new QLabel(tr("Apktool").append(": ").append(versions["apktool"]), this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(new QLabel(tr("Jadx").append(": ").append(versions["jadx"]), this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(new QLabel(tr("ADB").append(": ").append(versions["adb"]), this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(new QLabel(tr("Uber APK Signer").append(": ").append(versions["uas"]), this));
    statusbar->addPermanentWidget(new QWidget(this), 1);
    statusbar->addPermanentWidget(m_StatusCursor = new QLabel("0:0", this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(m_StatusMessage = new QLabel(tr("Ready!"), this));
    statusbar->setContentsMargins(4, 4, 4, 4);
    statusbar->setStyleSheet("QStatusBar::item { border: none; }");
    return statusbar;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    QSettings settings;
    bool maximized = isMaximized();
    settings.setValue("app_maximized", maximized);
    if (!maximized) {
        settings.setValue("app_size", size());
    }
    settings.setValue("dock_state", saveState());
    QStringList files;
    const int total = m_ModelOpenFiles->rowCount();
    for (int i = 0; i < total; ++i) {
        files << m_ModelOpenFiles->index(i, 0).data(Qt::UserRole + 1).toString();
    }
    settings.setValue("open_files", QVariant::fromValue(files));
    settings.sync();
}

int MainWindow::findTabIndex(const QString &path)
{
    int total = m_TabEditors->count();
    for (int i = 0; i < total; i++) {
        QString path2;
        auto widget = m_TabEditors->widget(i);
        auto edit = dynamic_cast<SourceCodeEdit *>(widget);
        auto hex = dynamic_cast<HexEdit *>(widget);
        auto viewer = dynamic_cast<ImageViewerWidget *>(widget);
        if (edit) {
            path2 = edit->filePath();
        } else if (hex) {
            path2 = hex->filePath();
        } else if (viewer) {
            path2 = viewer->filePath();
        }
        if (QString::compare(path, path2) == 0) {
            return i;
        }
    }
    return -1;
}

void MainWindow::handleActionAbout()
{
    QMessageBox box;
    box.setIconPixmap(QPixmap(":/images/icon.png").scaledToWidth(128));
    QFile about(":/about.html");
    about.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream stream(&about);
    box.setInformativeText(stream.readAll());
    about.close();
    box.setText(QString("<strong>Tag</strong>: %1<br><strong>Commit</strong>: %2").arg(GIT_TAG).arg(GIT_COMMIT_FULL));
    box.setWindowTitle(tr("About"));
    box.exec();
}

void MainWindow::handleActionApk()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("Browse APK"),
                                                      QString(),
                                                      tr("Android APK File(s) (*.apk)"));
#ifdef QT_DEBUG
    qDebug() << "User selected to open" << path;
#endif
    if (!path.isEmpty()) {
        auto dialog = new ApkDecompileDialog(QDir::toNativeSeparators(path), this);
        if (dialog->exec() == QDialog::Accepted) {
            auto thread = new QThread();
            auto worker = new ApkDecompileWorker(dialog->apk(), dialog->folder(), dialog->smali(), dialog->resources(), dialog->java());
            worker->moveToThread(thread);
            connect(worker, &ApkDecompileWorker::decompileFailed, this, &MainWindow::handleDecompileFailed);
            connect(worker, &ApkDecompileWorker::decompileFinished, this, &MainWindow::handleDecompileFinished);
            connect(worker, &ApkDecompileWorker::decompileProgress, this, &MainWindow::handleDecompileProgress);
            connect(thread, &QThread::started, worker, &ApkDecompileWorker::decompile);
            connect(worker, &ApkDecompileWorker::finished, thread, &QThread::quit);
            connect(worker, &ApkDecompileWorker::finished, worker, &QObject::deleteLater);
            connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
            m_ProgressDialog = new QProgressDialog(this);
            m_ProgressDialog->setCancelButton(nullptr);
            m_ProgressDialog->setRange(0, 100);
            m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
            m_ProgressDialog->setWindowTitle(tr("Decompiling..."));
            m_ProgressDialog->exec();
        }
        dialog->deleteLater();
    }
}

void MainWindow::handleActionBuild()
{
    auto active = m_ProjectsTree->currentItem();
    if (!active) {
        active = m_ProjectsTree->topLevelItem(0);
    }
    while (active->data(0, Qt::UserRole + 1).toInt() != Project) {
        active = active->parent();
    }
    QSettings settings;
    auto appt2 = settings.value("use_aapt2", true).toBool();
    auto thread = new QThread();
    auto worker = new ApkRecompileWorker(active->data(0, Qt::UserRole + 2).toString(), appt2);
    worker->moveToThread(thread);
    connect(worker, &ApkRecompileWorker::recompileFailed, this, &MainWindow::handleRecompileFailed);
    connect(worker, &ApkRecompileWorker::recompileFinished, this, &MainWindow::handleRecompileFinished);
    connect(thread, &QThread::started, worker, &ApkRecompileWorker::recompile);
    connect(worker, &ApkRecompileWorker::finished, thread, &QThread::quit);
    connect(worker, &ApkRecompileWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    m_ProgressDialog = new QProgressDialog(this);
    m_ProgressDialog->setCancelButton(nullptr);
    m_ProgressDialog->setLabelText(tr("Running apktool..."));
    m_ProgressDialog->setRange(0, 100);
    m_ProgressDialog->setValue(50);
    m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
    m_ProgressDialog->setWindowTitle(tr("Recompiling..."));
    m_ProgressDialog->exec();
}

void MainWindow::handleActionClose()
{
    int i = m_TabEditors->currentIndex();
    if (i >= 0) {
        handleTabCloseRequested(i);
    }
}

void MainWindow::handleActionCloseAll()
{
    int i = m_TabEditors->count();
    for (int j = --i; j >= 0; j--) {
        handleTabCloseRequested(j);
    }
}

void MainWindow::handleActionContribute()
{
    QDesktopServices::openUrl(QUrl(URL_CONTRIBUTE));
}

void MainWindow::handleActionCopy()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    edit->copy();
}

void MainWindow::handleActionCut()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    edit->cut();
}

void MainWindow::handleActionDocumentation()
{
    QDesktopServices::openUrl(QUrl(URL_DOCUMENTATION));
}

void MainWindow::handleActionFile()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("Browse File"),
                                                      QString());
#ifdef QT_DEBUG
    qDebug() << "User selected to open" << path;
#endif
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::handleActionFind()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    openFindReplaceDialog(edit, false);
}

void MainWindow::handleActionFolder()
{
    QSettings settings;
    const QString project = settings.value("open_project").toString();
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("Browse Folder (apktool.yml)"),
                                                      project,
                                                      tr("Apktool Project File(s) (apktool.yml)"));
#ifdef QT_DEBUG
    qDebug() << "User selected to open" << path;
#endif
    if (!path.isEmpty()) {
        openProject(QFileInfo(path).dir().absolutePath());
    }
}

void MainWindow::handleActionGoto()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    QTextCursor cursor = edit->textCursor();
    const int line = QInputDialog::getInt(this, tr("Go To"), tr("Enter a line number:"), cursor.blockNumber() + 1, 1, edit->document()->lineCount());
    if (line > 0) {
        edit->gotoLine(line);
    }
}

void MainWindow::handleActionInstall()
{
    auto selected = m_ProjectsTree->selectedItems().first();
    const QString path = selected->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
    qDebug() << "User wishes to install" << path;
#endif
    auto thread = new QThread();
    auto worker = new AdbInstallWorker(path);
    worker->moveToThread(thread);
    connect(worker, &AdbInstallWorker::installFailed, this, &MainWindow::handleInstallFailed);
    connect(worker, &AdbInstallWorker::installFinished, this, &MainWindow::handleInstallFinished);
    connect(thread, &QThread::started, worker, &AdbInstallWorker::install);
    connect(worker, &AdbInstallWorker::finished, thread, &QThread::quit);
    connect(worker, &AdbInstallWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    m_ProgressDialog = new QProgressDialog(this);
    m_ProgressDialog->setCancelButton(nullptr);
    m_ProgressDialog->setLabelText(tr("Running adb install..."));
    m_ProgressDialog->setRange(0, 100);
    m_ProgressDialog->setValue(50);
    m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
    m_ProgressDialog->setWindowTitle(tr("Installing..."));
    m_ProgressDialog->exec();
}

void MainWindow::handleActionPaste()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    if (edit->canPaste()) {
        edit->paste();
    }
}

void MainWindow::handleActionQuit()
{
    close();
}

void MainWindow::handleActionRedo()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    edit->redo();
}

void MainWindow::handleActionReplace()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    openFindReplaceDialog(edit, false);
}

void MainWindow::handleActionReportIssues()
{
    QDesktopServices::openUrl(QUrl(URL_ISSUES));
}

void MainWindow::handleActionSave()
{
    auto i = m_TabEditors->currentIndex();
    if (i >= 0) {
        saveTab(i);
    }
}

void MainWindow::handleActionSaveAll()
{
    int i = m_TabEditors->count();
    for (int j = 0; j < i; j++) {
        saveTab(j);
    }
}

void MainWindow::handleActionSayThanks()
{
    QDesktopServices::openUrl(QUrl(URL_THANKS));
}

void MainWindow::handleActionSettings()
{
    (new SettingsDialog(0, this))->exec();
}

void MainWindow::handleActionSign()
{
    auto selected = m_ProjectsTree->selectedItems().first();
    const QString path = selected->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
    qDebug() << "User wishes to sign" << path;
#endif
    auto dialog = new SigningConfigDialog(this);
    if (dialog->exec() == QDialog::Accepted) {
        QSettings settings;
        auto thread = new QThread();
        auto worker = new ApkSignWorker(
                    path,
                    settings.value("signing_keystore").toString(),
                    settings.value("signing_keystore_password").toString(),
                    settings.value("signing_alias").toString(),
                    settings.value("signing_alias_password").toString(),
                    settings.value("signing_zipalign", true).toBool());
        worker->moveToThread(thread);
        connect(worker, &ApkSignWorker::signFailed, this, &MainWindow::handleSignFailed);
        connect(worker, &ApkSignWorker::signFinished, this, &MainWindow::handleSignFinished);
        connect(thread, &QThread::started, worker, &ApkSignWorker::sign);
        connect(worker, &ApkSignWorker::finished, thread, &QThread::quit);
        connect(worker, &ApkSignWorker::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
        m_ProgressDialog = new QProgressDialog(this);
        m_ProgressDialog->setCancelButton(nullptr);
        m_ProgressDialog->setLabelText(tr("Running uber-apk-signer..."));
        m_ProgressDialog->setRange(0, 100);
        m_ProgressDialog->setValue(50);
        m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
        m_ProgressDialog->setWindowTitle(tr("Signing..."));
        m_ProgressDialog->exec();
    }
}

void MainWindow::handleActionAutoEveryThing()
{
    auto selected = m_ProjectsTree->selectedItems().first();
    const QString path = selected->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
    qDebug() << "User wishes to install" << path;
#endif

    auto active = m_ProjectsTree->currentItem();
    if (!active) {
        active = m_ProjectsTree->topLevelItem(0);
    }
    while (active->data(0, Qt::UserRole + 1).toInt() != Project) {
        active = active->parent();
    }
    QSettings settings;
    auto appt2 = settings.value("use_aapt2", true).toBool();
    auto thread = new QThread();
    auto worker = new ApkEverythingWorker(active->data(0, Qt::UserRole + 2).toString(), appt2);
    worker->moveToThread(thread);
    connect(worker, &ApkEverythingWorker::recompileFailed, this, &MainWindow::handleRecompileFailed);
    connect(worker, &ApkEverythingWorker::recompileFinished, this, &MainWindow::handleRecompileFinished);
    connect(thread, &QThread::started, worker, &ApkEverythingWorker::recompile);
    connect(worker, &ApkEverythingWorker::finished, thread, &QThread::quit);
    connect(worker, &ApkEverythingWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    m_ProgressDialog = new QProgressDialog(this);
    m_ProgressDialog->setCancelButton(nullptr);
    m_ProgressDialog->setLabelText(tr("Running apktool..."));
    m_ProgressDialog->setRange(0, 100);
    m_ProgressDialog->setValue(50);
    m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
    m_ProgressDialog->setWindowTitle(tr("Recompiling..."));
    m_ProgressDialog->exec();
}

void MainWindow::handleActionUndo()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    edit->undo();
}

void MainWindow::handleClipboardDataChanged()
{
#ifdef QT_DEBUG
    qDebug() << "Something has changed on clipboard.";
#endif
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    if (edit) {
        m_ActionPaste->setEnabled(edit && edit->canPaste());
    }
}

void MainWindow::handleCommandFinished(const ProcessResult &result)
{
    if (!result.error.isEmpty()) {
        m_EditConsole->setTextColor(QColor(COLOR_ERROR));
        foreach (auto line, result.error) {
            m_EditConsole->append(line);
        }
    }
    if (!result.output.isEmpty()) {
        m_EditConsole->setTextColor(QColor(COLOR_OUTPUT));
        foreach (auto line, result.output) {
            m_EditConsole->append(line);
        }
    }
    m_EditConsole->setTextColor(QColor(COLOR_CODE));
    m_EditConsole->append(QString("Process exited with code %1.").arg(result.code));
    m_EditConsole->append(QString());
}

void MainWindow::handleCommandStarting(const QString &exe, const QStringList &args)
{
    QString line = "$ " + exe;
    foreach (auto arg, args) {
        QString argument(arg);
        if (arg.contains(' ')) {
            argument.prepend('"');
            argument.append('"');
        }
        line.append(' ' + arg);
    }
    m_EditConsole->setTextColor(QColor(COLOR_COMMAND));
    m_EditConsole->append(line.trimmed());
}

void MainWindow::handleCursorPositionChanged()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    if (edit) {
        QTextCursor cursor = edit->textCursor();
        const QString position = QString("%1:%2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1);
        m_StatusCursor->setText(position);
    } else {
        m_StatusCursor->setText("0:0");
    }
}

void MainWindow::handleDecompileFailed(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("Decompilation failed."));
}

void MainWindow::handleDecompileFinished(const QString &apk, const QString &folder)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("Decompilation finished."));
    openProject(folder);
}

void MainWindow::handleDecompileProgress(const int percent, const QString &message)
{
    m_ProgressDialog->setLabelText(message);
    m_ProgressDialog->setValue(percent);
}

void MainWindow::handleFilesSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected)
    if (!selected.isEmpty()) {
        auto index = selected.indexes().first();
        const QString path = index.data(Qt::UserRole + 1).toString();
        m_TabEditors->setCurrentIndex(findTabIndex(path));
    }
}

void MainWindow::handleInstallFailed(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("Installation failed."));
}

void MainWindow::handleInstallFinished(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("Installation finished."));
}

void MainWindow::handleRecompileFailed(const QString &folder)
{
    Q_UNUSED(folder)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("Recompilation failed."));
}

void MainWindow::handleRecompileFinished(const QString &folder)
{
    Q_UNUSED(folder)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("Recompilation finished."));
    QTreeWidgetItem *focus = nullptr;
    for (int i = 0; i < m_ProjectsTree->topLevelItemCount(); i++) {
        auto parent = m_ProjectsTree->topLevelItem(i);
        if (folder != parent->data(0, Qt::UserRole + 2).toString()) {
            continue;
        }
#ifdef QT_DEBUG
        qDebug() << "Found project" << folder;
#endif
        reloadChildren(parent);
        auto dist = m_ProjectsTree->findItems(".apk", Qt::MatchEndsWith | Qt::MatchRecursive, 0);
        foreach (auto child, dist) {
            if (child->data(0, Qt::UserRole + 1).toInt() == File) {
                const QString path = child->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
                qDebug() << "Found file" << path;
#endif
                if (path.startsWith(folder)) {
                    focus = child;
                    break;
                }
            }
        }
    }
    if (focus) {
        auto parent = focus->parent();
        while (parent) {
            if (!parent->isExpanded()) {
                m_ProjectsTree->expandItem(parent);
            }
            parent = parent->parent();
        }
        m_ProjectsTree->scrollToItem(focus);
        m_ProjectsTree->selectionModel()->clearSelection();
        focus->setSelected(true);
    }
}

void MainWindow::handleSignFailed(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("Signing failed."));
}

void MainWindow::handleSignFinished(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("Signing finished."));
    auto selected = m_ProjectsTree->selectedItems().first();
    auto parent = selected->parent();
    reloadChildren(parent);
    selected = parent->child(0);
    m_ProjectsTree->scrollToItem(selected);
    m_ProjectsTree->selectionModel()->clearSelection();
    selected->setSelected(true);
}

void MainWindow::handleTabChanged(const int index)
{
#ifdef QT_DEBUG
    qDebug() << "User changed current tab" << index;
#endif
    QString path;
    auto widget = m_TabEditors->currentWidget();
    auto edit = dynamic_cast<SourceCodeEdit *>(widget);
    auto hex = dynamic_cast<HexEdit *>(widget);
    auto viewer = dynamic_cast<ImageViewerWidget *>(widget);
    if (edit) {
        path = edit->filePath();
    } else if (hex) {
        path = hex->filePath();
    } else if (viewer) {
        path = viewer->filePath();
    }
    const int total = m_ModelOpenFiles->rowCount();
    for (int i = 0; i < total; ++i) {
        const QModelIndex &mindex = m_ModelOpenFiles->index(i, 0);
        if (QString::compare(mindex.data(Qt::UserRole + 1).toString(), path) == 0) {
            m_ListOpenFiles->setCurrentIndex(mindex);
            break;
        }
    }
    m_ActionClose->setEnabled(index >= 0);
    m_ActionCloseAll->setEnabled(index >= 0);
    m_ActionCopy->setEnabled(false);
    m_ActionCut->setEnabled(false);
    m_ActionPaste->setEnabled(false);
    m_ActionRedo->setEnabled(false);
    m_ActionUndo->setEnabled(false);
    m_ActionFind->setEnabled(edit);
    m_ActionReplace->setEnabled(edit);
    m_ActionSave->setEnabled(edit || hex);
    m_ActionSaveAll->setEnabled(edit || hex);
    m_ActionGoto->setEnabled(edit);
    for (auto conn: m_EditorConnections) {
        disconnect(conn);
    }
    m_EditorConnections.clear();
    if (edit) {
        m_EditorConnections << connect(edit, &QPlainTextEdit::copyAvailable, m_ActionCopy, &QAction::setEnabled);
        m_EditorConnections << connect(edit, &QPlainTextEdit::copyAvailable, m_ActionCut, &QAction::setEnabled);
        m_EditorConnections << connect(edit, &QPlainTextEdit::redoAvailable, m_ActionRedo, &QAction::setEnabled);
        m_EditorConnections << connect(edit, &QPlainTextEdit::undoAvailable, m_ActionUndo, &QAction::setEnabled);
        m_EditorConnections << connect(edit, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::handleCursorPositionChanged);
        bool selected = !edit->textCursor().selection().isEmpty();
        m_ActionCut->setEnabled(selected);
        m_ActionCopy->setEnabled(selected);
        m_ActionPaste->setEnabled(edit->canPaste());
        m_ActionRedo->setEnabled(edit->document()->isRedoAvailable());
        m_ActionUndo->setEnabled(edit->document()->isUndoAvailable());
        if (m_FindReplaceDialog) {
            m_FindReplaceDialog->setTextEdit(edit);
        }
    }
    handleCursorPositionChanged();
}

void MainWindow::handleTabCloseRequested(const int index)
{
#ifdef QT_DEBUG
    qDebug() << "User requested to close tab" << index;
#endif
    QString path;
    auto widget = m_TabEditors->widget(index);
    auto edit = dynamic_cast<SourceCodeEdit *>(widget);
    auto hex = dynamic_cast<HexEdit *>(widget);
    auto viewer = dynamic_cast<ImageViewerWidget *>(widget);
    if (edit) {
        path = edit->filePath();
    } else if (hex) {
        path = hex->filePath();
    } else if (viewer) {
        path = viewer->filePath();
    }
    const int total = m_ModelOpenFiles->rowCount();
    for (int i = 0; i < total; ++i) {
        const QModelIndex &mindex = m_ModelOpenFiles->index(i, 0);
        if (QString::compare(mindex.data(Qt::UserRole + 1).toString(), path) == 0) {
            m_ModelOpenFiles->removeRow(mindex.row());
            break;
        }
    }
    m_ActionUndo->setEnabled(false);
    m_ActionRedo->setEnabled(false);
    m_ActionCut->setEnabled(false);
    m_ActionCopy->setEnabled(false);
    m_ActionPaste->setEnabled(false);
    m_TabEditors->removeTab(index);
    if (m_TabEditors->count() == 0) {
        m_CentralStack->setCurrentIndex(0);
    }
}

void MainWindow::handleTreeContextMenu(const QPoint &point)
{
    QMenu menu(this);
    auto item = m_ProjectsTree->itemAt(point);
    if (item) {
        const int type = item->data(0, Qt::UserRole + 1).toInt();
        const QString path = item->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
        qDebug() << "Context menu requested for" << item->text(0) << "at" << point;
#endif
        if (type == File) {
            auto open = menu.addAction(tr("Open"));
            connect(open, &QAction::triggered, [=] {
                openFile(path);
            });
        }
#ifdef Q_OS_WIN
        auto openin = menu.addAction(tr("Open in Explorer"));
        connect(openin, &QAction::triggered, [=] {
            QStringList args;
            if (type == File) {
                args << QLatin1String("/select,");
            }
            args << QDir::toNativeSeparators(path);
            QProcess::startDetached("explorer.exe", args);
        });
#elif defined(Q_OS_MACOS)
        auto openin = menu.addAction(tr("Open in Finder"));
        connect(openin, &QAction::triggered, [=] {
            QStringList args;
            args << "-e" << QString("tell application \"Finder\" to reveal POSIX file \"%1\"").arg(path);
            QProcess::execute("/usr/bin/osascript", args);
            args.clear();
            args << "-e" << "tell application \"Finder\" to activate";
            QProcess::execute("/usr/bin/osascript", args);
        });
#else
        auto openin = menu.addAction(tr("Open in Files"));
        connect(openin, &QAction::triggered, [=] {
            QProcess::startDetached("xdg-open", QStringList() << path);
        });
#endif
        menu.addSeparator();
        auto build = menu.addAction(tr("Build"));
        connect(build, &QAction::triggered, this, &MainWindow::handleActionBuild);
        if (path.endsWith(".apk")) {
            menu.addSeparator();
            auto install = menu.addAction(tr("Install"));
            connect(install, &QAction::triggered, this, &MainWindow::handleActionInstall);
            auto sign = menu.addAction(tr("Sign / Export"));
            connect(sign, &QAction::triggered, this, &MainWindow::handleActionSign);

            // auto autoDo = menu.addAction(tr("一键搞定"));
            // connect(autoDo, &QAction::triggered, this, &MainWindow::handleActionAutoEveryThing);            
        }
        menu.addSeparator();
        if (type != File) {
            auto refresh = menu.addAction(tr("Refresh"));
            connect(refresh, &QAction::triggered, [=] {
                reloadChildren(item);
            });
        }

        if(path.endsWith("-decompiled")) {
            auto autosteps = menu.addAction(tr("MLBB 一键三连"));
            connect(autosteps, &QAction::triggered, this, &MainWindow::handleActionAutoEveryThing);
        }

    } else {
        auto apk = menu.addAction(tr("Open APK"));
        connect(apk, &QAction::triggered, this, &MainWindow::handleActionApk);
        auto folder = menu.addAction(tr("Open Folder"));
        connect(folder, &QAction::triggered, this, &MainWindow::handleActionFolder);
        menu.addSeparator();
    }
    auto collapse = menu.addAction(tr("Collapse All"));
    if (m_ProjectsTree->topLevelItemCount() == 0) {
        collapse->setEnabled(false);
    } else {
        connect(collapse, &QAction::triggered, m_ProjectsTree, &QTreeWidget::collapseAll);
    }

    menu.exec(m_ProjectsTree->mapToGlobal(point));
}

void MainWindow::handleTreeDoubleClicked(const QModelIndex &index)
{
    const int type = index.data(Qt::UserRole + 1).toInt();
    const QString path = index.data(Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
    qDebug() << "User double clicked" << path;
#endif
    switch (type) {
    case Project:
    case Folder:
        break;
    case File:
        openFile(path);
        break;
    }
}

void MainWindow::handleTreeSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected)
    bool apk = false;
    if (!selected.isEmpty()) {
        auto index = selected.indexes().first();
        const int type = index.data(Qt::UserRole + 1).toInt();
        const QString path = index.data(Qt::UserRole + 2).toString();
        apk = (type == File) && path.endsWith(".apk");
    }
    m_ActionInstall1->setEnabled(apk);
    m_ActionInstall2->setEnabled(apk);
    m_ActionSign->setEnabled(apk);
}

void MainWindow::openFile(const QString &path)
{
#ifdef QT_DEBUG
    qDebug() << "Opening" << path;
#endif
    const int total = m_ModelOpenFiles->rowCount();
    for (int i = 0; i < total; ++i) {
        const QModelIndex &index = m_ModelOpenFiles->index(i, 0);
        if (QString::compare(index.data(Qt::UserRole + 1).toString(), path) == 0) {
            m_TabEditors->setCurrentIndex(findTabIndex(path));
            return;
        }
    }
    QFileInfo info(path);
    QWidget *widget;
    const QString extension = info.suffix();
    if (!extension.isEmpty() && QString(IMAGE_EXTENSIONS).contains(extension, Qt::CaseInsensitive)) {
        auto viewer = new ImageViewerWidget(this);
        viewer->open(path);
        viewer->zoomReset();
        widget = viewer;
    } else if (!extension.isEmpty() && QString(TEXT_EXTENSIONS).contains(extension, Qt::CaseInsensitive)) {
        auto editor = new SourceCodeEdit(this);
        editor->open(path);
        widget = editor;
    } else {
        auto hex = new HexEdit(this);
        hex->open(path);
        widget = hex;
    }
    const QIcon icon = m_FileIconProvider.icon(info);
    auto item = new QStandardItem(icon, info.fileName());
    item->setData(path, Qt::UserRole + 1);
    m_ModelOpenFiles->appendRow(item);
    const int i = m_TabEditors->addTab(widget, icon, info.fileName());
    m_TabEditors->setCurrentIndex(i);
    m_TabEditors->setTabToolTip(i, path);
    if (m_CentralStack->currentIndex() != 1) {
        m_CentralStack->setCurrentIndex(1);
    }
    m_ActionClose->setEnabled(true);
    m_ActionCloseAll->setEnabled(true);
}

void MainWindow::openFindReplaceDialog(QPlainTextEdit *edit, const bool replace)
{
    if (!m_FindReplaceDialog) {
        m_FindReplaceDialog = new FindReplaceDialog(replace, this);
        connect(m_FindReplaceDialog, &QDialog::finished, [=] {
            m_FindReplaceDialog->deleteLater();
            m_FindReplaceDialog = nullptr;
        });
        m_FindReplaceDialog->show();
    }
    m_FindReplaceDialog->setTextEdit(edit);
}

void MainWindow::openProject(const QString &folder, const bool last)
{
    QSettings settings;
    settings.setValue("open_project", folder);
    settings.sync();
    QFileInfo info(folder);
    QTreeWidgetItem *item = new QTreeWidgetItem(m_ProjectsTree);
    item->setData(0, Qt::UserRole + 1, Project);
    item->setData(0, Qt::UserRole + 2, folder);
    item->setIcon(0, m_FileIconProvider.icon(info));
    item->setText(0, info.fileName());
    reloadChildren(item);
    m_ProjectsTree->addTopLevelItem(item);
    m_ProjectsTree->expandItem(item);
    m_ActionBuild1->setEnabled(true);
    m_ActionBuild2->setEnabled(true);
    QDir dir(folder);
    if (!last) {
        const QString manifest = dir.filePath("AndroidManifest.xml");
        if (QFile::exists(manifest)) {
            openFile(manifest);
        }
    }
}

void MainWindow::reloadChildren(QTreeWidgetItem *item)
{
    while (item->childCount()) {
        qDeleteAll(item->takeChildren());
    }
    QDir dir(item->data(0, Qt::UserRole + 2).toString());
    if (dir.exists()) {
        QFileInfoList files = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst);
        foreach (auto info, files) {
            QTreeWidgetItem *child = new QTreeWidgetItem(item);
            child->setData(0, Qt::UserRole + 1, info.isDir() ? Folder : File);
            child->setData(0, Qt::UserRole + 2, info.absoluteFilePath());
            child->setIcon(0, m_FileIconProvider.icon(info));
            child->setText(0, info.fileName());
            const QString tooltip = QString("%1 - %2")
                    .arg(QDir::toNativeSeparators(info.filePath()))
                    .arg(QLocale::system().formattedDataSize(info.size(), 2, QLocale::DataSizeTraditionalFormat));
            child->setToolTip(0, tooltip);
            item->addChild(child);
            if (info.isDir()) {
                reloadChildren(child);
            }
        }
    }
}

bool MainWindow::saveTab(int i)
{
    auto widget = m_TabEditors->widget(i);
    auto edit = dynamic_cast<SourceCodeEdit *>(widget);
    if (edit) {
        return edit->save();
    } else {
        auto hex = dynamic_cast<HexEdit *>(widget);
        if (hex) {
            return hex->save();
        }
    }
    return true;
}

MainWindow::~MainWindow()
{
}
