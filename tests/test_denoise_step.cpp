#include <QtTest>

#include "core/DenoiseModel.h"
#include "core/DenoiseStep.h"

namespace {

// QImage::fill(QRgb) on RGBA8888 writes ARGB32 byte order (same quirk noted in
// UpscaleStep's tests), so write raw bytes for fixtures with distinct colors.
QImage makeRgba8888(int width, int height, uchar r, uchar g, uchar b, uchar a) {
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = a;
        }
    }
    return image;
}

// A DenoiseModel test double so DenoiseStep's alpha-preserving logic can be
// exercised without vision.cpp, a GPU, or a real model file.
class FakeDenoiseModel : public DenoiseModel {
public:
    bool ready = true;
    QImage imageToReturn;

    bool isReady() const override { return ready; }
    QImage denoise(const QImage& /*input*/) const override { return imageToReturn; }
};

} // namespace

class TestDenoiseStep : public QObject {
    Q_OBJECT

private slots:
    void notReadyReturnsInputUnchanged();
    void nullResultReturnsInputUnchanged();
    void opaqueInputStaysOpaque();
    void transparentInputKeepsAlphaUnscaled();

    void modelFailureReturnsInputUnchanged();
};

void TestDenoiseStep::notReadyReturnsInputUnchanged() {
    auto model = std::make_shared<FakeDenoiseModel>();
    model->ready = false;

    DenoiseStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::red);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
    QCOMPARE(step.isReady(), false);
}

void TestDenoiseStep::nullResultReturnsInputUnchanged() {
    auto model = std::make_shared<FakeDenoiseModel>();
    model->imageToReturn = QImage();

    DenoiseStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::blue);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
}

void TestDenoiseStep::opaqueInputStaysOpaque() {
    auto model = std::make_shared<FakeDenoiseModel>();
    // The model only ever sees/returns RGB; the step forces opaque output for
    // inputs that had no alpha channel.
    model->imageToReturn = makeRgba8888(4, 4, 10, 20, 30, 255);

    DenoiseStep step(model);

    QImage input(4, 4, QImage::Format_RGB32);
    input.fill(qRgb(1, 2, 3));

    const QImage output = step.process(input);

    QCOMPARE(output.size(), QSize(4, 4));
    QCOMPARE(qRed(output.pixel(0, 0)), 10);
    QCOMPARE(qGreen(output.pixel(0, 0)), 20);
    QCOMPARE(qBlue(output.pixel(0, 0)), 30);
    QCOMPARE(qAlpha(output.pixel(0, 0)), 255);
}

void TestDenoiseStep::transparentInputKeepsAlphaUnscaled() {
    auto model = std::make_shared<FakeDenoiseModel>();
    // Denoising preserves resolution, so the step must copy the input's own
    // alpha verbatim (unlike UpscaleStep, which rescales it) while the model
    // only ever sees RGB, forced fully opaque.
    model->imageToReturn = makeRgba8888(2, 1, 100, 150, 200, 255);

    DenoiseStep step(model);

    QImage input = makeRgba8888(2, 1, 9, 9, 9, 255);
    input.setPixel(1, 0, qRgba(0, 0, 0, 0)); // right pixel fully transparent

    const QImage output = step.process(input);

    QCOMPARE(output.size(), QSize(2, 1));
    QCOMPARE(qRed(output.pixel(0, 0)), 100);
    QCOMPARE(qAlpha(output.pixel(0, 0)), 255);
    QCOMPARE(qRed(output.pixel(1, 0)), 100);
    QCOMPARE(qAlpha(output.pixel(1, 0)), 0);
}

void TestDenoiseStep::modelFailureReturnsInputUnchanged() {
    // VisionCppDenoiseModel maps inference failures to a null QImage; the step
    // must degrade to returning the input rather than dropping the image.
    auto model = std::make_shared<FakeDenoiseModel>();
    model->imageToReturn = QImage();

    DenoiseStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::green);

    QCOMPARE(step.process(input), input);
}

QTEST_MAIN(TestDenoiseStep)
#include "test_denoise_step.moc"
