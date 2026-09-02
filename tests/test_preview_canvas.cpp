// PreviewCanvas tests: geometry math and wipe rendering, via QTest with the
// offscreen platform. Rendering checks use QWidget::grab() and sample pixel
// colors, so solid-color before/after images make the assertions trivial.

#include <QtTest>

#include "ui/PreviewCanvas.h"

#include <QRadioButton>

namespace {

QImage solidImage(int w, int h, QRgb color) {
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

constexpr QRgb kRed = qRgb(255, 0, 0);
constexpr QRgb kBlue = qRgb(0, 0, 255);

} // namespace

class TestPreviewCanvas : public QObject {
    Q_OBJECT

private slots:
    void fitModeShrinksLargeImagesAndCenters();
    void fitModeKeepsSmallImagesAtOneToOneAndCenters();
    void wheelZoomIsCursorAnchored();
    void zoomIsClampedTo16xAndFitScale();
    void transformationModeIsNearestAtHighZoom();
    void wipeDividerHitTestAndDrag();
    void wipeRenderSplitsBeforeAndAfter();
    void wipeDisabledRendersAfterOnly();
    void missingAfterShowsBeforeOnBothSides();
    void compareAvailabilityRules();
    void compareWorksWithMismatchedImageSizes();
    void resetViewRestoresFitAndCenteredDivider();
};

void TestPreviewCanvas::fitModeShrinksLargeImagesAndCenters() {
    PreviewCanvas canvas;
    canvas.resize(400, 300);
    canvas.setBeforeImage(solidImage(800, 600, kRed));
    canvas.setAfterImage(solidImage(800, 600, kBlue));

    // 800x600 into 400x300: exact 0.5 fit, no letterbox.
    QCOMPARE(canvas.fitZoom(), 0.5);
    QCOMPARE(canvas.zoom(), 0.5);
    QCOMPARE(canvas.imageRect(), QRectF(0, 0, 400, 300));
}

void TestPreviewCanvas::fitModeKeepsSmallImagesAtOneToOneAndCenters() {
    PreviewCanvas canvas;
    canvas.resize(400, 300);
    canvas.setBeforeImage(solidImage(100, 80, kRed));
    canvas.setAfterImage(solidImage(100, 80, kBlue));

    // Shrink-to-fit never enlarges: small images render at 1:1, centered.
    QCOMPARE(canvas.fitZoom(), 1.0);
    QCOMPARE(canvas.zoom(), 1.0);
    QCOMPARE(canvas.imageRect(), QRectF(150, 110, 100, 80));
}

void TestPreviewCanvas::wheelZoomIsCursorAnchored() {
    PreviewCanvas canvas;
    canvas.resize(400, 300);
    canvas.setBeforeImage(solidImage(800, 600, kRed));
    canvas.setAfterImage(solidImage(800, 600, kBlue));

    // Image coordinate under the cursor must not move when zooming at it.
    const QPointF cursor(100, 75);
    const QPointF imageCoordBefore = canvas.imageCoordAt(cursor);

    canvas.zoomAt(cursor, 2.0);

    QCOMPARE(canvas.imageCoordAt(cursor), imageCoordBefore);
    QCOMPARE(canvas.zoom(), 1.0); // 0.5 * 2.0
}

void TestPreviewCanvas::zoomIsClampedTo16xAndFitScale() {
    PreviewCanvas canvas;
    canvas.resize(400, 300);
    canvas.setBeforeImage(solidImage(800, 600, kRed));
    canvas.setAfterImage(solidImage(800, 600, kBlue));

    canvas.zoomAt(canvas.rect().center(), 1000.0);
    QCOMPARE(canvas.zoom(), PreviewCanvas::kMaxZoom);

    canvas.zoomAt(canvas.rect().center(), 1.0 / 1000.0);
    // Cannot zoom out past fit.
    QCOMPARE(canvas.zoom(), canvas.fitZoom());
}

void TestPreviewCanvas::transformationModeIsNearestAtHighZoom() {
    PreviewCanvas canvas;
    canvas.resize(400, 300);
    canvas.setBeforeImage(solidImage(800, 600, kRed));
    canvas.setAfterImage(solidImage(800, 600, kBlue));

    // Smooth while at/below fit; nearest once magnified >= 200%.
    QCOMPARE(canvas.transformationMode(), Qt::SmoothTransformation);
    canvas.zoomAt(canvas.rect().center(), 3.0); // 0.5 * 3 = 1.5
    QCOMPARE(canvas.transformationMode(), Qt::SmoothTransformation);
    canvas.zoomAt(canvas.rect().center(), 2.0); // 1.5 * 2 = 3.0
    QCOMPARE(canvas.transformationMode(), Qt::FastTransformation);
    canvas.zoomAt(canvas.rect().center(), 0.5); // 1.5
    QCOMPARE(canvas.transformationMode(), Qt::SmoothTransformation);
}

void TestPreviewCanvas::wipeDividerHitTestAndDrag() {
    PreviewCanvas canvas;
    canvas.resize(400, 300);
    canvas.setBeforeImage(solidImage(200, 200, kRed));
    canvas.setAfterImage(solidImage(200, 200, kBlue));

    // Divider at 50% of the image area; wide grab area around it.
    canvas.setDividerPosition(0.5);
    const double dividerX = canvas.dividerViewX();
    QVERIFY(canvas.isNearDivider(QPointF(dividerX + 3, 150)));
    QVERIFY(canvas.isNearDivider(QPointF(dividerX - 4, 150)));
    QVERIFY(!canvas.isNearDivider(QPointF(dividerX + 20, 150)));
    // Outside the image's vertical span is never a grab.
    QVERIFY(!canvas.isNearDivider(QPointF(dividerX, 5)));

    // Dragging maps view x -> fraction, clamped to the image area.
    canvas.setDividerFromViewX(dividerX + 50); // +25% of the 200px image width
    QCOMPARE(canvas.dividerPosition(), 0.75);
    canvas.setDividerFromViewX(dividerX - 1000);
    QCOMPARE(canvas.dividerPosition(), 0.0);
    canvas.setDividerFromViewX(dividerX + 1000);
    QCOMPARE(canvas.dividerPosition(), 1.0);
}

void TestPreviewCanvas::wipeRenderSplitsBeforeAndAfter() {
    PreviewCanvas canvas;
    canvas.resize(200, 200);
    canvas.setBeforeImage(solidImage(200, 200, kRed));
    canvas.setAfterImage(solidImage(200, 200, kBlue));
    canvas.setWipeEnabled(true);
    canvas.setDividerPosition(0.5);

    const QPixmap grab = canvas.grab();
    const QImage rendered = grab.toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(rendered.pixelColor(10, 100), QColor(kRed));
    QCOMPARE(rendered.pixelColor(190, 100), QColor(kBlue));
}

void TestPreviewCanvas::wipeDisabledRendersAfterOnly() {
    PreviewCanvas canvas;
    canvas.resize(200, 200);
    canvas.setBeforeImage(solidImage(200, 200, kRed));
    canvas.setAfterImage(solidImage(200, 200, kBlue));
    canvas.setWipeEnabled(false);

    const QImage rendered = canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(rendered.pixelColor(10, 100), QColor(kBlue));
    QCOMPARE(rendered.pixelColor(190, 100), QColor(kBlue));
}

void TestPreviewCanvas::missingAfterShowsBeforeOnBothSides() {
    PreviewCanvas canvas;
    canvas.resize(200, 200);
    canvas.setBeforeImage(solidImage(200, 200, kRed));
    // No after image yet (still processing): wipe shows before everywhere.
    canvas.setWipeEnabled(true);
    canvas.setDividerPosition(0.5);

    const QImage rendered = canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(rendered.pixelColor(10, 100), QColor(kRed));
    QCOMPARE(rendered.pixelColor(190, 100), QColor(kRed));
}

void TestPreviewCanvas::compareAvailabilityRules() {
    PreviewCanvas canvas;
    canvas.resize(200, 200);

    // No images: nothing to compare.
    QVERIFY(!canvas.isCompareAvailable());

    // Both present, GIFs excluded via setCompareAvailable(false).
    canvas.setBeforeImage(solidImage(200, 200, kRed));
    QVERIFY(!canvas.isCompareAvailable());
    canvas.setAfterImage(solidImage(200, 200, kBlue));
    QVERIFY(canvas.isCompareAvailable());

    canvas.setCompareAllowed(false);
    QVERIFY(!canvas.isCompareAvailable());
}

void TestPreviewCanvas::compareWorksWithMismatchedImageSizes() {
    // Upscaling produces a larger result than the source; the wipe must still
    // work by drawing both into the same display rect.
    PreviewCanvas canvas;
    canvas.resize(200, 200);
    canvas.setBeforeImage(solidImage(100, 100, kRed));
    canvas.setAfterImage(solidImage(400, 400, kBlue));
    canvas.setWipeEnabled(true);
    canvas.setDividerPosition(0.5);

    const QImage rendered = canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(rendered.pixelColor(10, 100), QColor(kRed));
    QCOMPARE(rendered.pixelColor(190, 100), QColor(kBlue));
}

void TestPreviewCanvas::resetViewRestoresFitAndCenteredDivider() {
    PreviewCanvas canvas;
    canvas.resize(400, 300);
    canvas.setBeforeImage(solidImage(800, 600, kRed));
    canvas.setAfterImage(solidImage(800, 600, kBlue));

    canvas.zoomAt(canvas.rect().center(), 8.0);
    canvas.setDividerPosition(0.9);

    canvas.resetView();

    QCOMPARE(canvas.zoom(), canvas.fitZoom());
    QCOMPARE(canvas.dividerPosition(), 0.5);
}

QTEST_MAIN(TestPreviewCanvas)
#include "test_preview_canvas.moc"
