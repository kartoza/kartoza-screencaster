#include "gui/settingspage.h"
#include <QVBoxLayout>
#include <QLabel>
SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel("Settings — TODO: port from Go");
    label->setStyleSheet("QLabel { color: #cdd6f4; font-size: 16px; }");
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
}
