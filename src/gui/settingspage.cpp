#include "gui/settingspage.h"
#include "config/config.h"
#include "youtube/youtube.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFileDialog>
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <algorithm>

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent) {
    setupUI();
    loadFromConfig();
}

void SettingsPage::setupUI() {
    QString labelStyle = "QLabel { color: #e8e8ec; font-size: 12px; }";
    QString sectionStyle = "QLabel { color: #569FC6; font-size: 14px; font-weight: bold; padding-top: 8px; }";
    QString inputStyle = "QLineEdit { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; padding: 5px; }";
    QString checkStyle = "QCheckBox { color: #e8e8ec; font-size: 12px; }";
    QString btnStyle = "QPushButton { background: #3d3d56; color: #e8e8ec; border: none; border-radius: 4px; padding: 5px 12px; } QPushButton:hover { background: #4d4d68; }";

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: #1a1a2e; }");

    auto *scrollContent = new QWidget;
    auto *layout = new QVBoxLayout(scrollContent);
    layout->setContentsMargins(20, 15, 20, 15);
    layout->setSpacing(8);

    auto *title = new QLabel("Settings");
    title->setStyleSheet("QLabel { color: #e8e8ec; font-size: 18px; font-weight: bold; }");
    layout->addWidget(title);

    // --- Recording Defaults ---
    auto *recLabel = new QLabel("Recording Defaults");
    recLabel->setStyleSheet(sectionStyle);
    layout->addWidget(recLabel);

    // Output directory
    auto *outRow = new QHBoxLayout;
    auto *outLabel = new QLabel("Output directory:");
    outLabel->setStyleSheet(labelStyle);
    outLabel->setFixedWidth(120);
    outLabel->setToolTip("Directory where recordings are saved.");
    outRow->addWidget(outLabel);
    m_outputDirInput = new QLineEdit;
    m_outputDirInput->setStyleSheet(inputStyle);
    m_outputDirInput->setToolTip("Directory where recordings are saved.");
    connect(m_outputDirInput, &QLineEdit::editingFinished, this, &SettingsPage::saveToConfig);
    outRow->addWidget(m_outputDirInput);
    auto *browseBtn = new QPushButton(QString::fromUtf8("\u2026")); // ellipsis
    browseBtn->setFixedSize(28, 28);
    browseBtn->setStyleSheet(btnStyle);
    browseBtn->setToolTip("Browse for directory");
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory", m_outputDirInput->text());
        if (!dir.isEmpty()) { m_outputDirInput->setText(dir); saveToConfig(); }
    });
    outRow->addWidget(browseBtn);
    layout->addLayout(outRow);

    // Default presenter
    auto *presRow = new QHBoxLayout;
    auto *presLabel = new QLabel("Default presenter:");
    presLabel->setStyleSheet(labelStyle);
    presLabel->setFixedWidth(120);
    presLabel->setToolTip("Pre-filled in the recording form.");
    presRow->addWidget(presLabel);
    m_presenterInput = new QLineEdit;
    m_presenterInput->setStyleSheet(inputStyle);
    m_presenterInput->setPlaceholderText("Presenter name...");
    m_presenterInput->setToolTip("Pre-filled in the recording form.");
    connect(m_presenterInput, &QLineEdit::editingFinished, this, &SettingsPage::saveToConfig);
    presRow->addWidget(m_presenterInput);
    layout->addLayout(presRow);

    // --- Audio Processing ---
    auto *audioLabel = new QLabel("Audio Processing");
    audioLabel->setStyleSheet(sectionStyle);
    layout->addWidget(audioLabel);

    m_normalizeCheck = new QCheckBox("Normalize and process audio");
    m_normalizeCheck->setStyleSheet(checkStyle);
    m_normalizeCheck->setToolTip("Denoise, compress, and normalize audio after recording.");
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    connect(m_normalizeCheck, &QCheckBox::checkStateChanged, this, [this]() { saveToConfig(); });
#else
    connect(m_normalizeCheck, &QCheckBox::stateChanged, this, [this]() { saveToConfig(); });
