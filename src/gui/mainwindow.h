/**
 * @file mainwindow.h
 * @brief Main application window for Kartoza Screencaster.
 */

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

/**
 * @class MainWindow
 * @brief Primary window that hosts the sidebar navigation and stacked content pages.
 *
 * Manages page navigation between Record, History, Settings, and Processing views.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Construct the main window.
     * @param version Application version string displayed in the UI.
     * @param parent Optional parent widget.
     */
    explicit MainWindow(const QString &version, QWidget *parent = nullptr);

    /** @brief Identifiers for the navigable content pages. */
    enum Page { PageRecord = 0, PageHistory, PageSettings, PageProcessing };

public slots:
    /**
     * @brief Switch the visible content page.
     * @param page The page to display.
     */
    void navigateTo(Page page);
    /** @brief Show the window from systray (works on Wayland). */
    void showFromTray();
    /** @brief Hide the window to systray (works on Wayland). */
    void hideToTray();

protected:
    /** @brief Hide to systray instead of quitting. */
    void closeEvent(QCloseEvent *event) override;
    /** @brief Suspend live previews when window is hidden. */
    void hideEvent(QHideEvent *event) override;
    /** @brief Resume live previews when window is shown. */
    void showEvent(QShowEvent *event) override;
    /** @brief Whether the window is conceptually hidden to systray. */
    bool isHiddenToTray() const { return m_hiddenToTray; }

private:
    /** @brief Build the complete UI layout. */
    void setupUI();
    /** @brief Create the left-hand navigation sidebar. */
    QWidget *createSidebar();
    /** @brief Create the bottom footer bar with branding and help text. */
    QWidget *createFooter();

    /** @brief Application version string. */
    QString m_version;

    /** @brief Stacked widget holding all content pages. */
    QStackedWidget *m_content;
    /** @brief Label in the footer for contextual help text. */
    QLabel *m_helpLabel;

    /** @brief Sidebar button to navigate to the Record page. */
    QPushButton *m_btnRecord;
    /** @brief Sidebar button to navigate to the History page. */
    QPushButton *m_btnHistory;
    /** @brief Sidebar button to navigate to the Settings page. */
    QPushButton *m_btnSettings;

    /** @brief The recording configuration and control page. */
    RecordPage *m_recordPage;
    /** @brief The recording history and playback page. */
    HistoryPage *m_historyPage;
    /** @brief The application settings page. */
    SettingsPage *m_settingsPage;
    /** @brief The post-recording processing progress page. */
    ProcessingPage *m_processingPage;

    /** @brief System tray icon and menu controller. */
    Tray *m_tray;

    /** @brief Timer that polls for help-text updates. */
    QTimer *m_helpPoller;

    /** @brief True when window is conceptually hidden to systray. */
    bool m_hiddenToTray = false;
    /** @brief Saved geometry before hiding to tray. */
    QRect m_savedGeometry;
};
