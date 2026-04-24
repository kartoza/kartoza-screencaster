#include "gui/historypage.h"
#include <QVBoxLayout>
#include <QLabel>
HistoryPage::HistoryPage(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel("History — TODO: port from Go");
    label->setStyleSheet("QLabel { color: #cdd6f4; font-size: 16px; }");
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
}
void HistoryPage::refresh() {}