#endif
    layout->addWidget(m_normalizeCheck);

    // --- Appearance ---
    auto *appearLabel = new QLabel("Appearance");
    appearLabel->setStyleSheet(sectionStyle);
    layout->addWidget(appearLabel);

    // Title color
    auto *tcRow = new QHBoxLayout;
    auto *tcLabel = new QLabel("Title color:");
    tcLabel->setStyleSheet(labelStyle);
    tcLabel->setFixedWidth(120);
    tcLabel->setToolTip("Color of the title text overlay on videos.");
    tcRow->addWidget(tcLabel);
    m_titleColorSwatch = new QPushButton;
    m_titleColorSwatch->setFixedSize(28, 28);
    m_titleColorSwatch->setToolTip("Click to pick a color.");
    connect(m_titleColorSwatch, &QPushButton::clicked, this, [this]() {
        openColorDialog("Select Title Color", m_titleColorSwatch, m_titleColorHex, m_titleColor);
    });
    tcRow->addWidget(m_titleColorSwatch);
    m_titleColorHex = new QLineEdit;
    m_titleColorHex->setStyleSheet(inputStyle);
    m_titleColorHex->setFixedWidth(90);
    m_titleColorHex->setPlaceholderText("#RRGGBB");
    m_titleColorHex->setToolTip("Hex color code for title text.");
    connect(m_titleColorHex, &QLineEdit::editingFinished, this, [this]() {
        QString hex = m_titleColorHex->text();
        if (QColor::isValidColorName(hex)) {
            m_titleColor = hex;
            m_titleColorSwatch->setStyleSheet(QString("background-color: %1; border: 2px solid #3d3d56; border-radius: 4px;").arg(hex));
            saveToConfig();
        }
    });
    tcRow->addWidget(m_titleColorHex);
    tcRow->addStretch();
    layout->addLayout(tcRow);

    // Background color
    auto *bgRow = new QHBoxLayout;
    auto *bgLabel = new QLabel("Background color:");
    bgLabel->setStyleSheet(labelStyle);
    bgLabel->setFixedWidth(120);
    bgLabel->setToolTip("Background color for vertical video letterboxing.");
    bgRow->addWidget(bgLabel);
    m_bgColorSwatch = new QPushButton;
    m_bgColorSwatch->setFixedSize(28, 28);
    m_bgColorSwatch->setToolTip("Click to pick a color.");
    connect(m_bgColorSwatch, &QPushButton::clicked, this, [this]() {
        openColorDialog("Select Background Color", m_bgColorSwatch, m_bgColorHex, m_bgColor);
    });
    bgRow->addWidget(m_bgColorSwatch);
    m_bgColorHex = new QLineEdit;
    m_bgColorHex->setStyleSheet(inputStyle);
    m_bgColorHex->setFixedWidth(90);
    m_bgColorHex->setPlaceholderText("#RRGGBB");
    m_bgColorHex->setToolTip("Hex color code for background.");
    connect(m_bgColorHex, &QLineEdit::editingFinished, this, [this]() {
        QString hex = m_bgColorHex->text();
        if (QColor::isValidColorName(hex)) {
            m_bgColor = hex;
            m_bgColorSwatch->setStyleSheet(QString("background-color: %1; border: 2px solid #3d3d56; border-radius: 4px;").arg(hex));
            saveToConfig();
        }
    });
    bgRow->addWidget(m_bgColorHex);
    bgRow->addStretch();
    layout->addLayout(bgRow);

    // Logo directory
    auto *logoLabel = new QLabel("Logos");
    logoLabel->setStyleSheet(sectionStyle);
    layout->addWidget(logoLabel);

    auto *logoRow = new QHBoxLayout;
    auto *logoDirLabel = new QLabel("Logo directory:");
    logoDirLabel->setStyleSheet(labelStyle);
    logoDirLabel->setFixedWidth(120);
    logoDirLabel->setToolTip("Directory to browse for logo images.");
    logoRow->addWidget(logoDirLabel);
    m_logoDirInput = new QLineEdit;
    m_logoDirInput->setStyleSheet(inputStyle);
    m_logoDirInput->setPlaceholderText("~/Pictures/Logos");
    m_logoDirInput->setToolTip("Directory to browse for logo images.");
    connect(m_logoDirInput, &QLineEdit::editingFinished, this, &SettingsPage::saveToConfig);
    logoRow->addWidget(m_logoDirInput);
    auto *logoBrowseBtn = new QPushButton(QString::fromUtf8("\u2026")); // ellipsis
    logoBrowseBtn->setFixedSize(28, 28);
    logoBrowseBtn->setStyleSheet(btnStyle);
    logoBrowseBtn->setToolTip("Browse for directory");
    connect(logoBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Logo Directory", m_logoDirInput->text());
        if (!dir.isEmpty()) { m_logoDirInput->setText(dir); saveToConfig(); }
    });
    logoRow->addWidget(logoBrowseBtn);
    layout->addLayout(logoRow);

    // --- Topics ---
    auto *topicsLabel = new QLabel("Topics");
    topicsLabel->setStyleSheet(sectionStyle);
    layout->addWidget(topicsLabel);

    m_topicsList = new QListWidget;
    m_topicsList->setMaximumHeight(100);
    m_topicsList->setStyleSheet("QListWidget { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; } QListWidget::item { padding: 3px 6px; }");
    m_topicsList->setToolTip("Recording topics/categories.");
    layout->addWidget(m_topicsList);

    auto *topicRow = new QHBoxLayout;
    m_topicInput = new QLineEdit;
    m_topicInput->setPlaceholderText("New topic...");
    m_topicInput->setStyleSheet(inputStyle);
    topicRow->addWidget(m_topicInput);

    auto *addTopicBtn = new QPushButton(QString::fromUtf8("\u002B")); // plus
    addTopicBtn->setFixedSize(28, 28);
    addTopicBtn->setStyleSheet("QPushButton { background: #06969A; color: #ffffff; border: none; border-radius: 4px; font-size: 16px; } QPushButton:hover { background: #058084; }");
    addTopicBtn->setToolTip("Add topic");
    connect(addTopicBtn, &QPushButton::clicked, this, [this]() {
        QString name = m_topicInput->text().trimmed();
        if (name.isEmpty()) return;
        m_topicInput->clear();
        m_topicsList->addItem(name);
        m_topicsList->sortItems();
        saveToConfig();
    });
    topicRow->addWidget(addTopicBtn);

    auto *rmTopicBtn = new QPushButton(QString::fromUtf8("\u2716")); // X mark
    rmTopicBtn->setFixedSize(28, 28);
    rmTopicBtn->setStyleSheet("QPushButton { background: #CC0403; color: #ffffff; border: none; border-radius: 4px; font-size: 14px; } QPushButton:hover { background: #E03030; }");
    rmTopicBtn->setToolTip("Remove topic");
    connect(rmTopicBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_topicsList->currentItem();
        if (item) { delete item; saveToConfig(); }
    });
    topicRow->addWidget(rmTopicBtn);
    layout->addLayout(topicRow);

    // --- YouTube ---
    auto *ytLabel = new QLabel("YouTube Integration");
    ytLabel->setStyleSheet(sectionStyle);
    layout->addWidget(ytLabel);

    m_ytStatusLabel = new QLabel("Status: Not connected");
    m_ytStatusLabel->setStyleSheet("QLabel { color: #8A8B8B; font-size: 12px; }");
    layout->addWidget(m_ytStatusLabel);

    m_ytChannelLabel = new QLabel;
    m_ytChannelLabel->setStyleSheet("QLabel { color: #06969A; font-size: 12px; }");
    m_ytChannelLabel->hide();
    layout->addWidget(m_ytChannelLabel);

    // Client ID
    auto *ytIdRow = new QHBoxLayout;
    auto *ytIdLabel = new QLabel("Client ID:");
    ytIdLabel->setStyleSheet(labelStyle);
    ytIdLabel->setFixedWidth(120);
    ytIdLabel->setToolTip("Google OAuth2 Client ID from Cloud Console.");
    ytIdRow->addWidget(ytIdLabel);
    m_ytClientIdInput = new QLineEdit;
    m_ytClientIdInput->setStyleSheet(inputStyle);
    m_ytClientIdInput->setPlaceholderText("Google OAuth Client ID...");
    m_ytClientIdInput->setToolTip("From Google Cloud Console > APIs & Services > Credentials.");
    connect(m_ytClientIdInput, &QLineEdit::editingFinished, this, [this]() {
        auto &cfg = Config::instance();
        cfg.youtubeClientId = m_ytClientIdInput->text().trimmed();
        cfg.save();
        refreshYouTubeStatus();
    });
    ytIdRow->addWidget(m_ytClientIdInput);
    layout->addLayout(ytIdRow);

    // Client Secret
    auto *ytSecRow = new QHBoxLayout;
    auto *ytSecLabel = new QLabel("Client Secret:");
    ytSecLabel->setStyleSheet(labelStyle);
    ytSecLabel->setFixedWidth(120);
    ytSecLabel->setToolTip("Google OAuth2 Client Secret from Cloud Console.");
    ytSecRow->addWidget(ytSecLabel);
    m_ytClientSecretInput = new QLineEdit;
    m_ytClientSecretInput->setStyleSheet(inputStyle);
    m_ytClientSecretInput->setPlaceholderText("Google OAuth Client Secret...");
    m_ytClientSecretInput->setEchoMode(QLineEdit::Password);
    m_ytClientSecretInput->setToolTip("From Google Cloud Console > APIs & Services > Credentials.");
    connect(m_ytClientSecretInput, &QLineEdit::editingFinished, this, [this]() {
        auto &cfg = Config::instance();
        cfg.youtubeClientSecret = m_ytClientSecretInput->text().trimmed();
        cfg.save();
        refreshYouTubeStatus();
    });
    ytSecRow->addWidget(m_ytClientSecretInput);
    layout->addLayout(ytSecRow);

    // YouTube instance
    m_youtube = new YouTube(this);
    connect(m_youtube, &YouTube::authenticated, this, [this]() {
        m_ytStatusLabel->setText("Status: Authenticated");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #06969A; font-size: 12px; }");
        m_youtube->fetchChannelInfo();
    });
    connect(m_youtube, &YouTube::authError, this, [this](const QString &err) {
        m_ytStatusLabel->setText("Status: Auth failed");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #CC0403; font-size: 12px; }");
        m_ytChannelLabel->setText(err);
        m_ytChannelLabel->setStyleSheet("QLabel { color: #CC0403; font-size: 11px; }");
        m_ytChannelLabel->show();
    });
    connect(m_youtube, &YouTube::channelInfoReady, this, [this](const QString &name, const QString &) {
        m_ytChannelLabel->setText("Channel: " + name);
        m_ytChannelLabel->setStyleSheet("QLabel { color: #06969A; font-size: 12px; }");
        m_ytChannelLabel->show();
    });
    connect(m_youtube, &YouTube::channelInfoError, this, [this](const QString &err) {
        m_ytChannelLabel->setText(err);
        m_ytChannelLabel->setStyleSheet("QLabel { color: #DF9E2F; font-size: 11px; }");
        m_ytChannelLabel->show();
    });
    connect(m_youtube, &YouTube::loggedOut, this, [this]() {
        refreshYouTubeStatus();
    });

    // Buttons
    auto *ytRow = new QHBoxLayout;
    auto *ytAuthBtn = new QPushButton(QString::fromUtf8("\u2714 Authenticate"));
    ytAuthBtn->setStyleSheet("QPushButton { background: #06969A; color: #ffffff; border: none; border-radius: 4px; padding: 5px 12px; font-weight: bold; } QPushButton:hover { background: #058084; }");
    ytAuthBtn->setToolTip("Open browser to authenticate with Google and grant YouTube access.");
    connect(ytAuthBtn, &QPushButton::clicked, this, [this]() {
        m_ytStatusLabel->setText("Status: Authenticating...");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #DF9E2F; font-size: 12px; }");
        m_youtube->authenticate();
    });
    ytRow->addWidget(ytAuthBtn);

    auto *ytVerifyBtn = new QPushButton(QString::fromUtf8("\u21BB Verify"));
    ytVerifyBtn->setStyleSheet(btnStyle);
    ytVerifyBtn->setToolTip("Test credentials and fetch channel info.");
    connect(ytVerifyBtn, &QPushButton::clicked, this, [this]() {
        m_ytStatusLabel->setText("Status: Verifying...");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #DF9E2F; font-size: 12px; }");
        m_youtube->fetchChannelInfo();
    });
    ytRow->addWidget(ytVerifyBtn);

    ytRow->addStretch(); // push destructive action right

    auto *ytDisconnectBtn = new QPushButton(QString::fromUtf8("\u2716 Disconnect"));
    ytDisconnectBtn->setStyleSheet("QPushButton { background: #CC0403; color: #ffffff; border: none; border-radius: 4px; padding: 5px 12px; font-weight: bold; } QPushButton:hover { background: #E03030; }");
    connect(ytDisconnectBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, "Disconnect YouTube",
                "Remove YouTube credentials and token?") == QMessageBox::Yes) {
            auto &cfg = Config::instance();
            cfg.youtubeClientId.clear();
            cfg.youtubeClientSecret.clear();
            cfg.save();
            m_youtube->logout();
            m_ytClientIdInput->clear();
            m_ytClientSecretInput->clear();
            m_ytChannelLabel->hide();
            refreshYouTubeStatus();
        }
    });
    ytRow->addWidget(ytDisconnectBtn);
    layout->addLayout(ytRow);

    layout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scrollArea);
}

