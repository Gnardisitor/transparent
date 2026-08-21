#include "SpinnerWidget.h"

#include <QPainter>
#include <QTimer>

SpinnerWidget::SpinnerWidget(QWidget* parent) : QWidget(parent) {
    setFixedSize(40, 40);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this]() {
        angle_ = (angle_ + 30) % 360;
        update();
    });
}

void SpinnerWidget::start() {
    angle_ = 0;
    setVisible(true);
    timer_->start(80);
}

void SpinnerWidget::stop() {
    timer_->stop();
    setVisible(false);
}

void SpinnerWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF area = rect().adjusted(4, 4, -4, -4);
    QPen pen(QColor(255, 255, 255, 60));
    pen.setWidth(4);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawEllipse(area);

    pen.setColor(QColor(66, 153, 225));
    painter.setPen(pen);
    painter.drawArc(area, -angle_ * 16, 100 * 16);
}
