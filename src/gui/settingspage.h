#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

private:
    void setupUI();
    void loadFromConfig();
    void saveToConfig();
    void openColorDialog(const QString &title, QPushButton *swatch, QLineEdit *hexInput, QString &colorVar);

    QLineEdit *m_outputDirInput;
    QLineEdit *m_presenterInput;
    QCheckBox *m_normalizeCheck;
    QPushButton *m_titleColorSwatch;
    QLineEdit *m_titleColorHex;
    QPushButton *m_bgColorSwatch;
    QLineEdit *m_bgColorHex;
    QLineEdit *m_logoDirInput;
    QListWidget *m_topicsList;
    QLineEdit *m_topicInput;
    QLabel *m_ytStatusLabel;

    QString m_titleColor;
    QString m_bgColor;
};
