#include <QtTest>

#include "core/GifIO.h"

#include <QImage>
#include <QTemporaryDir>

namespace {

QImage solidFrame(int size, const QColor& color) {
    QImage image(size, size, QImage::Format_RGBA8888);
    image.fill(color);
    return image;
}

// A frame with a fully-transparent square hole in the middle, the shape
// BackgroundRemovalStep output takes once thresholded down to GIF's on/off
// transparency.
QImage frameWithHole(int size, const QColor& color) {
    QImage image = solidFrame(size, color);
    for (int y = size / 4; y < size * 3 / 4; ++y) {
        for (int x = size / 4; x < size * 3 / 4; ++x) {
            image.setPixelColor(x, y, QColor(0, 0, 0, 0));
        }
    }
    return image;
}

} // namespace

class TestGifIO : public QObject {
    Q_OBJECT

private slots:
    void isAnimatedIsFalseForStaticImage();
    void isAnimatedIsTrueForMultiFrameGif();
    void writeThenReadRoundTripsFrameCountAndDelay();
    void writeThenReadPreservesOpaqueColorApproximately();
    void writeThenReadPreservesTransparencyHole();
    void writeFailsOnEmptyFrameList();

    void writerRoundTripsFramesEncodedOneAtATime();
    void writerFinishFailsIfNothingWasEncoded();
    void writerEncodeFailsOnSizeMismatch();
    void writerDestructorFinishesTheFile();
};

void TestGifIO::isAnimatedIsFalseForStaticImage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("static.gif");
    // Qt's bundled GIF plugin is read-only, so building even a single-frame
    // fixture goes through GifIO::writeFrames rather than QImage::save.
    QVERIFY(GifIO::writeFrames(path, {{solidFrame(8, Qt::red), 10}}));

    QVERIFY(!GifIO::isAnimated(path));
}

void TestGifIO::isAnimatedIsTrueForMultiFrameGif() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("anim.gif");

    std::vector<GifIO::Frame> frames{{solidFrame(8, Qt::red), 10}, {solidFrame(8, Qt::blue), 10}};
    QVERIFY(GifIO::writeFrames(path, frames));

    QVERIFY(GifIO::isAnimated(path));
}

void TestGifIO::writeThenReadRoundTripsFrameCountAndDelay() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("anim.gif");

    std::vector<GifIO::Frame> frames{
        {solidFrame(8, Qt::red), 20}, {solidFrame(8, Qt::green), 40}, {solidFrame(8, Qt::blue), 5}};
    QVERIFY(GifIO::writeFrames(path, frames));

    const std::vector<GifIO::Frame> readBack = GifIO::readFrames(path);
    QCOMPARE(readBack.size(), frames.size());
    QCOMPARE(readBack[0].delayCs, 20);
    QCOMPARE(readBack[1].delayCs, 40);
    QCOMPARE(readBack[2].delayCs, 5);
}

void TestGifIO::writeThenReadPreservesOpaqueColorApproximately() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("anim.gif");

    std::vector<GifIO::Frame> frames{{solidFrame(8, Qt::red), 10}, {solidFrame(8, Qt::blue), 10}};
    QVERIFY(GifIO::writeFrames(path, frames));

    const std::vector<GifIO::Frame> readBack = GifIO::readFrames(path);
    QCOMPARE(readBack.size(), 2u);
    // A solid-color frame quantizes to (at most) one palette entry, so this
    // should round-trip exactly rather than just approximately.
    QCOMPARE(readBack[0].image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(readBack[1].image.pixelColor(0, 0), QColor(Qt::blue));
}

void TestGifIO::writeThenReadPreservesTransparencyHole() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("anim.gif");

    std::vector<GifIO::Frame> frames{{frameWithHole(20, Qt::red), 10},
                                      {frameWithHole(20, Qt::blue), 10}};
    QVERIFY(GifIO::writeFrames(path, frames));

    const std::vector<GifIO::Frame> readBack = GifIO::readFrames(path);
    QCOMPARE(readBack.size(), 2u);
    // Center is inside the hole: fully transparent in both frames.
    QCOMPARE(readBack[0].image.pixelColor(10, 10).alpha(), 0);
    QCOMPARE(readBack[1].image.pixelColor(10, 10).alpha(), 0);
    // Corner is outside the hole: fully opaque, still the frame's own color
    // (i.e. the second frame's hole didn't get ghosted-in with the first
    // frame's opaque content, which DISPOSE_DO_NOT would have caused).
    QCOMPARE(readBack[0].image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(readBack[1].image.pixelColor(0, 0), QColor(Qt::blue));
}

void TestGifIO::writeFailsOnEmptyFrameList() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!GifIO::writeFrames(dir.filePath("empty.gif"), {}));
}

void TestGifIO::writerRoundTripsFramesEncodedOneAtATime() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("stream.gif");

    // No palette source: the first encoded frame defines the shared
    // palette, so its colors must cover the whole animation for an exact
    // round-trip. This is the BatchRunner/MainWindow streaming pattern.
    QImage halfRedHalfGreen = solidFrame(8, Qt::red);
    for (int y = 0; y < 8; ++y) {
        for (int x = 4; x < 8; ++x) {
            halfRedHalfGreen.setPixelColor(x, y, Qt::green);
        }
    }
    QImage swapped = halfRedHalfGreen.mirrored(true, false);

    GifIO::Writer writer;
    QVERIFY(writer.open(path));
    QVERIFY(writer.encode({halfRedHalfGreen, 20}));
    QVERIFY(writer.encode({swapped, 40}));
    QVERIFY(writer.finish());

    QVERIFY(GifIO::isAnimated(path));
    const std::vector<GifIO::Frame> readBack = GifIO::readFrames(path);
    QCOMPARE(readBack.size(), 2u);
    QCOMPARE(readBack[0].delayCs, 20);
    QCOMPARE(readBack[1].delayCs, 40);
    QCOMPARE(readBack[0].image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(readBack[0].image.pixelColor(7, 0), QColor(Qt::green));
    QCOMPARE(readBack[1].image.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(readBack[1].image.pixelColor(7, 0), QColor(Qt::red));
}

void TestGifIO::writerFinishFailsIfNothingWasEncoded() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Covers the unreadable-input path: open() succeeds, but without any
    // encode() there is no valid GIF to finish.
    GifIO::Writer writer;
    QVERIFY(writer.open(dir.filePath("never-started.gif")));
    QVERIFY(!writer.finish());
    QVERIFY(!QFile::exists(dir.filePath("never-started.gif")));
}

void TestGifIO::writerEncodeFailsOnSizeMismatch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    GifIO::Writer writer;
    QVERIFY(writer.open(dir.filePath("mismatch.gif")));
    QVERIFY(writer.encode({solidFrame(8, Qt::red), 10}));
    QVERIFY(!writer.encode({solidFrame(16, Qt::red), 10}));
}

void TestGifIO::writerDestructorFinishesTheFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("dtor.gif");

    {
        GifIO::Writer writer;
        QVERIFY(writer.open(path));
        QVERIFY(writer.encode({solidFrame(8, Qt::red), 10}));
        // No finish(): the destructor must still close giflib cleanly
        // enough that the frames written so far are a readable GIF.
    }

    const std::vector<GifIO::Frame> readBack = GifIO::readFrames(path);
    QCOMPARE(readBack.size(), 1u);
    QCOMPARE(readBack[0].image.pixelColor(0, 0), QColor(Qt::red));
}

QTEST_MAIN(TestGifIO)
#include "test_gif_io.moc"
