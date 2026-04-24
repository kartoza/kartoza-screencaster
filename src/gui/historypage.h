#pragma once
#include <QWidget>
class HistoryPage : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPage(QWidget *parent = nullptr);
    void refresh();
};
