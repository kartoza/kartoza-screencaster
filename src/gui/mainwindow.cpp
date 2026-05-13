#include "gui/mainwindow.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QPainter>
#include <QShowEvent>
#include <QSvgRenderer>
#include <QTimer>
#include <QVBoxLayout>
#include "gui/canvas.h"

/**
 * @brief A large circular stop button matching the systray recording icon.
 *
 * Renders the ready.svg at large size with a red dot in the center.
 * Clicking stops the recording.
 */
class StopCircleButton : public QWidget {
    Q_OBJECT
public:
    explicit StopCircleButton(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(200, 200);
        setCursor(Qt::PointingHandCursor);
        setToolTip("Click to stop recording");
        m_renderer = new QSvgRenderer(QString(":/icons/ready.svg"), this);
    }
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Render the SVG icon scaled to fill the widget
        m_renderer->render(&p, rect());
        // Red recording dot in center
        int inset = 70;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#CC0403"));
        p.drawEllipse(rect().adjusted(inset, inset, -inset, -inset));
    }
    void mousePressEvent(QMouseEvent *event) override {
        Q_UNUSED(event);
        emit clicked();
    }
private:
    QSvgRenderer *m_renderer;
};

MainWindow::MainWindow(const QString &version, QWidget *parent)
    : QMainWindow(parent), m_version(version) {
  setWindowTitle("Kartoza Screencaster");
  setMinimumSize(1000, 700);

  // Global dark theme + hide tooltips
  setStyleSheet(R"(
        QToolTip { border:0; padding:0; background:transparent; color:transparent; max-height:0; max-width:0; }
        QDialog, QFileDialog, QColorDialog, QInputDialog, QMessageBox {
            background-color: #1a1a2e; color: #e8e8ec;
        }
        QDialog QLabel { color: #e8e8ec; }
        QDialog QLineEdit, QDialog QSpinBox { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; padding: 4px; }
        QDialog QPushButton { background: #3d3d56; color: #e8e8ec; border: 1px solid #4d4d68; border-radius: 3px; padding: 4px 12px; }
        QDialog QPushButton:hover { background: #4d4d68; }
        QDialog QComboBox { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; padding: 4px 24px 4px 8px; }
        QDialog QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 20px; border: none; }
        QDialog QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid #8A8B8B; margin-right: 6px; }
        QDialog QTreeView, QDialog QListView { background: #1a1a2e; color: #e8e8ec; border: 1px solid #3d3d56; }
        QDialog QHeaderView::section { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; padding: 4px; }
        QDialog QGroupBox { color: #e8e8ec; font-weight: bold; }
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
  m_content->setStyleSheet("background-color: #1a1a2e;");

  m_recordPage = new RecordPage;
  m_historyPage = new HistoryPage;
  m_settingsPage = new SettingsPage;
  m_processingPage = new ProcessingPage;

  m_content->addWidget(m_recordPage);
  m_content->addWidget(m_historyPage);
  m_content->addWidget(m_settingsPage);
  m_content->addWidget(m_processingPage);

  // Recording-in-progress page: large stop button + elapsed timer
  m_recordingPage = new QWidget;
  m_recordingPage->setStyleSheet("background-color: #1a1a2e;");
  auto *recLayout = new QVBoxLayout(m_recordingPage);
  recLayout->setAlignment(Qt::AlignCenter);
  auto *recLabel = new QLabel("Recording in progress");
  recLabel->setStyleSheet("color: #CC0403; font-size: 20px; font-weight: bold;");
  recLabel->setAlignment(Qt::AlignCenter);
  recLayout->addWidget(recLabel);
  recLayout->addSpacing(20);
  auto *stopCircle = new StopCircleButton;
  recLayout->addWidget(stopCircle, 0, Qt::AlignCenter);
  recLayout->addSpacing(20);
  m_recordingElapsed = new QLabel("00:00:00");
  m_recordingElapsed->setStyleSheet("color: #e8e8ec; font-size: 28px; font-weight: bold; font-family: monospace;");
  m_recordingElapsed->setAlignment(Qt::AlignCenter);
  recLayout->addWidget(m_recordingElapsed);
  auto *stopHint = new QLabel("Click the circle to stop");
  stopHint->setStyleSheet("color: #8A8B8B; font-size: 12px;");
  stopHint->setAlignment(Qt::AlignCenter);
  recLayout->addWidget(stopHint);
  m_content->addWidget(m_recordingPage);

  connect(stopCircle, &StopCircleButton::clicked, m_recordPage, &RecordPage::onStopClicked);

  m_recordingTimer = new QTimer(this);
  connect(m_recordingTimer, &QTimer::timeout, this, [this]() {
    qint64 ms = m_recordPage->recorder()->elapsedMs();
    int h = ms / 3600000;
    int m = (ms / 60000) % 60;
    int s = (ms / 1000) % 60;
    m_recordingElapsed->setText(QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0')));
  });

  topLayout->addWidget(m_content, 1);
  mainLayout->addWidget(topArea, 1);
  mainLayout->addWidget(createFooter());

  auto *sb = new QStatusBar;
  sb->showMessage("Idle");
  setStatusBar(sb);

  setCentralWidget(central);

  // Hide window when countdown starts
  connect(m_recordPage, &RecordPage::countdownStarted, this, [this]() {
    hideToTray();
  });

  // Connect recorder signals — this is native Qt signals/slots, works across threads!
  connect(m_recordPage, &RecordPage::recordingStarted, this, [this]() {
    m_recordingTimer->start(200);
    QMainWindow::statusBar()->showMessage("Recording...");
  });
  connect(m_recordPage, &RecordPage::recordingStopped, this, [this]() {
    m_recordingTimer->stop();
    m_recordingElapsed->setText("00:00:00");
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
  connect(m_processingPage, &ProcessingPage::processingCancelled, this, [this]() {
    navigateTo(PageRecord);
    QMainWindow::statusBar()->showMessage("Processing cancelled");
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
    if (!widget)
      return;
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
        QWidget { background-color: #141428; color: #e8e8ec; }
        QPushButton { background-color: transparent; color: #e8e8ec; border: none; padding: 12px 20px; text-align: left; font-size: 14px; }
        QPushButton:hover { background-color: #2d2d44; }
        QPushButton:checked { background-color: #3d3d56; color: #569FC6; font-weight: bold; }
    )");

  auto *layout = new QVBoxLayout(sidebar);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto *logo = new QLabel;
  QSvgRenderer logoRenderer(QString(":/icons/kartoza.svg"));
  QPixmap logoPix(64, 64);
  logoPix.fill(Qt::transparent);
  { QPainter p(&logoPix); logoRenderer.render(&p); }
  logo->setPixmap(logoPix);
  logo->setAlignment(Qt::AlignCenter);
  logo->setStyleSheet("padding: 20px 0 8px 0;");
  layout->addWidget(logo);

  auto *title = new QLabel("Kartoza\nScreencaster");
  title->setStyleSheet("color: #569FC6; font-size: 16px; font-weight: bold; padding: 0 20px 4px 20px;");
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);

  auto *ver = new QLabel("v" + m_version);
  ver->setStyleSheet("color: #8A8B8B; font-size: 11px; padding: 0 20px 15px 20px;");
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

  auto *quitBtn = new QPushButton("Quit");
  quitBtn->setToolTip("Quit the application completely.");
  quitBtn->setStyleSheet(R"(
        QPushButton { background-color: transparent; color: #CC0403; border: none; padding: 12px 20px; text-align: left; font-size: 14px; }
        QPushButton:hover { background-color: #2d2d44; }
    )");
  connect(quitBtn, &QPushButton::clicked, this, [this]() {
    setProperty("quitting", true);
    QApplication::quit();
  });
  layout->addWidget(quitBtn);

  auto *sep = new QFrame;
  sep->setFrameShape(QFrame::HLine);
  sep->setStyleSheet("color: #2d2d44;");
  layout->addWidget(sep);

  auto *helpTitle = new QLabel("Help");
  helpTitle->setStyleSheet(
      "color: #4d4d68; font-size: 11px; font-weight: bold; padding: 5px 10px 0 10px;");
  layout->addWidget(helpTitle);

  m_helpLabel = new QLabel("Hover over any option\nfor help.");
  m_helpLabel->setStyleSheet("color: #8A8B8B; font-size: 11px; padding: 2px 10px 10px 10px;");
  m_helpLabel->setWordWrap(true);
  m_helpLabel->setMinimumHeight(80);
  m_helpLabel->setAlignment(Qt::AlignTop);
  layout->addWidget(m_helpLabel);

  return sidebar;
}

QWidget *MainWindow::createFooter() {
  auto *footer = new QWidget;
  footer->setFixedHeight(30);
  footer->setStyleSheet(
      "QWidget { background-color: #0f0f20; } QLabel { color: #8A8B8B; font-size: 11px; }");

  auto *layout = new QHBoxLayout(footer);
  layout->setContentsMargins(10, 0, 10, 0);

  auto *label = new QLabel(
      R"(Made with ❤️ by <a href="https://kartoza.com" style="color:#569FC6;">Kartoza</a> | <a href="https://github.com/sponsors/kartoza" style="color:#569FC6;">Donate!</a> | <a href="https://github.com/kartoza/kartoza-screencaster" style="color:#569FC6;">GitHub</a>)");
  label->setAlignment(Qt::AlignCenter);
  label->setOpenExternalLinks(true);
  label->setTextFormat(Qt::RichText);
  layout->addWidget(label);

  return footer;
}

void MainWindow::hideToTray() {
  if (m_hiddenToTray)
    return;
  m_hiddenToTray = true;
  m_savedGeometry = geometry();

  // Suspend canvas previews
  auto *canvas = m_recordPage->findChild<Canvas *>();
  if (canvas && !m_recordPage->recorder()->isRecording()) {
    canvas->suspendPreviews();
  }

  // On Wayland, hiding destroys the surface and you can't get it back.
  // Instead, minimize — compositor keeps the surface alive.
  showMinimized();
}

void MainWindow::showFromTray() {
  m_hiddenToTray = false;

  // Restore window state and geometry
  setWindowState(windowState() & ~Qt::WindowMinimized);
  showNormal();

  if (m_savedGeometry.isValid()) {
    setGeometry(m_savedGeometry);
  }

  raise();
  activateWindow();

  // Resume canvas previews
  auto *canvas = m_recordPage->findChild<Canvas *>();
  if (canvas && !m_recordPage->recorder()->isRecording()) {
    canvas->resumePreviews();
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (property("quitting").toBool()) {
    event->accept();
    return;
  }
  event->ignore();
  hideToTray();
}

void MainWindow::hideEvent(QHideEvent *event) {
  QMainWindow::hideEvent(event);
  m_recordPage->suspendPreviews();
}

void MainWindow::showEvent(QShowEvent *event) {
  QMainWindow::showEvent(event);
  if (m_content->currentIndex() == PageRecord)
    m_recordPage->resumePreviews();
}

void MainWindow::navigateTo(Page page) {
  int prev = m_content->currentIndex();
  m_content->setCurrentIndex(page);
  m_btnRecord->setChecked(page == PageRecord);
  m_btnHistory->setChecked(page == PageHistory);
  m_btnSettings->setChecked(page == PageSettings);

  // Suspend/resume canvas previews based on record page visibility
  if (prev == PageRecord && page != PageRecord)
    m_recordPage->suspendPreviews();
  else if (prev != PageRecord && page == PageRecord)
    m_recordPage->resumePreviews();
}

#include "mainwindow.moc"