void SettingsPage::loadFromConfig() {
    auto &cfg = Config::instance();
    cfg.load();

    m_outputDirInput->setText(cfg.outputDir);
    m_presenterInput->setText(cfg.defaultPresenter);
    m_normalizeCheck->setChecked(cfg.normalizeAudio);
    m_logoDirInput->setText(cfg.logoDirectory);

    m_titleColor = cfg.titleColor.isEmpty() ? "#62A4C7" : cfg.titleColor;
    m_titleColorHex->setText(m_titleColor);
    m_titleColorSwatch->setStyleSheet(QString("background-color: %1; border: 2px solid #3d3d56; border-radius: 4px;").arg(m_titleColor));

    m_bgColor = cfg.bgColor.isEmpty() ? "white" : cfg.bgColor;
    m_bgColorHex->setText(m_bgColor);
    m_bgColorSwatch->setStyleSheet(QString("background-color: %1; border: 2px solid #3d3d56; border-radius: 4px;").arg(m_bgColor));

    m_ytClientIdInput->setText(cfg.youtubeClientId);
    m_ytClientSecretInput->setText(cfg.youtubeClientSecret);
    refreshYouTubeStatus();
}

void SettingsPage::saveToConfig() {
    auto &cfg = Config::instance();
    cfg.outputDir = m_outputDirInput->text();
    cfg.defaultPresenter = m_presenterInput->text();
    cfg.normalizeAudio = m_normalizeCheck->isChecked();
    cfg.logoDirectory = m_logoDirInput->text();
    cfg.titleColor = m_titleColor;
    cfg.bgColor = m_bgColor;
    cfg.save();
}

