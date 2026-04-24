#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QStatusBar>
#include "gui/recordpage.h"
#include "gui/historypage.h"
#include "gui/settingspage.h"
#include "gui/processingpage.h"
#include "gui/tray.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &version, QWidget *parent = nullptr);

    enum Page { PageRecord = 0, PageHistory, PageSettings, PageProcessing };

public slots:
    void navigateTo(Page page);

private:
    void setupUI();
    QWidget *createSidebar();
    QWidget *createFooter();

    QString m_version;

    QStackedWidget *m_content;
    QLabel *m_helpLabel;

    QPushButton *m_btnRecord;
    QPushButton *m_btnHistory;
    QPushButton *m_btnSettings;

    RecordPage *m_recordPage;
    HistoryPage *m_historyPage;
    SettingsPage *m_settingsPage;
    ProcessingPage *m_processingPage;

    Tray *m_tray;

    QTimer *m_helpPoller;
};
