#include "PreviewCanvas.h"

#include <QPainter>
#include <QWheelEvent>

#include <algorithm>

namespace {

// Checkerboard behind the preview so removed background is visible. Drawn
// at physical-pixel resolution with the DPR set so it stays crisp on HiDPI.
QPixmap checkerboardPattern(qreal devicePixelRatio, int cell = 12) {
    QPixmap pattern(cell * 2 * devicePixelRatio, cell * 2 * devicePixelRatio);
    pattern.setDevicePixelRatio(devicePixelRatio);
    QPainter painter(&pattern);
    painter.fillRect(0, 0, cell * 2, cell * 2, QColor(210, 210, 210));
    painter.fillRect(0, 0, cell, cell, QColor(160, 160, 160));
    painter.fillRect(cell, cell, cell, cell, QColor(160, 160, 160));
    return pattern;
}

// Distance in widget pixels within which the divider can be grabbed.
constexpr qreal kDividerGrabDistance = 6.0;

} // namespace

PreviewCanvas::PreviewCanvas(QWidget* parent) : QFrame(parent) {
    // No minimum size here: tests need small canvases. The app layout sets
    // its own minimum on the preview area.
    setMouseTracking(true); // divider hover cursor without buttons held
}

void PreviewCanvas::setBeforeImage(const QImage& image) {
    beforeImage_ = image;
    emitZoomChanged(); // fit zoom may have changed with the new image
    update();
}

void PreviewCanvas::setAfterImage(const QImage& image) {
    afterImage_ = image;
    emitZoomChanged();
    update();
}

void PreviewCanvas::clearImages() {
    beforeImage_ = QImage();
    afterImage_ = QImage();
    resetView();
    update();
}

void PreviewCanvas::setCompareAllowed(bool allowed) {
    compareAllowed_ = allowed;
    update();
}

bool PreviewCanvas::isCompareAvailable() const {
    return compareAllowed_ && !beforeImage_.isNull() && !afterImage_.isNull();
}

void PreviewCanvas::setWipeEnabled(bool enabled) {
    wipeEnabled_ = enabled;
    update();
}

void PreviewCanvas::setDividerPosition(double fraction) {
    dividerFraction_ = std::clamp(fraction, 0.0, 1.0);
    update();
}

void PreviewCanvas::resetView() {
    viewMode_ = ViewMode::Fit;
    dividerFraction_ = 0.5;
    emitZoomChanged();
    update();
}

double PreviewCanvas::fitZoom() const {
    const QImage& primary = primaryImage();
    if (primary.isNull() || width() < 1 || height() < 1) {
        return 1.0;
    }
    const qreal fitWidth = qreal(width()) / primary.width();
    const qreal fitHeight = qreal(height()) / primary.height();
    // Shrink-to-fit only; smaller images render at 1:1.
    return std::clamp(std::min(fitWidth, fitHeight), 0.0, 1.0);
}

double PreviewCanvas::zoom() const {
    return viewMode_ == ViewMode::Fit ? fitZoom() : freeZoom_;
}

QRectF PreviewCanvas::imageRect() const {
    const QImage& primary = primaryImage();
    const double z = zoom();
    const QSizeF scaled(primary.width() * z, primary.height() * z);
    // pan_ is the image coordinate at the viewport center, so image coord 0
    // lands at center - pan*z. Fit mode always centers the image regardless
    // of a stale pan from a previous Free mode.
    const QPointF pan = viewMode_ == ViewMode::Fit
        ? QPointF(primary.width() / 2.0, primary.height() / 2.0)
        : pan_;
    const QPointF topLeft(width() / 2.0 - pan.x() * z, height() / 2.0 - pan.y() * z);
    return QRectF(topLeft, scaled);
}

QPointF PreviewCanvas::imageCoordAt(QPointF pos) const {
    const double z = zoom();
    const QRectF rect = imageRect();
    if (z <= 0 || rect.isEmpty()) {
        return QPointF(0, 0);
    }
    return QPointF((pos.x() - rect.left()) / z, (pos.y() - rect.top()) / z);
}

void PreviewCanvas::zoomAt(QPointF pos, double factor) {
    if (primaryImage().isNull()) {
        return;
    }
    const QPointF imageCoord = imageCoordAt(pos);
    const double oldZoom = zoom();
    const double newZoom =
        std::clamp(oldZoom * factor, std::max(fitZoom(), 0.001), kMaxZoom);
    if (qFuzzyCompare(newZoom, oldZoom)) {
        return;
    }

    viewMode_ = ViewMode::Free;
    freeZoom_ = newZoom;
    // Keep the image coordinate under `pos` fixed: pos = center + (u - pan)*z
    // => pan_new = imageCoord + (center - pos) / z_new.
    pan_ = imageCoord + (QPointF(width() / 2.0, height() / 2.0) - pos) / newZoom;
    emitZoomChanged();
    update();
}

void PreviewCanvas::zoomToFit() {
    viewMode_ = ViewMode::Fit;
    emitZoomChanged();
    update();
}

void PreviewCanvas::zoomTo100() {
    if (primaryImage().isNull()) {
        return;
    }
    viewMode_ = ViewMode::Free;
    freeZoom_ = 1.0;
    emitZoomChanged();
    update();
}

