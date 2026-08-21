#include <QtTest>

#include "core/BokehStep.h"
#include "core/SegmentationModel.h"

namespace {

// A SegmentationModel test double so BokehStep's blend/blur logic can be
// exercised without ncnn, a GPU, or a real model file (same fake shape as
// test_background_removal_step.cpp).
class FakeSegmentationModel : public SegmentationModel {
public:
    bool ready = true;
    QImage maskToReturn;

    bool isReady() const override { return ready; }
    QImage computeMask(const QImage& /*input*/) const override { return maskToReturn; }
};

QImage solidMask(int width, int height, uchar value) {
    QImage mask(width, height, QImage::Format_Alpha8);
    mask.fill(0);
    for (int y = 0; y < height; ++y) {
        uchar* row = mask.scanLine(y);
        for (int x = 0; x < width; ++x) {
            row[x] = value;
        }
    }
    return mask;
}

} // namespace

class TestBokehStep : public QObject {
    Q_OBJECT

private slots:
    void notReadyReturnsInputUnchanged();
    void nullMaskReturnsInputUnchanged();
    void mismatchedMaskSizeReturnsInputUnchanged();
    void fullyForegroundMaskKeepsSharpPixels();
    void fullyBackgroundMaskBlursDetailAway();
    void alphaChannelPassesThroughUnchanged();
};

void TestBokehStep::notReadyReturnsInputUnchanged() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->ready = false;

    BokehStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::red);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
    QCOMPARE(step.isReady(), false);
}

void TestBokehStep::nullMaskReturnsInputUnchanged() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = QImage();

    BokehStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::blue);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
}

void TestBokehStep::mismatchedMaskSizeReturnsInputUnchanged() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(2, 2, 0);

    BokehStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::green);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
}

void TestBokehStep::fullyForegroundMaskKeepsSharpPixels() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(4, 1, 255);

    BokehStep step(model, /*blurRadius=*/5);

    QImage input(4, 1, QImage::Format_RGB32);
    input.setPixelColor(0, 0, QColor(10, 20, 30));
    input.setPixelColor(1, 0, QColor(200, 100, 50));
    input.setPixelColor(2, 0, QColor(0, 255, 0));
    input.setPixelColor(3, 0, QColor(255, 0, 0));

    const QImage output = step.process(input);

    // Full mask weight means the blend is 100% sharp, regardless of the blur
    // implementation, so output colors should be exactly the input colors.
    QCOMPARE(qRed(output.pixel(0, 0)), 10);
    QCOMPARE(qGreen(output.pixel(0, 0)), 20);
    QCOMPARE(qBlue(output.pixel(0, 0)), 30);
    QCOMPARE(qRed(output.pixel(3, 0)), 255);
    QCOMPARE(qAlpha(output.pixel(0, 0)), 255);
}

void TestBokehStep::fullyBackgroundMaskBlursDetailAway() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(5, 5, 0);

    BokehStep step(model, /*blurRadius=*/5);

    // A single bright pixel on an otherwise dark field: blurring should
    // spread its brightness into neighbors and dim the pixel itself.
    QImage input(5, 5, QImage::Format_RGB32);
    input.fill(Qt::black);
    input.setPixelColor(2, 2, QColor(255, 255, 255));

    const QImage output = step.process(input);

    QVERIFY(qRed(output.pixel(2, 2)) < 255);
    QVERIFY(qRed(output.pixel(1, 2)) > 0);
}

void TestBokehStep::alphaChannelPassesThroughUnchanged() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(3, 1, 128);

    BokehStep step(model, /*blurRadius=*/3);

    QImage input(3, 1, QImage::Format_ARGB32);
    input.setPixelColor(0, 0, QColor::fromRgba(qRgba(10, 20, 30, 255)));
    input.setPixelColor(1, 0, QColor::fromRgba(qRgba(40, 50, 60, 128)));
    input.setPixelColor(2, 0, QColor::fromRgba(qRgba(70, 80, 90, 0)));

    const QImage output = step.process(input);

    QCOMPARE(qAlpha(output.pixel(0, 0)), 255);
    QCOMPARE(qAlpha(output.pixel(1, 0)), 128);
    QCOMPARE(qAlpha(output.pixel(2, 0)), 0);
}

QTEST_MAIN(TestBokehStep)
#include "test_bokeh_step.moc"
