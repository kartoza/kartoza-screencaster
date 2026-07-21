#include "gui/recordpage.h"
#include "config/config.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QMenu>
#include <QFileDialog>
#include <QDateTime>
#include <QFile>
#include <QSet>
#include <QShowEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QSpinBox>
#include <QColorDialog>
#include <QProcess>
#include <QFileInfo>
#include <QHash>

// Resolve a font file for the requested family + weight via fontconfig so the
// merger can burn in the correct weight (FFmpeg drawtext has no weight option).
// Returns empty on failure; the merger then falls back to the family name.
static QString resolveFontFile(const QString &family, int weight) {
    // Cache by family+weight so N text boxes sharing a font spawn fc-match once.
    static QHash<QString, QString> cache;
    const QString key = family + "#" + QString::number(weight);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    QString pattern = family;
    if (weight >= 600) pattern += ":weight=bold";
    else if (weight <= 300) pattern += ":weight=light";
    QProcess p;
    p.start("fc-match", {"-f", "%{file}", pattern});
    QString result;
    if (p.waitForFinished(2000)) {
        QString path = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        if (QFileInfo::exists(path)) result = path;
    }
    cache.insert(key, result);
    return result;
}

RecordPage::RecordPage(QWidget *parent) : QWidget(parent) {
    m_recorder = new Recorder(this);
    m_monitors = Monitor::listMonitors();
    m_webcams = Webcam::detectAll();

    setupUI();

    // Connect recorder signals — native Qt, works perfectly across threads
    connect(m_recorder, &Recorder::recordingStarted, this, &RecordPage::onRecorderStarted);
    connect(m_recorder, &Recorder::recordingStopped, this, &RecordPage::onRecorderStopped);
    connect(m_recorder, &Recorder::recordingError, this, &RecordPage::onRecorderError);

    // Countdown timer
    m_countdownTimer = new QTimer(this);
    connect(m_countdownTimer, &QTimer::timeout, this, &RecordPage::onCountdownTick);

    // Elapsed timer - uses recorder's elapsed which handles pause/resume
    m_elapsedTimer = new QTimer(this);
    connect(m_elapsedTimer, &QTimer::timeout, this, [this]() {
        qint64 ms = m_recorder->elapsedMs();
        int h = ms / 3600000;
        int m = (ms / 60000) % 60;
        int s = (ms / 1000) % 60;
        m_elapsedLabel->setText(QString("%1:%2:%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0')));
    });
}

void RecordPage::setupUI() {
    auto &cfg = Config::instance();
    cfg.load();

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setSpacing(4);
    outerLayout->setContentsMargins(10, 10, 10, 10);

    auto *mainRow = new QWidget;
    auto *layout = new QHBoxLayout(mainRow);
    layout->setSpacing(10);
    layout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(mainRow, 1);

    QString labelStyle = "QLabel { color: #e8e8ec; font-size: 12px; }";
    QString inputStyle = "QLineEdit { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; padding: 5px; font-size: 12px; }";

    // === Left sidebar ===
    auto *leftCol = new QWidget;
    leftCol->setFixedWidth(220);
    auto *leftLayout = new QVBoxLayout(leftCol);
    leftLayout->setSpacing(6);

    auto *metaLabel = new QLabel("Recording");
    metaLabel->setStyleSheet("QLabel { color: #569FC6; font-size: 14px; font-weight: bold; }");
    leftLayout->addWidget(metaLabel);

    // Title
    auto *titleRow = new QHBoxLayout;
    auto *titleLbl = new QLabel("Title:");
    titleLbl->setStyleSheet(labelStyle);
    titleLbl->setFixedWidth(60);
    titleRow->addWidget(titleLbl);
    m_titleInput = new QLineEdit;
    m_titleInput->setPlaceholderText("Title...");
    m_titleInput->setStyleSheet(inputStyle);
    m_titleInput->setToolTip("Recording title. Appears on canvas and in filenames.");
    connect(m_titleInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_canvas->setTitle(text);
    });
    titleRow->addWidget(m_titleInput);
    leftLayout->addLayout(titleRow);

    // Number
    auto *numRow = new QHBoxLayout;
    auto *numLbl = new QLabel("Number:");
    numLbl->setStyleSheet(labelStyle);
    numLbl->setFixedWidth(60);
    numRow->addWidget(numLbl);
    m_numberInput = new QLineEdit;
    m_numberInput->setStyleSheet(inputStyle);
    m_numberInput->setText(QString("%1").arg(cfg.nextRecordingNumber(), 3, 10, QChar('0')));
    m_numberInput->setToolTip("Sequential recording number.");
    numRow->addWidget(m_numberInput);
    leftLayout->addLayout(numRow);

    // Presenter
    auto *presRow = new QHBoxLayout;
    auto *presLbl = new QLabel("Presenter:");
    presLbl->setStyleSheet(labelStyle);
    presLbl->setFixedWidth(60);
    presRow->addWidget(presLbl);
    m_presenterInput = new QLineEdit;
    m_presenterInput->setStyleSheet(inputStyle);
    m_presenterInput->setText(cfg.defaultPresenter);
    m_presenterInput->setToolTip("Presenter name.");
    presRow->addWidget(m_presenterInput);
    leftLayout->addLayout(presRow);

    // Description
    auto *descLbl = new QLabel("Description:");
    descLbl->setStyleSheet("QLabel { color: #569FC6; font-size: 12px; font-weight: bold; padding-top: 4px; }");
    leftLayout->addWidget(descLbl);
    m_descInput = new QTextEdit;
    m_descInput->setPlaceholderText("Optional description...");
    m_descInput->setStyleSheet("QTextEdit { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; padding: 4px; font-size: 12px; }");
    m_descInput->setMaximumHeight(60);
    m_descInput->setToolTip("Optional description for the recording.");
    leftLayout->addWidget(m_descInput);

    m_audioCheck = new QCheckBox("Record Audio");
    m_audioCheck->setChecked(true);
    m_audioCheck->setStyleSheet("QCheckBox { color: #e8e8ec; font-size: 12px; }");
    m_audioCheck->setToolTip("Record microphone audio.");
    leftLayout->addWidget(m_audioCheck);

    // Canvas mode
    auto *modeLabel = new QLabel("Canvas Mode");
    modeLabel->setStyleSheet("QLabel { color: #569FC6; font-size: 12px; font-weight: bold; padding-top: 6px; }");
    leftLayout->addWidget(modeLabel);

    QString radioStyle = "QRadioButton { color: #e8e8ec; font-size: 12px; }";
    m_modeGroup = new QButtonGroup(this);

    auto *landscapeRadio = new QRadioButton("Landscape 16:9");
    landscapeRadio->setChecked(true);
    landscapeRadio->setStyleSheet(radioStyle);
    m_modeGroup->addButton(landscapeRadio, 0);
    leftLayout->addWidget(landscapeRadio);

    auto *verticalRadio = new QRadioButton("Vertical 9:16");
    verticalRadio->setStyleSheet(radioStyle);
    m_modeGroup->addButton(verticalRadio, 1);
    leftLayout->addWidget(verticalRadio);

    auto *leftSplitRadio = new QRadioButton("Vertical (Left Split)");
    leftSplitRadio->setStyleSheet(radioStyle);
    m_modeGroup->addButton(leftSplitRadio, 2);
    leftLayout->addWidget(leftSplitRadio);

    auto *rightSplitRadio = new QRadioButton("Vertical (Right Split)");
    rightSplitRadio->setStyleSheet(radioStyle);
    m_modeGroup->addButton(rightSplitRadio, 3);
    leftLayout->addWidget(rightSplitRadio);

    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_canvas->setMode(id);
    });

    // Layer list
    auto *layerLabel = new QLabel("Layers");
    layerLabel->setStyleSheet("QLabel { color: #569FC6; font-size: 12px; font-weight: bold; padding-top: 6px; }");
    layerLabel->setToolTip("Canvas elements in draw order.\nArrow keys to nudge. Delete to remove.");
    leftLayout->addWidget(layerLabel);

    m_layerList = new QListWidget;
    m_layerList->setMaximumHeight(120);
    m_layerList->setStyleSheet("QListWidget { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; font-size: 11px; } QListWidget::item { padding: 3px 6px; } QListWidget::item:selected { background: #3d3d56; color: #e8e8ec; }");
    connect(m_layerList, &QListWidget::currentRowChanged, m_canvas, &Canvas::setSelectedItem);
    leftLayout->addWidget(m_layerList);

    auto *layerBtnRow = new QHBoxLayout;
    layerBtnRow->setSpacing(4);

    QString iconBtnStyle = "QPushButton { background: #3d3d56; color: #e8e8ec; border: none; border-radius: 3px; font-size: 14px; } QPushButton:hover { background: #4d4d68; }";
    QString iconDelStyle = "QPushButton { background: #CC0403; color: #ffffff; border: none; border-radius: 3px; font-size: 14px; } QPushButton:hover { background: #E03030; }";

    auto *upBtn = new QPushButton(QString::fromUtf8("\u25B2")); // up triangle
    upBtn->setFixedSize(28, 24);
    upBtn->setStyleSheet(iconBtnStyle);
    upBtn->setToolTip("Move layer up");
    connect(upBtn, &QPushButton::clicked, this, [this]() {
        int r = m_layerList->currentRow();
        if (r > 0) { m_canvas->swapItems(r, r-1); refreshLayerList(); m_layerList->setCurrentRow(r-1); }
    });
    layerBtnRow->addWidget(upBtn);

    auto *downBtn = new QPushButton(QString::fromUtf8("\u25BC")); // down triangle
    downBtn->setFixedSize(28, 24);
    downBtn->setStyleSheet(iconBtnStyle);
    downBtn->setToolTip("Move layer down");
    connect(downBtn, &QPushButton::clicked, this, [this]() {
        int r = m_layerList->currentRow();
        if (r >= 0 && r < m_layerList->count()-1) { m_canvas->swapItems(r, r+1); refreshLayerList(); m_layerList->setCurrentRow(r+1); }
    });
    layerBtnRow->addWidget(downBtn);
    layerBtnRow->addStretch(); // push destructive action right

    auto *delBtn = new QPushButton(QString::fromUtf8("\u2716")); // X mark
    delBtn->setFixedSize(28, 24);
    delBtn->setStyleSheet(iconDelStyle);
    delBtn->setToolTip("Remove layer");
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        int r = m_layerList->currentRow();
        if (r >= 0) { m_canvas->removeItem(r); refreshLayerList(); }
    });
    layerBtnRow->addWidget(delBtn);
    leftLayout->addLayout(layerBtnRow);

    // Preset management
    auto *presetLabel = new QLabel("Presets");
    presetLabel->setStyleSheet("QLabel { color: #569FC6; font-size: 12px; font-weight: bold; padding-top: 6px; }");
    leftLayout->addWidget(presetLabel);

    m_presetList = new QListWidget;
    m_presetList->setMaximumHeight(80);
    m_presetList->setStyleSheet("QListWidget { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 4px; font-size: 11px; } QListWidget::item { padding: 3px 6px; } QListWidget::item:selected { background: #3d3d56; color: #e8e8ec; } QListWidget QLineEdit { background: #3d3d56; color: #e8e8ec; border: 1px solid #569FC6; padding: 2px; }");
    leftLayout->addWidget(m_presetList);

    // Name input row (hidden until "New" is clicked)
    m_presetNameRow = new QWidget;
    auto *nameRowLayout = new QHBoxLayout(m_presetNameRow);
    nameRowLayout->setContentsMargins(0, 0, 0, 0);
    nameRowLayout->setSpacing(4);
    m_presetNameInput = new QLineEdit;
    m_presetNameInput->setPlaceholderText("New preset name...");
    m_presetNameInput->setStyleSheet(inputStyle);
    nameRowLayout->addWidget(m_presetNameInput);
    auto *createBtn = new QPushButton(QString::fromUtf8("\u2714")); // check mark
    createBtn->setFixedSize(28, 24);
    createBtn->setStyleSheet("QPushButton { background: #06969A; color: #ffffff; border: none; border-radius: 3px; font-size: 14px; } QPushButton:hover { background: #058084; }");
    createBtn->setToolTip("Create preset");
    nameRowLayout->addWidget(createBtn);
    m_presetNameRow->hide();
    leftLayout->addWidget(m_presetNameRow);

    auto *presetBtnRow = new QHBoxLayout;
    presetBtnRow->setSpacing(4);
    auto *newPresetBtn = new QPushButton(QString::fromUtf8("\u002B")); // plus
    newPresetBtn->setFixedSize(28, 24);
    newPresetBtn->setStyleSheet(iconBtnStyle);
    newPresetBtn->setToolTip("New preset");
    presetBtnRow->addWidget(newPresetBtn);
    presetBtnRow->addStretch(); // push destructive action right
    auto *delPresetBtn = new QPushButton(QString::fromUtf8("\u2716")); // X mark
    delPresetBtn->setFixedSize(28, 24);
    delPresetBtn->setStyleSheet(iconDelStyle);
    delPresetBtn->setToolTip("Delete preset");
    presetBtnRow->addWidget(delPresetBtn);
    leftLayout->addLayout(presetBtnRow);

    // Double-click preset -> rename it inline
    connect(m_presetList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        QString text = item->text();
        if (text.startsWith("* ")) text = text.mid(2);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        m_presetList->blockSignals(true);
        item->setData(Qt::UserRole, text); // stash original name
        item->setText(text); // remove the "* " prefix for editing
        m_presetList->blockSignals(false);
        m_presetList->editItem(item);
    });

    connect(m_presetList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_restoring || !item) return;
        QString oldName = item->data(Qt::UserRole).toString();
        if (oldName.isEmpty()) return; // not a rename
        QString newName = item->text().trimmed();
        item->setData(Qt::UserRole, QVariant()); // clear stash
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);

        if (newName.isEmpty() || newName == oldName) {
            refreshPresetList(); // revert display
            return;
        }

        auto &cfg = Config::instance();
        if (cfg.presets.contains(newName)) {
            refreshPresetList(); // name collision, revert
            return;
        }

        CanvasState state = cfg.presets.take(oldName);
        cfg.presets[newName] = state;
        if (cfg.activePreset == oldName) cfg.activePreset = newName;
        cfg.save();
        refreshPresetList();
        emit presetChanged();
    });

    // Click preset in list -> load it
    connect(m_presetList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_restoring || row < 0 || !m_canvas) return;
        auto &cfg = Config::instance();
        // Strip the active marker to get the name
        QString text = m_presetList->item(row)->text();
        if (text.startsWith("* ")) text = text.mid(2);
        if (!cfg.presets.contains(text)) return;
        cfg.activePreset = text;
        cfg.canvasState = cfg.presets[text];
        cfg.save();
        m_restoring = true;
        applyState(cfg.canvasState);
        m_restoring = false;
        refreshPresetList();
        m_presetNameRow->hide();
        emit presetChanged();
    });

    // New: show name input and clear canvas
    connect(newPresetBtn, &QPushButton::clicked, this, [this]() {
        m_presetNameRow->show();
        m_presetNameInput->clear();
        m_presetNameInput->setFocus();
    });

    // Create: save current canvas as a new preset
    auto doCreate = [this]() {
        QString name = m_presetNameInput->text().trimmed();
        if (name.isEmpty()) return;
        auto &cfg = Config::instance();
        cfg.canvasState = captureCurrentState();
        cfg.presets[name] = cfg.canvasState;
        cfg.activePreset = name;
        cfg.save();
        m_presetNameRow->hide();
        refreshPresetList();
        emit presetChanged();
    };
    connect(createBtn, &QPushButton::clicked, this, doCreate);
    connect(m_presetNameInput, &QLineEdit::returnPressed, this, doCreate);

    // Delete selected preset
    connect(delPresetBtn, &QPushButton::clicked, this, [this]() {
        auto &cfg = Config::instance();
        if (cfg.activePreset.isEmpty()) return;
        if (QMessageBox::question(this, "Delete Preset",
                QString("Delete preset \"%1\"?").arg(cfg.activePreset)) != QMessageBox::Yes) return;
        cfg.presets.remove(cfg.activePreset);
        cfg.activePreset.clear();
        cfg.save();
        refreshPresetList();
        emit presetChanged();
    });

    leftLayout->addStretch();
    layout->addWidget(leftCol);

    // === Center: Canvas + Controls ===
    auto *centerCol = new QWidget;
    auto *centerLayout = new QVBoxLayout(centerCol);
    centerLayout->setSpacing(8);
    centerLayout->setContentsMargins(0, 0, 0, 0);

    m_canvas = new Canvas(this);
    m_canvas->setToolTip("WYSIWYG preview.\nAdd elements with + button.\nDrag to move, scroll to resize.\nArrow keys to nudge.");
    centerLayout->addWidget(m_canvas, 1);

    // Sync canvas selection → layer list
    connect(m_canvas, &Canvas::selectionChanged, this, [this](int index) {
        if (index >= 0 && index < m_layerList->count())
            m_layerList->setCurrentRow(index);
        else
            m_layerList->setCurrentRow(-1);
    });

    // Refresh layer list and auto-save when items change
    connect(m_canvas, &Canvas::itemsChanged, this, [this]() {
        refreshLayerList();
        saveCanvasState(); // self-guards on m_restoring
    });

    // Restore saved canvas state and populate preset list
    restoreCanvasState();
    refreshPresetList();

    // Add element + controls
    auto *addRow = new QHBoxLayout;
    addRow->setSpacing(8);

    auto *addBtn = new QPushButton("+ Add Element");
    addBtn->setStyleSheet("QPushButton { background: #569FC6; color: #ffffff; border: none; border-radius: 6px; padding: 8px 16px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #4889B0; }");

    auto *addMenu = new QMenu(this);
    addMenu->setStyleSheet("QMenu { background: #1a1a2e; color: #e8e8ec; border: 1px solid #3d3d56; } QMenu::item { padding: 6px 20px; } QMenu::item:selected { background: #3d3d56; }");

    auto *screenMenu = addMenu->addMenu("Screen");
    for (const auto &mon : m_monitors) {
        QString label = mon.description.isEmpty() ? mon.name : mon.description;
        label += QString(" (%1x%2)").arg(mon.width).arg(mon.height);
        auto *action = screenMenu->addAction(label);
        connect(action, &QAction::triggered, this, [this, mon]() {
            addScreen(mon);
        });
    }

    auto *webcamMenu = addMenu->addMenu("Webcam");
    // Re-detect on every open so a stale/empty startup scan (the app can launch
    // into the tray before devices settle) never permanently hides a camera,
    // and hot-plugged cameras appear without restarting the app.
    auto populateWebcamMenu = [this, webcamMenu]() {
        webcamMenu->clear();
        m_webcams = Webcam::detectAll();
        if (m_webcams.isEmpty()) {
            auto *none = webcamMenu->addAction("No webcams detected");
            none->setEnabled(false);
            return;
        }
        for (const auto &dev : m_webcams) {
            auto *devMenu = webcamMenu->addMenu(dev.name);
            auto *roundAction = devMenu->addAction("Round bubble");
            connect(roundAction, &QAction::triggered, this, [this, dev]() { m_canvas->addWebcam(dev.device, dev.name, 0); });
            auto *squareAction = devMenu->addAction("Square");
            connect(squareAction, &QAction::triggered, this, [this, dev]() { m_canvas->addWebcam(dev.device, dev.name, 1); });
            auto *rectAction = devMenu->addAction("Rectangle");
            connect(rectAction, &QAction::triggered, this, [this, dev]() { m_canvas->addWebcam(dev.device, dev.name, 2); });
        }
    };
    populateWebcamMenu();
    connect(webcamMenu, &QMenu::aboutToShow, this, populateWebcamMenu);

    auto *logoAction = addMenu->addAction("Logo");
    connect(logoAction, &QAction::triggered, this, [this]() {
        // Raster/GIF only — FFmpeg has no SVG decoder, so an SVG logo would break
        // the render. (SVGs that still slip in via drag-drop or a saved preset are
        // rasterised to PNG at record time as a safety net.)
        QString file = QFileDialog::getOpenFileName(
            this, "Select Logo", QString(),
            "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp);;All files (*)");
        if (!file.isEmpty()) m_canvas->addLogo(file);
    });

    auto *titleAction = addMenu->addAction("Title Text");
    connect(titleAction, &QAction::triggered, this, [this]() {
        QString t = m_titleInput->text();
        if (t.isEmpty()) t = "Title";
        m_canvas->setTitle(t);
    });

    auto *textBoxAction = addMenu->addAction("Text Box");
    connect(textBoxAction, &QAction::triggered, this, [this]() {
        m_canvas->addTextBox("Text");
    });

    connect(addBtn, &QPushButton::clicked, this, [addBtn, addMenu]() {
        addMenu->popup(addBtn->mapToGlobal(QPoint(0, addBtn->height())));
    });
    addRow->addWidget(addBtn);

    // GIF loop controls (shown when a GIF logo is selected)
    m_gifLoopRow = new QWidget;
    auto *gifLayout = new QHBoxLayout(m_gifLoopRow);
    gifLayout->setContentsMargins(0, 0, 0, 0);
    gifLayout->setSpacing(4);

    QString smallBtnStyle = "QPushButton { background: #3d3d56; color: #e8e8ec; border: none; border-radius: 3px; padding: 4px 8px; font-size: 10px; } QPushButton:hover { background: #4d4d68; } QPushButton:checked { background: #569FC6; color: #ffffff; }";

    auto *loopLabel = new QLabel("GIF:");
    loopLabel->setStyleSheet("QLabel { color: #8A8B8B; font-size: 10px; }");
    gifLayout->addWidget(loopLabel);

    auto *btnCont = new QPushButton("Loop");
    btnCont->setStyleSheet(smallBtnStyle); btnCont->setCheckable(true);
    gifLayout->addWidget(btnCont);

    auto *btnOnce = new QPushButton("Once");
    btnOnce->setStyleSheet(smallBtnStyle); btnOnce->setCheckable(true);
    gifLayout->addWidget(btnOnce);

    auto *btnNone = new QPushButton("Still");
    btnNone->setStyleSheet(smallBtnStyle); btnNone->setCheckable(true);
    gifLayout->addWidget(btnNone);

    auto *loopCountSpin = new QSpinBox;
    loopCountSpin->setRange(1, 999);
    loopCountSpin->setValue(3);
    loopCountSpin->setPrefix("x");
    loopCountSpin->setFixedWidth(55);
    loopCountSpin->setStyleSheet("QSpinBox { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 3px; padding: 2px; font-size: 10px; }");
    gifLayout->addWidget(loopCountSpin);

    auto *btnNTimes = new QPushButton("N times");
    btnNTimes->setStyleSheet(smallBtnStyle); btnNTimes->setCheckable(true);
    gifLayout->addWidget(btnNTimes);

    m_gifLoopRow->hide();
    addRow->addWidget(m_gifLoopRow);

    // Update GIF controls when selection changes
    auto updateGifControls = [this, btnCont, btnOnce, btnNone, btnNTimes, loopCountSpin]() {
        int sel = m_canvas->selectedItem();
        if (sel < 0 || sel >= m_canvas->itemCount()) { m_gifLoopRow->hide(); return; }
        auto e = m_canvas->itemExport(sel);
        if (e.type != 2 || !e.filePath.toLower().endsWith(".gif")) { m_gifLoopRow->hide(); return; }

        m_gifLoopRow->show();
        int loop = e.gifLoop;
        btnCont->setChecked(loop == 2);
        btnOnce->setChecked(loop == 1);
        btnNone->setChecked(loop == 0);
        btnNTimes->setChecked(loop == 3);
        loopCountSpin->setValue(e.gifLoopMax);
        loopCountSpin->setVisible(loop == 3);
    };

    connect(m_canvas, &Canvas::selectionChanged, this, updateGifControls);
    connect(m_canvas, &Canvas::itemsChanged, this, updateGifControls);

    connect(btnCont, &QPushButton::clicked, this, [this]() {
        int s = m_canvas->selectedItem(); if (s >= 0) m_canvas->setItemGifLoop(s, 2);
    });
    connect(btnOnce, &QPushButton::clicked, this, [this]() {
        int s = m_canvas->selectedItem(); if (s >= 0) m_canvas->setItemGifLoop(s, 1);
    });
    connect(btnNone, &QPushButton::clicked, this, [this]() {
        int s = m_canvas->selectedItem(); if (s >= 0) m_canvas->setItemGifLoop(s, 0);
    });
    connect(btnNTimes, &QPushButton::clicked, this, [this, loopCountSpin]() {
        int s = m_canvas->selectedItem(); if (s >= 0) m_canvas->setItemGifLoop(s, 3, loopCountSpin->value());
    });
    connect(loopCountSpin, &QSpinBox::valueChanged, this, [this](int val) {
        int s = m_canvas->selectedItem();
        if (s >= 0) {
            if (m_canvas->itemExport(s).gifLoop == 3) m_canvas->setItemGifLoop(s, 3, val);
        }
    });

    // Text-box controls (shown when a text item is selected)
    m_textRow = new QWidget;
    auto *textLayout = new QHBoxLayout(m_textRow);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(4);

    m_textContentInput = new QLineEdit;
    m_textContentInput->setPlaceholderText("Text...");
    m_textContentInput->setStyleSheet(inputStyle);
    m_textContentInput->setFixedWidth(120);
    m_textContentInput->setToolTip("Text box content.");
    textLayout->addWidget(m_textContentInput);

    QString comboStyle = "background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; border-radius: 3px; padding: 2px; font-size: 11px;";
    // The dropdown popup (QAbstractItemView) needs its own light-on-dark colours,
    // otherwise items render as dark text on the platform's default light/ dark view.
    QString comboViewStyle = " QAbstractItemView { background: #2d2d44; color: #e8e8ec; border: 1px solid #3d3d56; selection-background-color: #3d3d56; selection-color: #e8e8ec; }";
    m_fontCombo = new QFontComboBox;
    m_fontCombo->setStyleSheet("QFontComboBox { " + comboStyle + " }" + comboViewStyle);
    m_fontCombo->setMaximumWidth(150);
    m_fontCombo->setToolTip("Font family. Size derives from the box height.");
    textLayout->addWidget(m_fontCombo);

    m_weightCombo = new QComboBox;
    m_weightCombo->addItem("Light", 300);
    m_weightCombo->addItem("Normal", 400);
    m_weightCombo->addItem("Bold", 700);
    m_weightCombo->setCurrentIndex(1);
    m_weightCombo->setStyleSheet("QComboBox { " + comboStyle + " }" + comboViewStyle);
    m_weightCombo->setToolTip("Font weight.");
    textLayout->addWidget(m_weightCombo);

    m_textColorBtn = new QPushButton("Colour");
    m_textColorBtn->setStyleSheet(smallBtnStyle);
    m_textColorBtn->setToolTip("Text colour.");
    textLayout->addWidget(m_textColorBtn);

    m_textRow->hide();
    addRow->addWidget(m_textRow);

    connect(m_textContentInput, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (m_syncingText) return;
        int s = m_canvas->selectedItem();
        if (m_canvas->isTextItem(s)) m_canvas->setItemText(s, t);
    });
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        if (m_syncingText) return;
        int s = m_canvas->selectedItem();
        if (m_canvas->isTextItem(s)) m_canvas->setItemFont(s, f.family());
    });
    connect(m_weightCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_syncingText) return;
        int s = m_canvas->selectedItem();
        if (m_canvas->isTextItem(s)) m_canvas->setItemFontWeight(s, m_weightCombo->currentData().toInt());
    });
    connect(m_textColorBtn, &QPushButton::clicked, this, [this]() {
        int s = m_canvas->selectedItem();
        if (!m_canvas->isTextItem(s)) return;
        QString tc = m_canvas->itemExport(s).textColor;
        QColor cur = tc.isEmpty() ? QColor("#62A4C7") : QColor(tc);
        QColor c = QColorDialog::getColor(cur, this, "Text Colour");
        if (c.isValid()) m_canvas->setItemTextColor(s, c.name());
    });
    connect(m_canvas, &Canvas::selectionChanged, this, [this](int) { updateTextControls(); });
    connect(m_canvas, &Canvas::itemsChanged, this, [this]() { updateTextControls(); });

    addRow->addStretch();

    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("QLabel { color: #06969A; font-size: 13px; font-weight: bold; }");
    m_statusLabel->setWordWrap(true);
    addRow->addWidget(m_statusLabel);

    m_elapsedLabel = new QLabel("00:00:00");
    m_elapsedLabel->setStyleSheet("QLabel { color: #e8e8ec; font-size: 16px; font-weight: bold; font-family: monospace; }");
    addRow->addWidget(m_elapsedLabel);

    centerLayout->addLayout(addRow);

    // Buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    m_startBtn = new QPushButton("Start Recording");
    m_startBtn->setStyleSheet("QPushButton { background: #06969A; color: #ffffff; border: none; border-radius: 6px; padding: 10px 24px; font-size: 14px; font-weight: bold; } QPushButton:hover { background: #058084; } QPushButton:disabled { background: #3d3d56; color: #8A8B8B; }");
    connect(m_startBtn, &QPushButton::clicked, this, &RecordPage::onStartClicked);
    btnRow->addWidget(m_startBtn);

    m_pauseBtn = new QPushButton("Pause");
    m_pauseBtn->setStyleSheet("QPushButton { background: #DF9E2F; color: #ffffff; border: none; border-radius: 6px; padding: 10px 16px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #E8B84A; }");
    m_pauseBtn->hide();
    connect(m_pauseBtn, &QPushButton::clicked, this, &RecordPage::onPauseClicked);
    btnRow->addWidget(m_pauseBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setStyleSheet("QPushButton { background: #CC0403; color: #ffffff; border: none; border-radius: 6px; padding: 10px 16px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #E03030; }");
    m_stopBtn->hide();
    connect(m_stopBtn, &QPushButton::clicked, this, &RecordPage::onStopClicked);
    btnRow->addWidget(m_stopBtn);

    centerLayout->addLayout(btnRow);
    layout->addWidget(centerCol, 1);

    // === Asset gallery at bottom ===
    m_assetGallery = new AssetGallery(this);
    outerLayout->addWidget(m_assetGallery);
}

