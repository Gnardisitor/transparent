#pragma once

#include <QFrame>

// The image preview canvas: checkerboard backdrop, zoom/pan, and the
// before/after wipe. Replaces the old QLabel-with-QPixmap preview so the
// result can be inspected at pixel level and compared against the source.
//
// View model: `zoom_` is screen pixels per image pixel (1.0 = 100%) and
// `pan_` is the image coordinate (in after-image pixels) at the center of
// the viewport. Fit mode keeps the image shrunk to fit and centered; any
// zoom interaction switches to Free mode. Both images are drawn into the
// same display rect, so the wipe stays aligned even when an upscale step
// makes the result larger than the source.
class PreviewCanvas : public QFrame {
    Q_OBJECT

public:
    // 1 image pixel = 16 screen pixels; past visual resolution on any
    // display, and it bounds repaint cost.
    static constexpr double kMaxZoom = 16.0;

    explicit PreviewCanvas(QWidget* parent = nullptr);

    // `before` is the original image, `after` the pipeline result. A null
    // after renders the before on both wipe halves (still processing).
    void setBeforeImage(const QImage& image);
    void setAfterImage(const QImage& image);
    void clearImages();

    // Compare is a still-image feature: animations set this to false, which
    // makes isCompareAvailable() false and renders the wipe inert.
    void setCompareAllowed(bool allowed);
    bool isCompareAllowed() const { return compareAllowed_; }
    // Both images present (and compare allowed). A mismatched pixel size is
    // fine — both draw into the same display rect.
    bool isCompareAvailable() const;

    void setWipeEnabled(bool enabled);
    bool isWipeEnabled() const { return wipeEnabled_; }
    // Divider as a fraction of the image display rect's width, 0..1.
    void setDividerPosition(double fraction);
    double dividerPosition() const { return dividerFraction_; }

    // View state.
    void resetView(); // fit, divider back to 50%
    void zoomToFit();
    void zoomTo100();
    void zoomIn();  // one step, centered
    void zoomOut(); // one step, centered
    // Cursor-anchored zoom: the image coordinate under `pos` stays there.
    void zoomAt(QPointF pos, double factor);
    // Screen pixels per image pixel; Fit mode reports the fit zoom.
    double zoom() const;
    // Zoom that fits the after-image into the canvas, never enlarging.
    double fitZoom() const;
    // Display rect of the after-image in widget coordinates.
    QRectF imageRect() const;
    // Image coordinate (after-image pixels) under a widget position.
    QPointF imageCoordAt(QPointF pos) const;
    // Nearest-neighbor once magnified >= 200% (blur hides the differences
    // zoom exists to show), smooth otherwise.
    Qt::TransformationMode transformationMode() const;

    // Divider interaction. `dividerViewX()` is the divider's x in widget
    // coordinates; `isNearDivider` is the wide grab-area test; dragging
    // calls `setDividerFromViewX` (clamped to the image area).
    double dividerViewX() const;
    bool isNearDivider(QPointF pos) const;
    void setDividerFromViewX(double x);

signals:
    void zoomChanged(double zoom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    enum class ViewMode { Fit, Free };

    // The after image drives the display geometry; before it exists (still
    // processing), the before image does.
    const QImage& primaryImage() const {
        return afterImage_.isNull() ? beforeImage_ : afterImage_;
    }

    void updateCursor(QPointF pos);
    void emitZoomChanged();
    // True when the wipe should render (compare available + enabled).
    bool wipeActive() const;

    QImage beforeImage_;
    QImage afterImage_;
    bool compareAllowed_ = true;
    bool wipeEnabled_ = true;
    double dividerFraction_ = 0.5;

    ViewMode viewMode_ = ViewMode::Fit;
    double freeZoom_ = 1.0;                // screen px per image px
    QPointF pan_ = QPointF(0, 0);          // image coord at the viewport center

    enum class DragMode { None, Pan, Divider };
    DragMode dragMode_ = DragMode::None;
    QPointF panGrabStartImageCoord_;       // image coord under cursor at press
};
