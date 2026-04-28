#include "gui/settingspage.h"
#include "config/config.h"
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
    QString labelStyle = "QLabel { color: #cdd6f4; font-size: 12px; }";
    QString sectionStyle = "QLabel { color: #89b4fa; font-size: 14px; font-weight: bold; padding-top: 8px; }";
    QString inputStyle = "QLineEdit { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 5px; }";
    QString checkStyle = "QCheckBox { color: #cdd6f4; font-size: 12px; }";
    QString btnStyle = "QPushButton { background: #45475a; color: #cdd6f4; border: none; border-radius: 4px; padding: 5px 12px; } QPushButton:hover { background: #585b70; }";

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: #1e1e2e; }");

    auto *scrollContent = new QWidget;
    auto *layout = new QVBoxLayout(scrollContent);
    layout->setContentsMargins(20, 15, 20, 15);
    layout->setSpacing(8);

    auto *title = new QLabel("Settings");
    title->setStyleSheet("QLabel { color: #cdd6f4; font-size: 18px; font-weight: bold; }");
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
    auto *browseBtn = new QPushButton("Browse");
    browseBtn->setStyleSheet(btnStyle);
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
    connect(m_normalizeCheck, &QCheckBox::stateChanged, this, [this]() { saveToConfig(); });
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
            m_titleColorSwatch->setStyleSheet(QString("background-color: %1; border: 2px solid #45475a; border-radius: 4px;").arg(hex));
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
            m_bgColorSwatch->setStyleSheet(QString("background-color: %1; border: 2px solid #45475a; border-radius: 4px;").arg(hex));
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
    auto *logoBrowseBtn = new QPushButton("Browse");
    logoBrowseBtn->setStyleSheet(btnStyle);
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
    m_topicsList->setStyleSheet("QListWidget { background: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; } QListWidget::item { padding: 3px 6px; }");
    m_topicsList->setToolTip("Recording topics/categories.");
    layout->addWidget(m_topicsList);

    auto *topicRow = new QHBoxLayout;
    m_topicInput = new QLineEdit;
    m_topicInput->setPlaceholderText("New topic...");
    m_topicInput->setStyleSheet(inputStyle);
    topicRow->addWidget(m_topicInput);

    auto *addTopicBtn = new QPushButton("Add");
    addTopicBtn->setStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px 10px; font-weight: bold; } QPushButton:hover { background: #94e2d5; }");
    connect(addTopicBtn, &QPushButton::clicked, this, [this]() {
        QString name = m_topicInput->text().trimmed();
        if (name.isEmpty()) return;
        m_topicInput->clear();
        m_topicsList->addItem(name);
        m_topicsList->sortItems();
        saveToConfig();
    });
    topicRow->addWidget(addTopicBtn);

    auto *rmTopicBtn = new QPushButton("Remove");
    rmTopicBtn->setStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px 10px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }");
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
    m_ytStatusLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 12px; }");
    layout->addWidget(m_ytStatusLabel);

    auto *ytRow = new QHBoxLayout;
    auto *ytSetupBtn = new QPushButton("Setup YouTube");
    ytSetupBtn->setStyleSheet("QPushButton { background: #a6e3a1; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px 12px; font-weight: bold; } QPushButton:hover { background: #94e2d5; }");
    ytSetupBtn->setToolTip("Enter your Google OAuth Client ID and Secret.");
    connect(ytSetupBtn, &QPushButton::clicked, this, [this, ytSetupBtn]() {
        bool ok;
        QString clientId = QInputDialog::getText(this, "YouTube Setup", "Client ID:", QLineEdit::Normal, "", &ok);
        if (!ok || clientId.isEmpty()) return;
        QString clientSecret = QInputDialog::getText(this, "YouTube Setup", "Client Secret:", QLineEdit::Normal, "", &ok);
        if (!ok || clientSecret.isEmpty()) return;
        auto &cfg = Config::instance();
        cfg.youtubeClientId = clientId;
        cfg.youtubeClientSecret = clientSecret;
        cfg.save();
        m_ytStatusLabel->setText("Status: Credentials saved");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #fab387; font-size: 12px; }");
    });
    ytRow->addWidget(ytSetupBtn);

    auto *ytDisconnectBtn = new QPushButton("Disconnect");
    ytDisconnectBtn->setStyleSheet("QPushButton { background: #f38ba8; color: #1e1e2e; border: none; border-radius: 4px; padding: 5px 12px; font-weight: bold; } QPushButton:hover { background: #eba0ac; }");
    connect(ytDisconnectBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, "Disconnect YouTube", "Remove YouTube credentials?") == QMessageBox::Yes) {
            auto &cfg = Config::instance();
            cfg.youtubeClientId.clear();
            cfg.youtubeClientSecret.clear();
            cfg.save();
            m_ytStatusLabel->setText("Status: Not connected");
            m_ytStatusLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 12px; }");
        }
    });
    ytRow->addWidget(ytDisconnectBtn);
    ytRow->addStretch();
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
    m_titleColorSwatch->setStyleSheet(QString("background-color: %1; border: 2px solid #45475a; border-radius: 4px;").arg(m_titleColor));

    m_bgColor = cfg.bgColor.isEmpty() ? "white" : cfg.bgColor;
    m_bgColorHex->setText(m_bgColor);
    m_bgColorSwatch->setStyleSheet(QString("background-color: %1; border: 2px solid #45475a; border-radius: 4px;").arg(m_bgColor));

    if (!cfg.youtubeClientId.isEmpty()) {
        m_ytStatusLabel->setText("Status: Connected");
        m_ytStatusLabel->setStyleSheet("QLabel { color: #a6e3a1; font-size: 12px; }");
    }
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

void SettingsPage::openColorDialog(const QString &title, QPushButton *swatch, QLineEdit *hexInput, QString &colorVar) {
    QColor initial(colorVar);
    QColorDialog dlg(initial, this);
    dlg.setWindowTitle(title);
    dlg.setStyleSheet(R"(
        QColorDialog, QWidget { background-color: #1e1e2e; color: #cdd6f4; }
        QLabel { color: #cdd6f4; }
        QLineEdit, QSpinBox { background: #313244; color: #cdd6f4; border: 1px solid #45475a; }
        QPushButton { background: #45475a; color: #cdd6f4; border: 1px solid #585b70; border-radius: 3px; padding: 4px 12px; }
        QPushButton:hover { background: #585b70; }
        QGroupBox { color: #cdd6f4; }
    )");
    if (dlg.exec() == QDialog::Accepted) {
        QColor c = dlg.selectedColor();
        colorVar = c.name();
        hexInput->setText(colorVar);
        swatch->setStyleSheet(QString("background-color: %1; border: 2px solid #45475a; border-radius: 4px;").arg(colorVar));
        saveToConfig();
    }
}