void RecordPage::addScreen(const MonitorInfo &mon) {
    m_canvas->setMonitor(mon);
}

void RecordPage::loadPreset(const QString &name) {
    auto &cfg = Config::instance();
    if (!cfg.presets.contains(name)) return;
    cfg.activePreset = name;
    cfg.canvasState = cfg.presets[name];
    cfg.save();
    m_restoring = true;
    applyState(cfg.canvasState);
    m_restoring = false;
    refreshPresetList();
    emit presetChanged();
}

void RecordPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refreshLayerList();
    m_canvas->update();
    if (m_assetGallery) m_assetGallery->refresh();
}

void RecordPage::suspendPreviews() {
    if (m_canvas) m_canvas->suspendPreviews();
}

void RecordPage::resumePreviews() {
    if (m_canvas && !m_isRecording) m_canvas->resumePreviews();
}

void RecordPage::onStartClicked() {
    if (m_isRecording || m_countdownTimer->isActive()) return;

    // Default title for systray-initiated recordings
    if (m_titleInput->text().trimmed().isEmpty()) {
        m_titleInput->setText("recording-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    }

    m_countdownVal = 5;
    m_statusLabel->setText("Starting in 5...");
    m_statusLabel->setStyleSheet("QLabel { color: #DF9E2F; font-size: 13px; font-weight: bold; }");
    m_startBtn->setEnabled(false);
    emit countdownStarted();
    emit countdownTick(5);
    m_countdownTimer->start(1000);
}

void RecordPage::onCountdownTick() {
    m_countdownVal--;
    if (m_countdownVal <= 0) {
        m_countdownTimer->stop();
        m_statusLabel->setText("Starting recorders...");

        // Build options from canvas state
        RecordingOptions opts;
        opts.monitor = m_canvas->selectedMonitor();
        opts.noScreen = opts.monitor.isEmpty();
        opts.noAudio = !m_audioCheck->isChecked();
        opts.webcamDevice = m_canvas->firstWebcamDevice();
        opts.noWebcam = opts.webcamDevice.isEmpty();
        opts.canvasMode = m_modeGroup->checkedId(); // 0=landscape, 1=vertical, 2=left split, 3=right split
        opts.title = m_titleInput->text();
        opts.description = m_descInput->toPlainText();
        opts.number = m_numberInput->text().toInt();
        opts.presenter = m_presenterInput->text();
        opts.titleColor = Config::instance().titleColor;

        // Map canvas items with their interactive placement
        auto items = m_canvas->exportItems();
        double cw = m_canvas->canvasWidth();
        double ch = m_canvas->canvasHeight();
        int screenSeen = 0;
        int webcamSeen = 0;
        for (const auto &e : items) {
            if (e.type == 0) { // screen layer
                if (screenSeen == 0) {
                    // Primary screen — the recorded background. Extract its crop.
                    if (e.h > 0 && e.w > 0) {
                        opts.screenCropTop = double(e.cropTop) / e.h;
                        opts.screenCropBottom = double(e.cropBottom) / e.h;
                        opts.screenCropLeft = double(e.cropLeft) / e.w;
                        opts.screenCropRight = double(e.cropRight) / e.w;
                    }
                } else if (!e.monitorName.isEmpty()) {
                    // Additional monitor — captured separately and composited as an
                    // inset at its canvas-relative placement.
                    RecordingOptions::ScreenOverlay so;
                    so.monitor = e.monitorName;
                    so.relX = (e.x - e.w/2.0) / cw;
                    so.relY = (e.y - e.h/2.0) / ch;
                    so.relW = e.w / cw;
                    so.relH = e.h / ch;
                    if (e.h > 0 && e.w > 0) {
                        so.cropTop = double(e.cropTop) / e.h;
                        so.cropBottom = double(e.cropBottom) / e.h;
                        so.cropLeft = double(e.cropLeft) / e.w;
                        so.cropRight = double(e.cropRight) / e.w;
                    }
                    opts.extraScreens.append(so);
                }
                screenSeen++;
            } else if (e.type == 2) { // logo
                RecordingOptions::LogoOpts lo;
                lo.path = e.filePath;
                lo.gifLoop = e.gifLoop;
                lo.gifLoopMax = e.gifLoopMax;
                // Store canvas-relative positions (0.0-1.0)
                lo.relX = (e.x - e.w/2.0) / cw;
                lo.relY = (e.y - e.h/2.0) / ch;
                lo.relW = e.w / cw;
                lo.relH = e.h / ch;
                opts.logos.append(lo);
            } else if (e.type == 1) { // webcam — capture placement, shape and crop
                if (webcamSeen == 0) {
                    // Primary webcam — the existing single-webcam path.
                    opts.webcamRelX = (e.x - e.w/2.0) / cw;
                    opts.webcamRelY = (e.y - e.h/2.0) / ch;
                    opts.webcamRelW = e.w / cw;
                    opts.webcamRelH = e.h / ch;
                    opts.webcamShape = e.shape; // 0=round, 1=square, 2=rect
                    if (e.w > 0 && e.h > 0) {
                        // Alt-crop on the canvas trims the recorded webcam region to
                        // the shown portion (fractions of the webcam frame).
                        opts.webcamCropTop = double(e.cropTop) / e.h;
                        opts.webcamCropBottom = double(e.cropBottom) / e.h;
                        opts.webcamCropLeft = double(e.cropLeft) / e.w;
                        opts.webcamCropRight = double(e.cropRight) / e.w;
                    }
                } else if (!e.device.isEmpty()) {
                    // Additional webcam — captured separately, composited as overlay.
                    RecordingOptions::WebcamOverlay wo;
                    wo.device = e.device; wo.shape = e.shape;
                    wo.relX = (e.x - e.w/2.0) / cw;
                    wo.relY = (e.y - e.h/2.0) / ch;
                    wo.relW = e.w / cw;
                    wo.relH = e.h / ch;
                    if (e.w > 0 && e.h > 0) {
                        wo.cropTop = double(e.cropTop) / e.h;
                        wo.cropBottom = double(e.cropBottom) / e.h;
                        wo.cropLeft = double(e.cropLeft) / e.w;
                        wo.cropRight = double(e.cropRight) / e.w;
                    }
                    opts.extraWebcams.append(wo);
                }
                webcamSeen++;
            } else if (e.type == 3) { // text box
                if (e.label.trimmed().isEmpty()) continue;
                RecordingOptions::TextBox tb;
                tb.text = e.label;
                tb.fontFamily = e.fontFamily.isEmpty() ? "Sans" : e.fontFamily;
                tb.fontWeight = e.fontWeight;
                tb.color = e.textColor.isEmpty() ? Config::instance().titleColor : e.textColor;
                tb.fontFile = resolveFontFile(tb.fontFamily, tb.fontWeight);
                tb.relX = (e.x - e.w/2.0) / cw;
                tb.relY = (e.y - e.h/2.0) / ch;
                tb.relW = e.w / cw;
                tb.relH = e.h / ch;
                opts.textBoxes.append(tb);
            } else if (e.type == 4) { // start sound
                opts.startSound = e.filePath;
            } else if (e.type == 5) { // end sound
                opts.endSound = e.filePath;
            }
        }

        // Save presenter for next session
        auto &cfg = Config::instance();
        cfg.defaultPresenter = opts.presenter;
        cfg.save();

        // Release webcam devices so the recorder can use them
        m_canvas->stopAllWebcamPreviews();

        m_recorder->start(opts);
    } else {
        m_statusLabel->setText(QString("Starting in %1...").arg(m_countdownVal));
        emit countdownTick(m_countdownVal);
    }
}

void RecordPage::onRecorderStarted() {
    // This slot is called via signal/slot — guaranteed on the main thread!
    m_isRecording = true;
    m_statusLabel->setText("Recording");
    m_statusLabel->setStyleSheet("QLabel { color: #CC0403; font-size: 13px; font-weight: bold; }");
    m_startBtn->hide();
    m_pauseBtn->setText("Pause");
    m_pauseBtn->show();
    m_stopBtn->show();
    m_elapsedTimer->start(200); // update more frequently for smooth display

    emit recordingStarted(); // MainWindow hides
}

void RecordPage::onStopClicked() {
    m_statusLabel->setText("Stopping...");
    m_stopBtn->setEnabled(false);
    m_pauseBtn->setEnabled(false);
    m_recorder->stop();
}

void RecordPage::onRecorderStopped() {
    // Signal/slot — main thread!
    m_isRecording = false;
    m_elapsedTimer->stop();
    m_elapsedLabel->setText("00:00:00");
    m_startBtn->show();
    m_startBtn->setEnabled(true);
    m_pauseBtn->hide();
    m_pauseBtn->setEnabled(true);
    m_stopBtn->hide();
    m_stopBtn->setEnabled(true);

    // Restart webcam previews now that recording is done
    m_canvas->startAllWebcamPreviews();

    // Increment number
    int num = m_numberInput->text().toInt() + 1;
    m_numberInput->setText(QString("%1").arg(num, 3, 10, QChar('0')));

    m_statusLabel->setText("Ready");
    m_statusLabel->setStyleSheet("QLabel { color: #06969A; font-size: 13px; font-weight: bold; }");

    emit recordingStopped(); // MainWindow shows + navigates to processing
}

void RecordPage::onRecorderError(const QString &error) {
    m_statusLabel->setText("Error: " + error);
    m_statusLabel->setStyleSheet("QLabel { color: #CC0403; font-size: 13px; font-weight: bold; }");
    m_startBtn->show();
    m_startBtn->setEnabled(true);
    m_pauseBtn->hide();
    m_stopBtn->hide();
}

void RecordPage::saveCanvasState() {
    // Self-guard against re-entrant saves while a restore/preset-load is applying
    // items (which emits itemsChanged). Defence-in-depth vs guarding at every call.
    if (m_restoring) return;
    auto &cfg = Config::instance();
    cfg.canvasState = captureCurrentState();
    // Auto-save to active preset
    if (!cfg.activePreset.isEmpty()) {
        cfg.presets[cfg.activePreset] = cfg.canvasState;
    }
    cfg.save();
}

void RecordPage::restoreCanvasState() {
    auto &cfg = Config::instance();
    if (cfg.canvasState.items.isEmpty()) return;

    // Startup restore shares the exact crop/position/sound-aware path used by
    // preset load, so a reopened session restores identically to loading a preset.
    m_restoring = true;
    buildCanvasFromState(cfg.canvasState, /*clearFirst=*/false);
    m_restoring = false;
}

void RecordPage::refreshPresetList() {
    auto &cfg = Config::instance();
    m_restoring = true;
    m_presetList->blockSignals(true);
    m_presetList->clear();
    int activeRow = -1;
    int row = 0;
    for (const auto &name : cfg.presets.keys()) {
        bool isActive = (name == cfg.activePreset);
        QString display = isActive ? ("* " + name) : name;
        m_presetList->addItem(display);
        if (isActive) activeRow = row;
        row++;
    }
    if (activeRow >= 0) m_presetList->setCurrentRow(activeRow);
    m_presetList->blockSignals(false);
    m_restoring = false;
}

CanvasState RecordPage::captureCurrentState() {
    CanvasState state;
    state.mode = m_canvas->modeString();
    state.audioEnabled = m_audioCheck->isChecked();
    state.presenter = m_presenterInput->text();
    state.titleColor = Config::instance().titleColor;

    for (const auto &e : m_canvas->exportItems()) {
        CanvasItemState s;
        switch (e.type) {
        case 0: s.type = "screen"; break;
        case 1: s.type = "webcam"; break;
        case 2: s.type = "logo"; break;
        case 3: s.type = "title"; break;
        case 4: s.type = "start_sound"; break;
        case 5: s.type = "end_sound"; break;
        default: s.type = "unknown"; break;
        }
        s.label = e.label;
        // Store as relative fractions of the output frame (not the widget)
        QRect fr = m_canvas->frameRect();
        double fw = fr.width() > 0 ? fr.width() : 1;
        double fh = fr.height() > 0 ? fr.height() : 1;
        s.rx = (e.x - fr.x()) / fw;
        s.ry = (e.y - fr.y()) / fh;
        s.rw = e.w / fw;
        s.rh = e.h / fh;
        s.device = e.device; s.filePath = e.filePath;
        s.shape = e.shape; s.gifLoop = e.gifLoop; s.gifLoopMax = e.gifLoopMax;
        // Store crop as fraction of item dimensions
        s.cropTop = e.h > 0 ? double(e.cropTop) / e.h : 0;
        s.cropBottom = e.h > 0 ? double(e.cropBottom) / e.h : 0;
        s.cropLeft = e.w > 0 ? double(e.cropLeft) / e.w : 0;
        s.cropRight = e.w > 0 ? double(e.cropRight) / e.w : 0;
        s.fontFamily = e.fontFamily; s.fontWeight = e.fontWeight; s.textColor = e.textColor;
        state.items.append(s);
    }
    return state;
}

void RecordPage::applyState(const CanvasState &state) {
    buildCanvasFromState(state, /*clearFirst=*/true);
}

void RecordPage::buildCanvasFromState(const CanvasState &state, bool clearFirst) {
    if (clearFirst) m_canvas->clearAll();

    // Restore mode
    int modeId = 0;
    if (state.mode == "vertical") modeId = 1;
    else if (state.mode == "left_split") modeId = 2;
    else if (state.mode == "right_split") modeId = 3;
    m_canvas->setMode(modeId);
    if (m_modeGroup->button(modeId))
        m_modeGroup->button(modeId)->setChecked(true);

    QSet<QString> availableWebcams;
    for (const auto &dev : m_webcams) availableWebcams.insert(dev.device);

    QRect fr = m_canvas->frameRect();

    for (const auto &s : state.items) {
        // Convert frame-relative fractions back to canvas pixels
        int px = fr.x() + static_cast<int>(s.rx * fr.width());
        int py = fr.y() + static_cast<int>(s.ry * fr.height());
        int pw = static_cast<int>(s.rw * fr.width());
        int ph = static_cast<int>(s.rh * fr.height());

        if (s.type == "screen") {
            bool matched = false;
            for (const auto &mon : m_monitors) {
                QString desc = mon.description.isEmpty() ? mon.name : mon.description;
                if (s.label == "Screen: " + desc) { addScreen(mon); matched = true; break; }
            }
            if (!matched && !m_monitors.isEmpty()) addScreen(m_monitors.first());
            // Apply saved position and crop to the screen item
            for (int i = 0; i < m_canvas->itemCount(); i++) {
                if (m_canvas->isScreenItem(i)) {
                    Canvas::ItemExport e;
                    e.type = 0; e.label = s.label;
                    e.x = px; e.y = py; e.w = pw; e.h = ph;
                    e.cropTop = static_cast<int>(s.cropTop * ph);
                    e.cropBottom = static_cast<int>(s.cropBottom * ph);
                    e.cropLeft = static_cast<int>(s.cropLeft * pw);
                    e.cropRight = static_cast<int>(s.cropRight * pw);
                    m_canvas->updateScreenItem(e);
                    break;
                }
            }
        } else if (s.type == "webcam") {
            if (!availableWebcams.contains(s.device)) continue;
            Canvas::ItemExport e;
            e.type = 1; e.label = s.label;
            e.x = px; e.y = py; e.w = pw; e.h = ph;
            e.device = s.device; e.shape = s.shape;
            e.cropTop = static_cast<int>(s.cropTop * ph);
            e.cropBottom = static_cast<int>(s.cropBottom * ph);
            e.cropLeft = static_cast<int>(s.cropLeft * pw);
            e.cropRight = static_cast<int>(s.cropRight * pw);
            m_canvas->importItem(e);
        } else if (s.type == "logo") {
            if (s.filePath.isEmpty() || !QFile::exists(s.filePath)) continue;
            Canvas::ItemExport e;
            e.type = 2; e.label = s.label;
            e.x = px; e.y = py; e.w = pw; e.h = ph;
            e.filePath = s.filePath;
            e.gifLoop = s.gifLoop; e.gifLoopMax = s.gifLoopMax;
            e.cropTop = static_cast<int>(s.cropTop * ph);
            e.cropBottom = static_cast<int>(s.cropBottom * ph);
            e.cropLeft = static_cast<int>(s.cropLeft * pw);
            e.cropRight = static_cast<int>(s.cropRight * pw);
            m_canvas->importItem(e);
        } else if (s.type == "title") {
            Canvas::ItemExport e;
            e.type = 3; e.label = s.label;
            e.x = px; e.y = py; e.w = pw; e.h = ph;
            e.cropTop = static_cast<int>(s.cropTop * ph);
            e.cropBottom = static_cast<int>(s.cropBottom * ph);
            e.cropLeft = static_cast<int>(s.cropLeft * pw);
            e.cropRight = static_cast<int>(s.cropRight * pw);
            e.fontFamily = s.fontFamily; e.fontWeight = s.fontWeight; e.textColor = s.textColor;
            m_canvas->importItem(e);
        } else if (s.type == "start_sound" || s.type == "end_sound") {
            Canvas::ItemExport e;
            e.type = (s.type == "end_sound") ? 5 : 4;
            e.label = s.label;
            e.filePath = s.filePath;
            m_canvas->importItem(e);
        }
    }

    m_audioCheck->setChecked(state.audioEnabled);
    if (!state.presenter.isEmpty()) m_presenterInput->setText(state.presenter);
    if (!state.titleColor.isEmpty()) {
        Config::instance().titleColor = state.titleColor;
        m_canvas->setTitleColor(state.titleColor);
    }
    for (const auto &s : state.items) {
        if (s.type == "title" && !s.label.isEmpty()) {
            m_titleInput->setText(s.label);
            break;
        }
    }
    refreshLayerList();
}

void RecordPage::refreshLayerList() {
    m_layerList->clear();
    for (int i = 0; i < m_canvas->itemCount(); i++) {
        m_layerList->addItem(m_canvas->itemLabel(i));
    }
}

void RecordPage::updateTextControls() {
    if (!m_textRow) return;
    int sel = m_canvas->selectedItem();
    if (!m_canvas->isTextItem(sel)) { m_textRow->hide(); return; }
    auto e = m_canvas->itemExport(sel);

    m_syncingText = true;
    // Don't clobber the caret while the user is typing in the field.
    if (!m_textContentInput->hasFocus()) m_textContentInput->setText(e.label);
    if (!e.fontFamily.isEmpty()) m_fontCombo->setCurrentFont(QFont(e.fontFamily));
    int wi = m_weightCombo->findData(e.fontWeight);
    m_weightCombo->setCurrentIndex(wi >= 0 ? wi : 1);
    m_syncingText = false;
    m_textRow->show();
}

void RecordPage::onPauseClicked() {
    if (m_recorder->isPaused()) {
        m_recorder->resume();
        m_pauseBtn->setText("Pause");
        m_statusLabel->setText("Recording");
        m_statusLabel->setStyleSheet("QLabel { color: #CC0403; font-size: 13px; font-weight: bold; }");
    } else {
        m_recorder->pause();
        m_pauseBtn->setText("Resume");
        m_statusLabel->setText("Paused");
        m_statusLabel->setStyleSheet("QLabel { color: #DF9E2F; font-size: 13px; font-weight: bold; }");
    }
}
