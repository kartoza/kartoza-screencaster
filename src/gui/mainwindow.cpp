#include "gui/mainwindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <QTimer>
#include <QCursor>
#include <QFrame>
#include <QCloseEvent>
#include <QHideEvent>
#include <QShowEvent>
#include "gui/canvas.h"

MainWindow::MainWindow(const QString &version, QWidget *parent)
    : QMainWindow(parent), m_version(version) {
    setWindowTitle("Kartoza Screencaster");
    setMinimumSize(1000, 700);

    // Global dark theme + hide tooltips
    setStyleSheet(R"(
        QToolTip { border:0; padding:0; background:transparent; color:transparent; max-height:0; max-width:0; }
        QDialog, QFileDialog, QColorDialog, QInputDialog, QMessageBox {
            background-color: #1e1e2e; color: #cdd6f4;
        }
        QDialog QLabel { color: #cdd6f4; }
        QDialog QLineEdit, QDialog QSpinBox { background: #313244; color: #cdd6f4; border: 1px solid #45475a; padding: 4px; }
        QDialog QPushButton { background: #45475a; color: #cdd6f4; border: 1px solid #585b70; border-radius: 3px; padding: 4px 12px; }
        QDialog QPushButton:hover { background: #585b70; }
        QDialog QComboBox { background: #313244; color: #cdd6f4; border: 1px solid #45475a; padding: 4px; }
        QDialog QTreeView, QDialog QListView { background: #1e1e2e; color: #cdd6f4; border: 1px solid #45475a; }
        QDialog QHeaderView::section { background: #313244; color: #cdd6f4; border: 1px solid #45475a; padding: 4px; }
        QDialog QGroupBox { color: #cdd6f4; font-weight: bold; }
    )");

    setupUI();
}

void MainWindow::setupUI() {
    auto *central = new QWidget;
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *topArea = new QWidget;
    auto *topLayout = new QHBoxLayout(topArea);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);

    topLayout->addWidget(createSidebar());

    m_content = new QStackedWidget;
    m_content->setStyleSheet("background-color: #1e1e2e;");

    m_recordPage = new RecordPage;
    m_historyPage = new HistoryPage;
    m_settingsPage = new SettingsPage;
    m_processingPage = new ProcessingPage;

    m_content->addWidget(m_recordPage);
    m_content->addWidget(m_historyPage);
    m_content->addWidget(m_settingsPage);
    m_content->addWidget(m_processingPage);

    topLayout->addWidget(m_content, 1);
    mainLayout->addWidget(topArea, 1);
    mainLayout->addWidget(createFooter());

    auto *sb = new QStatusBar;
    sb->showMessage("Idle");
    setStatusBar(sb);

    setCentralWidget(central);

    // Connect recorder signals — this is native Qt signals/slots, works across threads!
    connect(m_recordPage, &RecordPage::recordingStarted, this, [this]() {
        hide();
        QMainWindow::statusBar()->showMessage("Recording...");
    });
    connect(m_recordPage, &RecordPage::recordingStopped, this, [this]() {
        show();
        raise();
        navigateTo(PageProcessing);
        m_processingPage->startMonitoring(m_recordPage->recorder());
    });
    connect(m_processingPage, &ProcessingPage::processingDone, this, [this](bool success) {
        m_historyPage->refresh();
        if (success) {
            QMainWindow::statusBar()->showMessage("Processing complete");
        } else {
            QMainWindow::statusBar()->showMessage("Processing finished with errors");
        }
        // Don't auto-navigate — user can click "Back to History" button
    });
    connect(m_processingPage, &ProcessingPage::backToHistory, this, [this]() {
        m_historyPage->refresh();
        navigateTo(PageHistory);
    });

    // Reprocess from history
    connect(m_historyPage, &HistoryPage::reprocessRequested, this, [this](const QString &folder) {
        auto *recorder = m_recordPage->recorder();
        navigateTo(PageProcessing);
        m_processingPage->startMonitoring(recorder);
        recorder->reprocess(folder);
        QMainWindow::statusBar()->showMessage("Reprocessing...");
    });

    // Tray
    m_tray = new Tray(this, m_recordPage);

    navigateTo(PageRecord);

    // Help poller
    m_helpPoller = new QTimer(this);
    connect(m_helpPoller, &QTimer::timeout, this, [this]() {
        auto *widget = QApplication::widgetAt(QCursor::pos());
        if (!widget) return;
        QString tip = widget->toolTip();
        auto *parent = widget->parentWidget();
        while (parent && tip.isEmpty()) {
            tip = parent->toolTip();
            parent = parent->parentWidget();
        }
        if (!tip.isEmpty()) {
            m_helpLabel->setText(tip);
        }
    });
    m_helpPoller->start(200);
}

QWidget *MainWindow::createSidebar() {
    auto *sidebar = new QWidget;
    sidebar->setFixedWidth(200);
    sidebar->setStyleSheet(R"(
        QWidget { background-color: #181825; color: #cdd6f4; }
        QPushButton { background-color: transparent; color: #cdd6f4; border: none; padding: 12px 20px; text-align: left; font-size: 14px; }
        QPushButton:hover { background-color: #313244; }
        QPushButton:checked { background-color: #45475a; color: #89b4fa; font-weight: bold; }
    )");

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *title = new QLabel("Kartoza\nScreencaster");
    title->setStyleSheet("color: #89b4fa; font-size: 16px; font-weight: bold; padding: 20px;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *ver = new QLabel("v" + m_version);
    ver->setStyleSheet("color: #6c7086; font-size: 11px; padding: 0 20px 15px 20px;");
    ver->setAlignment(Qt::AlignCenter);
    layout->addWidget(ver);

    m_btnRecord = new QPushButton("Record");
    m_btnRecord->setCheckable(true);
    m_btnRecord->setChecked(true);
    m_btnRecord->setToolTip("Set up and start a new recording.");
    connect(m_btnRecord, &QPushButton::clicked, this, [this]() { navigateTo(PageRecord); });
    layout->addWidget(m_btnRecord);

    m_btnHistory = new QPushButton("History");
    m_btnHistory->setCheckable(true);
    m_btnHistory->setToolTip("Browse past recordings.");
    connect(m_btnHistory, &QPushButton::clicked, this, [this]() { navigateTo(PageHistory); });
    layout->addWidget(m_btnHistory);

    m_btnSettings = new QPushButton("Settings");
    m_btnSettings->setCheckable(true);
    m_btnSettings->setToolTip("Configure application settings.");
    connect(m_btnSettings, &QPushButton::clicked, this, [this]() { navigateTo(PageSettings); });
    layout->addWidget(m_btnSettings);

    layout->addStretch();

    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #313244;");
    layout->addWidget(sep);

    auto *helpTitle = new QLabel("Help");
    helpTitle->setStyleSheet("color: #585b70; font-size: 11px; font-weight: bold; padding: 5px 10px 0 10px;");
    layout->addWidget(helpTitle);

    m_helpLabel = new QLabel("Hover over any option\nfor help.");
    m_helpLabel->setStyleSheet("color: #6c7086; font-size: 11px; padding: 2px 10px 10px 10px;");
    m_helpLabel->setWordWrap(true);
    m_helpLabel->setMinimumHeight(80);
    m_helpLabel->setAlignment(Qt::AlignTop);
    layout->addWidget(m_helpLabel);

    return sidebar;
}

QWidget *MainWindow::createFooter() {
    auto *footer = new QWidget;
    footer->setFixedHeight(30);
    footer->setStyleSheet("QWidget { background-color: #11111b; } QLabel { color: #6c7086; font-size: 11px; }");

    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(10, 0, 10, 0);

    auto *label = new QLabel(R"(Made with ❤️ by <a href="https://kartoza.com" style="color:#89b4fa;">Kartoza</a> | <a href="https://github.com/sponsors/kartoza" style="color:#89b4fa;">Donate!</a> | <a href="https://github.com/kartoza/kartoza-screencaster" style="color:#89b4fa;">GitHub</a>)");
    label->setAlignment(Qt::AlignCenter);
    label->setOpenExternalLinks(true);
    label->setTextFormat(Qt::RichText);
    layout->addWidget(label);

    return footer;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    event->ignore();
    hide();
}

void MainWindow::hideEvent(QHideEvent *event) {
    // Suspend canvas previews when window is not visible
    m_recordPage->recorder(); // ensure recorder exists
    auto *canvas = m_recordPage->findChild<Canvas *>();
    if (canvas && !m_recordPage->recorder()->isRecording()) {
        canvas->suspendPreviews();
    }
    QMainWindow::hideEvent(event);
}

void MainWindow::showEvent(QShowEvent *event) {
    // Resume canvas previews when window becomes visible
    auto *canvas = m_recordPage->findChild<Canvas *>();
    if (canvas && !m_recordPage->recorder()->isRecording()) {
        canvas->resumePreviews();
    }
    QMainWindow::showEvent(event);
}

void MainWindow::navigateTo(Page page) {
    m_content->setCurrentIndex(page);
    m_btnRecord->setChecked(page == PageRecord);
    m_btnHistory->setChecked(page == PageHistory);
    m_btnSettings->setChecked(page == PageSettings);
}