void PreviewCanvas::zoomIn() {
    zoomAt(QPointF(width() / 2.0, height() / 2.0), 1.25);
}

void PreviewCanvas::zoomOut() {
    zoomAt(QPointF(width() / 2.0, height() / 2.0), 0.8);
}

Qt::TransformationMode PreviewCanvas::transformationMode() const {
    return zoom() >= 2.0 ? Qt::FastTransformation : Qt::SmoothTransformation;
}

double PreviewCanvas::dividerViewX() const {
    const QRectF rect = imageRect();
    return rect.left() + dividerFraction_ * rect.width();
}

bool PreviewCanvas::isNearDivider(QPointF pos) const {
    if (!wipeActive()) {
        return false;
    }
    const QRectF rect = imageRect();
    if (pos.y() < rect.top() || pos.y() > rect.bottom()) {
        return false;
    }
    return std::abs(pos.x() - dividerViewX()) <= kDividerGrabDistance;
}

void PreviewCanvas::setDividerFromViewX(double x) {
    const QRectF rect = imageRect();
    if (rect.width() < 1) {
        return;
    }
    setDividerPosition((x - rect.left()) / rect.width());
}

bool PreviewCanvas::wipeActive() const {
    return isCompareAvailable() && wipeEnabled_;
}

void PreviewCanvas::emitZoomChanged() {
    emit zoomChanged(zoom());
}

void PreviewCanvas::paintEvent(QPaintEvent* event) {
    QFrame::paintEvent(event); // draw the frame behind the image
    QPainter painter(this);
    painter.drawTiledPixmap(rect(), checkerboardPattern(devicePixelRatioF()));

    if (afterImage_.isNull() && beforeImage_.isNull()) {
        return;
    }

    // The after image (falling back to before while it is missing) defines
    // the display rect; the before image is drawn into the same rect so the
    // wipe halves stay aligned even at different pixel sizes.
    const QImage& primary = !afterImage_.isNull() ? afterImage_ : beforeImage_;
    const QRectF rect = imageRect();
    const Qt::TransformationMode mode = transformationMode();

    const bool wipe = wipeActive();
    const qreal dividerX = dividerViewX();

    auto drawImage = [&](const QImage& image) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform,
                              mode == Qt::SmoothTransformation);
        painter.drawImage(rect, image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    };

    if (wipe && !beforeImage_.isNull()) {
        // Left of the divider: before. Right: after.
        painter.save();
        painter.setClipRect(QRectF(rect.left(), rect.top(), dividerX - rect.left(), rect.height()));
        drawImage(beforeImage_);
        painter.restore();

        painter.save();
        painter.setClipRect(QRectF(dividerX, rect.top(), rect.right() - dividerX + 1, rect.height()));
        drawImage(afterImage_);
        painter.restore();

        // Clean vertical line with a small grip at the top.
        painter.setRenderHint(QPainter::Antialiasing, true);
        QLineF line(dividerX, rect.top(), dividerX, rect.bottom());
        painter.setPen(QPen(QColor(0, 0, 0, 160), 3));
        painter.drawLine(line);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawLine(line);
        QRectF grip(dividerX - 5, rect.top() + 6, 10, 18);
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1));
        painter.setBrush(QColor(255, 255, 255, 220));
        painter.drawRoundedRect(grip, 3, 3);
        painter.setRenderHint(QPainter::Antialiasing, false);
    } else {
        // No compare, or the result isn't ready yet (null after renders the
        // source on both halves while processing).
        drawImage(!afterImage_.isNull() ? afterImage_ : beforeImage_);
    }
}

void PreviewCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Fit mode re-fits automatically (zoom() derives from the widget size);
    // Free mode keeps pan_, i.e. the image stays anchored to its content.
    emitZoomChanged();
    update();
}

void PreviewCanvas::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    zoomAt(event->position(), std::pow(1.25, steps));
    event->accept();
}

void PreviewCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (isNearDivider(event->position())) {
        dragMode_ = DragMode::Divider;
        setDividerFromViewX(event->position().x());
    } else {
        dragMode_ = DragMode::Pan;
        panGrabStartImageCoord_ = imageCoordAt(event->position());
    }
    event->accept();
}

void PreviewCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (dragMode_ == DragMode::Divider) {
        setDividerFromViewX(event->position().x());
    } else if (dragMode_ == DragMode::Pan) {
        const double z = zoom();
        // Keep the image coordinate grabbed at press under the cursor:
        pan_ = panGrabStartImageCoord_ +
               (QPointF(width() / 2.0, height() / 2.0) - event->position()) / z;
        update();
    } else {
        updateCursor(event->position());
    }
    event->accept();
}

void PreviewCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragMode_ != DragMode::None) {
        dragMode_ = DragMode::None;
        updateCursor(event->position());
    }
    event->accept();
}

void PreviewCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    // Toggle fit <-> 100%.
    if (viewMode_ == ViewMode::Fit) {
        zoomTo100();
    } else if (qFuzzyCompare(freeZoom_, 1.0)) {
        zoomToFit();
    } else {
        zoomTo100();
    }
    event->accept();
}

void PreviewCanvas::updateCursor(QPointF pos) {
    setCursor(isNearDivider(pos) ? Qt::SplitHCursor : Qt::ArrowCursor);
}
