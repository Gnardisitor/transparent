#pragma once

#include <QWidget>

class QTimer;

// Minimal indeterminate spinner drawn with QPainter, no image assets.
class SpinnerWidget : public QWidget {
    Q_OBJECT

public:
    explicit SpinnerWidget(QWidget* parent = nullptr);

    void start();
    void stop();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QTimer* timer_;
    int angle_ = 0;
};
