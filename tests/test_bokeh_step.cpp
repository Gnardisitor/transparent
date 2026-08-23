#include <QtTest>

#include "core/BokehStep.h"
#include "core/SegmentationModel.h"

namespace {

// A SegmentationModel test double so BokehStep's blend/blur logic can be
// exercised without ncnn, a GPU, or a real model file (same fake shape as
// test_background_removal_step.cpp). computeMaskCallCount lets the cache
// tests assert reblendCached() never triggers a fresh mask computation.
class FakeSegmentationModel : public SegmentationModel {
public:
    bool ready = true;
    QImage maskToReturn;
    mutable int computeMaskCallCount = 0;

    bool isReady() const override { return ready; }
    QImage computeMask(const QImage& /*input*/) const override {
        ++computeMaskCallCount;
        return maskToReturn;
    }
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
    void zeroStrengthLeavesImageUnblurred();

    void reblendCachedIsNullBeforeAnyProcessCall();
    void reblendCachedReusesLastMaskWithoutRecomputingIt();
    void reblendCachedReflectsTheRequestedStrengthNotTheStoredOne();
    void setModelInvalidatesTheCache();
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

    // Full mask weight means the blend is 100% sharp regardless of blur
    // strength, so the exact strength here doesn't matter to this test.
    BokehStep step(model, /*strengthPercent=*/100);

    QImage input(4, 1, QImage::Format_RGB32);
    input.setPixelColor(0, 0, QColor(10, 20, 30));
    input.setPixelColor(1, 0, QColor(200, 100, 50));
    input.setPixelColor(2, 0, QColor(0, 255, 0));
    input.setPixelColor(3, 0, QColor(255, 0, 0));

    const QImage output = step.process(input);

    QCOMPARE(qRed(output.pixel(0, 0)), 10);
    QCOMPARE(qGreen(output.pixel(0, 0)), 20);
    QCOMPARE(qBlue(output.pixel(0, 0)), 30);
    QCOMPARE(qRed(output.pixel(3, 0)), 255);
    QCOMPARE(qAlpha(output.pixel(0, 0)), 255);
}

void TestBokehStep::fullyBackgroundMaskBlursDetailAway() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(41, 41, 0);

    // Strength maps to a radius relative to the image's shorter side (see
    // BokehStep::radiusForImage), so this needs a large-enough image for
    // 100% to round to a non-zero radius — a handful of pixels doesn't,
    // which is correct behavior, just not useful for exercising the blur
    // itself.
    BokehStep step(model, /*strengthPercent=*/100);

    // A single bright pixel on an otherwise dark field: blurring should
    // spread its brightness into neighbors and dim the pixel itself.
    QImage input(41, 41, QImage::Format_RGB32);
    input.fill(Qt::black);
    input.setPixelColor(20, 20, QColor(255, 255, 255));

    const QImage output = step.process(input);

    QVERIFY(qRed(output.pixel(20, 20)) < 255);
    QVERIFY(qRed(output.pixel(19, 20)) > 0);
}

void TestBokehStep::alphaChannelPassesThroughUnchanged() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(3, 1, 128);

    BokehStep step(model, /*strengthPercent=*/60);

    QImage input(3, 1, QImage::Format_ARGB32);
    input.setPixelColor(0, 0, QColor::fromRgba(qRgba(10, 20, 30, 255)));
    input.setPixelColor(1, 0, QColor::fromRgba(qRgba(40, 50, 60, 128)));
    input.setPixelColor(2, 0, QColor::fromRgba(qRgba(70, 80, 90, 0)));

    const QImage output = step.process(input);

    QCOMPARE(qAlpha(output.pixel(0, 0)), 255);
    QCOMPARE(qAlpha(output.pixel(1, 0)), 128);
    QCOMPARE(qAlpha(output.pixel(2, 0)), 0);
}

void TestBokehStep::zeroStrengthLeavesImageUnblurred() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(41, 41, 0);

    BokehStep step(model, /*strengthPercent=*/0);

    QImage input(41, 41, QImage::Format_RGB32);
    input.fill(Qt::black);
    input.setPixelColor(20, 20, QColor(255, 255, 255));

    const QImage output = step.process(input);

    // radiusForImage(0%) == 0, so blur is a no-op: the bright pixel stays
    // exactly as bright, and doesn't spread to its neighbor.
    QCOMPARE(qRed(output.pixel(20, 20)), 255);
    QCOMPARE(qRed(output.pixel(19, 20)), 0);
}

void TestBokehStep::reblendCachedIsNullBeforeAnyProcessCall() {
    auto model = std::make_shared<FakeSegmentationModel>();
    BokehStep step(model);

    QVERIFY(!step.hasCachedMask());
    QVERIFY(step.reblendCached(50).isNull());
}

void TestBokehStep::reblendCachedReusesLastMaskWithoutRecomputingIt() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(41, 41, 0);
    BokehStep step(model);

    QImage input(41, 41, QImage::Format_RGB32);
    input.fill(Qt::black);
    input.setPixelColor(20, 20, QColor(255, 255, 255));

    const QImage processed = step.process(input);
    QCOMPARE(model->computeMaskCallCount, 1);
    QVERIFY(step.hasCachedMask());

    const QImage reblended = step.reblendCached(step.strengthPercent());

    // Same mask, same input, same strength: the live-preview path should
    // reproduce process()'s own output exactly, without touching the model
    // a second time.
    QCOMPARE(model->computeMaskCallCount, 1);
    QCOMPARE(reblended, processed);
}

void TestBokehStep::reblendCachedReflectsTheRequestedStrengthNotTheStoredOne() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(41, 41, 0);
    BokehStep step(model, /*strengthPercent=*/50);

    QImage input(41, 41, QImage::Format_RGB32);
    input.fill(Qt::black);
    input.setPixelColor(20, 20, QColor(255, 255, 255));
    step.process(input);

    // reblendCached()'s argument is independent of strengthPercent_ (a live
    // slider drag passes its in-flight value explicitly rather than storing
    // it first, see BokehStep.h) — the stored value (50) must stay
    // untouched by this call.
    const QImage atZero = step.reblendCached(0);
    const QImage atMax = step.reblendCached(100);

    QCOMPARE(step.strengthPercent(), 50);
    QCOMPARE(qRed(atZero.pixel(20, 20)), 255); // 0%: no blur at all.
    QVERIFY(qRed(atMax.pixel(20, 20)) < qRed(atZero.pixel(20, 20)));
}

void TestBokehStep::setModelInvalidatesTheCache() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(4, 4, 0);
    BokehStep step(model);

    QImage input(4, 4, QImage::Format_RGB32);
    input.fill(Qt::black);
    step.process(input);
    QVERIFY(step.hasCachedMask());

    step.setModel(std::make_shared<FakeSegmentationModel>());

    QVERIFY(!step.hasCachedMask());
}

QTEST_MAIN(TestBokehStep)
#include "test_bokeh_step.moc"