void SettingsPage::refreshYouTubeStatus() {
    auto &cfg = Config::instance();
    if (cfg.youtubeClientId.isEmpty() || cfg.youtubeClientSecret.isEmpty()) {
        m_ytStatusLabel->setText("Status: Not configured");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #8A8B8B; font-size: 12px; }");
        m_ytChannelLabel->hide();
    } else if (m_youtube->hasToken()) {
        m_ytStatusLabel->setText("Status: Connected");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #06969A; font-size: 12px; }");
    } else {
        m_ytStatusLabel->setText("Status: Credentials saved (not authenticated)");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #DF9E2F; font-size: 12px; }");
        m_ytChannelLabel->hide();
    }
}

void SettingsPage::openColorDialog(const QString &title, QPushButton *swatch, QLineEdit *hexInput, QString &colorVar) {
    QColor initial(colorVar);
    QColorDialog dlg(initial, this);
    dlg.setWindowTitle(title);
    dlg.setStyleSheet(R"(
        QColorDialog, QWidget { background-color: #1a1a2e; color: #e8e8ec; }
        QLabel { color: #e8e8ec; }
        QLineEdit, QSpinBox { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; }
        QPushButton { background: #3d3d56; color: #e8e8ec; border: 1px solid #4d4d68; border-radius: 3px; padding: 4px 12px; }
        QPushButton:hover { background: #4d4d68; }
        QGroupBox { color: #e8e8ec; }
    )");
    if (dlg.exec() == QDialog::Accepted) {
        QColor c = dlg.selectedColor();
        colorVar = c.name();
        hexInput->setText(colorVar);
        swatch->setStyleSheet(QString("background-color: %1; border: 2px solid #3d3d56; border-radius: 4px;").arg(colorVar));
        saveToConfig();
    }
}
