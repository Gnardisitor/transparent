#include <QtTest>

#include "core/BackgroundRemovalStep.h"
#include "core/SegmentationModel.h"

namespace {

// A SegmentationModel test double so BackgroundRemovalStep's compositing
// logic can be exercised without ncnn, a GPU, or a real model file.
class FakeSegmentationModel : public SegmentationModel {
public:
    bool ready = true;
    QImage maskToReturn;

    bool isReady() const override { return ready; }
    QImage computeMask(const QImage& /*input*/) const override { return maskToReturn; }
};

} // namespace

class TestBackgroundRemovalStep : public QObject {
    Q_OBJECT

private slots:
    void notReadyReturnsInputUnchanged();
    void nullMaskReturnsInputUnchanged();
    void mismatchedMaskSizeReturnsInputUnchanged();
    void compositesMaskAsAlpha();
};

void TestBackgroundRemovalStep::notReadyReturnsInputUnchanged() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->ready = false;

    BackgroundRemovalStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::red);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
    QCOMPARE(step.isReady(), false);
}

void TestBackgroundRemovalStep::nullMaskReturnsInputUnchanged() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = QImage();

    BackgroundRemovalStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::blue);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
}

void TestBackgroundRemovalStep::mismatchedMaskSizeReturnsInputUnchanged() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = QImage(2, 2, QImage::Format_Alpha8);
    model->maskToReturn.fill(0);

    BackgroundRemovalStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::green);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
}

void TestBackgroundRemovalStep::compositesMaskAsAlpha() {
    auto model = std::make_shared<FakeSegmentationModel>();

    QImage mask(4, 1, QImage::Format_Alpha8);
    // Left half fully foreground, right half fully background.
    mask.setPixelColor(0, 0, QColor::fromRgba(qRgba(0, 0, 0, 255)));
    mask.setPixelColor(1, 0, QColor::fromRgba(qRgba(0, 0, 0, 255)));
    mask.setPixelColor(2, 0, QColor::fromRgba(qRgba(0, 0, 0, 0)));
    mask.setPixelColor(3, 0, QColor::fromRgba(qRgba(0, 0, 0, 0)));
    model->maskToReturn = mask;

    BackgroundRemovalStep step(model);

    QImage input(4, 1, QImage::Format_RGB32);
    input.fill(qRgb(10, 20, 30));

    const QImage output = step.process(input);

    QCOMPARE(output.format(), QImage::Format_RGBA8888);
    QCOMPARE(qAlpha(output.pixel(0, 0)), 255);
    QCOMPARE(qAlpha(output.pixel(1, 0)), 255);
    QCOMPARE(qAlpha(output.pixel(2, 0)), 0);
    QCOMPARE(qAlpha(output.pixel(3, 0)), 0);
    QCOMPARE(qRed(output.pixel(0, 0)), 10);
    QCOMPARE(qGreen(output.pixel(0, 0)), 20);
    QCOMPARE(qBlue(output.pixel(0, 0)), 30);
}

QTEST_MAIN(TestBackgroundRemovalStep)
#include "test_background_removal_step.moc"
