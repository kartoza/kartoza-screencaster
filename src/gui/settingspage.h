/**
 * @file settingspage.h
 * @brief Application settings page for Kartoza Screencaster.
 */

#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

/**
 * @class SettingsPage
 * @brief Page widget for viewing and editing application-wide settings.
 *
 * Provides controls for output directory, default presenter, audio
 * normalisation, title/background colours, logo directory, topic presets,
 * and YouTube integration status.
 */
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Construct the settings page.
     * @param parent Optional parent widget.
     */
    explicit SettingsPage(QWidget *parent = nullptr);

private:
    /** @brief Build the complete UI layout. */
    void setupUI();
    /** @brief Load current values from the persistent config file. */
    void loadFromConfig();
    /** @brief Save current values to the persistent config file. */
    void saveToConfig();
    /**
     * @brief Open a colour picker dialog and update the swatch and hex input.
     * @param title Dialog window title.
     * @param swatch Button used as a colour swatch preview.
     * @param hexInput Line edit showing the hex colour value.
     * @param colorVar Reference to the backing colour variable to update.
     */
    void openColorDialog(const QString &title, QPushButton *swatch, QLineEdit *hexInput, QString &colorVar);

    /** @brief Input for the recording output directory path. */
    QLineEdit *m_outputDirInput;
    /** @brief Input for the default presenter name. */
    QLineEdit *m_presenterInput;
    /** @brief Checkbox to enable/disable audio normalisation. */
    QCheckBox *m_normalizeCheck;
    /** @brief Colour swatch button for the title colour. */
    QPushButton *m_titleColorSwatch;
    /** @brief Hex colour input for the title colour. */
    QLineEdit *m_titleColorHex;
    /** @brief Colour swatch button for the background colour. */
    QPushButton *m_bgColorSwatch;
    /** @brief Hex colour input for the background colour. */
    QLineEdit *m_bgColorHex;
    /** @brief Input for the logo image directory path. */
    QLineEdit *m_logoDirInput;
    /** @brief List widget showing saved topic presets. */
    QListWidget *m_topicsList;
    /** @brief Input for adding a new topic preset. */
    QLineEdit *m_topicInput;
    /** @brief Label showing YouTube API connection status. */
    QLabel *m_ytStatusLabel;

    /** @brief Current title colour value. */
    QString m_titleColor;
    /** @brief Current background colour value. */
    QString m_bgColor;
};
