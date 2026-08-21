#include <QtTest>

#include "core/UpscaleModel.h"
#include "core/UpscaleStep.h"

namespace {

// QImage::fill(QRgb) on Format_RGBA8888 doesn't do the format-aware
// conversion pixel()/setPixelColor() do (same quirk noted in
// BackgroundRemovalStep.cpp): it can write bytes in ARGB32's bit layout
// rather than RGBA8888's byte order, swapping red/blue. Write the raw bytes
// directly instead so fixture images have the color they claim to.
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

// An UpscaleModel test double so UpscaleStep's alpha-preserving logic can be
// exercised without vision.cpp, a GPU, or a real model file.
class FakeUpscaleModel : public UpscaleModel {
public:
    bool ready = true;
    QImage imageToReturn;

    bool isReady() const override { return ready; }
    QImage upscale(const QImage& /*input*/) const override { return imageToReturn; }
};

} // namespace

class TestUpscaleStep : public QObject {
    Q_OBJECT

private slots:
    void notReadyReturnsInputUnchanged();
    void nullResultReturnsInputUnchanged();
    void opaqueInputReturnsUpscaledResultAsIs();
    void transparentInputGetsUpscaledAlphaChannel();
};

void TestUpscaleStep::notReadyReturnsInputUnchanged() {
    auto model = std::make_shared<FakeUpscaleModel>();
    model->ready = false;

    UpscaleStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::red);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
    QCOMPARE(step.isReady(), false);
}

void TestUpscaleStep::nullResultReturnsInputUnchanged() {
    auto model = std::make_shared<FakeUpscaleModel>();
    model->imageToReturn = QImage();

    UpscaleStep step(model);

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::blue);

    const QImage output = step.process(input);

    QCOMPARE(output, input);
}

void TestUpscaleStep::opaqueInputReturnsUpscaledResultAsIs() {
    auto model = std::make_shared<FakeUpscaleModel>();
    model->imageToReturn = makeRgba8888(8, 8, 10, 20, 30, 255);

    UpscaleStep step(model);

    QImage input(4, 4, QImage::Format_RGB32);
    input.fill(qRgb(1, 2, 3));

    const QImage output = step.process(input);

    QCOMPARE(output.size(), QSize(8, 8));
    QCOMPARE(qRed(output.pixel(0, 0)), 10);
    QCOMPARE(qAlpha(output.pixel(0, 0)), 255);
}

void TestUpscaleStep::transparentInputGetsUpscaledAlphaChannel() {
    auto model = std::make_shared<FakeUpscaleModel>();
    // The model only ever sees/returns RGB, forced fully opaque, mirroring
    // vision.cpp's esrgan_compute output.
    model->imageToReturn = makeRgba8888(4, 1, 100, 150, 200, 255);

    UpscaleStep step(model);

    // Left half of a 2x1 input is fully foreground, right half fully
    // background; upscaled 2x should keep that split at the 4-wide result.
    QImage input(2, 1, QImage::Format_ARGB32);
    input.setPixelColor(0, 0, QColor::fromRgba(qRgba(0, 0, 0, 255)));
    input.setPixelColor(1, 0, QColor::fromRgba(qRgba(0, 0, 0, 0)));

    const QImage output = step.process(input);

    QCOMPARE(output.size(), QSize(4, 1));
    QCOMPARE(qRed(output.pixel(0, 0)), 100);
    QVERIFY(qAlpha(output.pixel(0, 0)) > 200);
    QVERIFY(qAlpha(output.pixel(3, 0)) < 50);
}

QTEST_MAIN(TestUpscaleStep)
#include "test_upscale_step.moc"
