#pragma once

#include <QWidget>

class QTimer;

// Minimal indeterminate "busy" spinner: a rotating partial ring driven by an
// internal QTimer. No image assets — just QPainter — so it can be dropped
// anywhere (e.g. floated over the preview image) without extra resources.
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
