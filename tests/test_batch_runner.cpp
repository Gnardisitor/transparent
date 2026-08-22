#include <QtTest>

#include "core/BatchRunner.h"
#include "core/GifIO.h"
#include "core/Pipeline.h"

#include <QDir>
#include <QTemporaryDir>

namespace {

// Deterministic, GPU-free step so batch behavior can be tested without a
// real segmentation model, same approach test_pipeline.cpp uses.
class InvertStep : public PipelineStep {
public:
    QImage process(const QImage& input) const override {
        QImage out = input.convertToFormat(QImage::Format_ARGB32);
        out.invertPixels();
        return out;
    }
    QString name() const override { return QStringLiteral("Invert"); }
};

QImage makeTestImage(const QColor& color) {
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

// Records step ordering via a PNG tEXt chunk (QImage::text() survives a
// save/load round-trip), same approach test_pipeline.cpp uses for sequencing.
class TagStep : public PipelineStep {
public:
    explicit TagStep(QString tag) : tag_(std::move(tag)) {}

    QImage process(const QImage& input) const override {
        QImage out = input;
        out.setText(QStringLiteral("tag"), out.text(QStringLiteral("tag")) + tag_);
        return out;
    }
    QString name() const override { return QStringLiteral("Tag(%1)").arg(tag_); }

private:
    QString tag_;
};

} // namespace

class TestBatchRunner : public QObject {
    Q_OBJECT

private slots:
    void discoverImagesFindsOnlySupportedFilesSortedByName();
    void runAppliesPipelineAndWritesPngPerImage();
    void runSkipsUnreadableFileButContinuesBatch();
    void runReportsProgressPerImage();
    void runCreatesOutputFolderIfMissing();
    void runFailsSecondFileInsteadOfOverwritingOnOutputNameCollision();
    void runWithStepOrderAppliesOnlyListedStepsInGivenOrder();
    void runWritesAnimatedGifPerFrameInsteadOfPng();
    void runTreatsSingleFrameGifAsPlainImage();
};

void TestBatchRunner::discoverImagesFindsOnlySupportedFilesSortedByName() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    makeTestImage(Qt::red).save(dir.filePath("b.png"));
    makeTestImage(Qt::green).save(dir.filePath("a.jpg"));
    QFile notAnImage(dir.filePath("readme.txt"));
    QVERIFY(notAnImage.open(QIODevice::WriteOnly));
    notAnImage.write("not an image");
    notAnImage.close();
    QDir(dir.path()).mkdir("subfolder");

    BatchRunner runner(std::make_shared<Pipeline>());
    const QStringList images = runner.discoverImages(dir.path());

    QCOMPARE(images.size(), 2);
    QVERIFY(images[0].endsWith("a.jpg"));
    QVERIFY(images[1].endsWith("b.png"));
}

void TestBatchRunner::runAppliesPipelineAndWritesPngPerImage() {
    QTemporaryDir inputDir;
    QTemporaryDir outputDir;
    QVERIFY(inputDir.isValid() && outputDir.isValid());

    makeTestImage(Qt::red).save(inputDir.filePath("photo.png"));

    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<InvertStep>());
    BatchRunner runner(pipeline);

    const BatchResult result = runner.run(inputDir.path(), outputDir.path());

    QCOMPARE(result.succeeded, 1);
    QVERIFY(result.failedFiles.isEmpty());

    const QImage output(outputDir.filePath("photo.png"));
    QVERIFY(!output.isNull());
    // InvertStep inverts RGB (not alpha), so red -> cyan.
    QCOMPARE(output.pixelColor(0, 0), QColor(Qt::cyan));
}

void TestBatchRunner::runSkipsUnreadableFileButContinuesBatch() {
    QTemporaryDir inputDir;
    QTemporaryDir outputDir;
    QVERIFY(inputDir.isValid() && outputDir.isValid());

    makeTestImage(Qt::blue).save(inputDir.filePath("good.png"));

    // A file with an image extension but garbage contents: QImage::load
    // will fail on it, exercising the "unreadable source" failure path.
    QFile corrupt(inputDir.filePath("corrupt.png"));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("this is not png data");
    corrupt.close();

    BatchRunner runner(std::make_shared<Pipeline>());
    const BatchResult result = runner.run(inputDir.path(), outputDir.path());

    QCOMPARE(result.succeeded, 1);
    QCOMPARE(result.failedFiles.size(), 1);
    QCOMPARE(result.failedFiles.first(), QStringLiteral("corrupt.png"));
    QVERIFY(QFile::exists(outputDir.filePath("good.png")));
}

void TestBatchRunner::runReportsProgressPerImage() {
    QTemporaryDir inputDir;
    QTemporaryDir outputDir;
    QVERIFY(inputDir.isValid() && outputDir.isValid());

    makeTestImage(Qt::red).save(inputDir.filePath("one.png"));
    makeTestImage(Qt::green).save(inputDir.filePath("two.png"));

    BatchRunner runner(std::make_shared<Pipeline>());

    QList<QPair<int, int>> progressCalls;
    QStringList fileNames;
    runner.run(inputDir.path(), outputDir.path(),
               [&](int done, int total, const QString& fileName) {
                   progressCalls.append({done, total});
                   fileNames.append(fileName);
               });

    QCOMPARE(progressCalls.size(), 2);
    QCOMPARE(progressCalls[0], qMakePair(1, 2));
    QCOMPARE(progressCalls[1], qMakePair(2, 2));
    QCOMPARE(fileNames, QStringList({QStringLiteral("one.png"), QStringLiteral("two.png")}));
}

void TestBatchRunner::runCreatesOutputFolderIfMissing() {
    QTemporaryDir inputDir;
    QTemporaryDir outputParent;
    QVERIFY(inputDir.isValid() && outputParent.isValid());

    makeTestImage(Qt::red).save(inputDir.filePath("photo.png"));
    const QString outputFolder = outputParent.filePath("does-not-exist-yet");

    BatchRunner runner(std::make_shared<Pipeline>());
    const BatchResult result = runner.run(inputDir.path(), outputFolder);

    QCOMPARE(result.succeeded, 1);
    QVERIFY(QFile::exists(outputFolder + "/photo.png"));
}

void TestBatchRunner::runFailsSecondFileInsteadOfOverwritingOnOutputNameCollision() {
    QTemporaryDir inputDir;
    QTemporaryDir outputDir;
    QVERIFY(inputDir.isValid() && outputDir.isValid());

    // "photo.jpg" sorts before "photo.png", so it's processed first and
    // claims the shared output name "photo.png".
    makeTestImage(Qt::blue).save(inputDir.filePath("photo.jpg"), "JPG");
    makeTestImage(Qt::red).save(inputDir.filePath("photo.png"));

    BatchRunner runner(std::make_shared<Pipeline>());
    const BatchResult result = runner.run(inputDir.path(), outputDir.path());

    QCOMPARE(result.succeeded, 1);
    QCOMPARE(result.failedFiles, QStringList({QStringLiteral("photo.png")}));

    // The first (jpg-derived) output must survive untouched, not be
    // overwritten by the second image that wanted the same output name.
    const QImage output(outputDir.filePath("photo.png"));
    QVERIFY(!output.isNull());
    // Blue channel should dominate (lossy JPEG round-trip, so not exactly
    // 255); red, from the second source image, would mean it overwrote.
    QVERIFY(output.pixelColor(0, 0).blue() > 200);
    QVERIFY(output.pixelColor(0, 0).red() < 50);
}

void TestBatchRunner::runWithStepOrderAppliesOnlyListedStepsInGivenOrder() {
    QTemporaryDir inputDir;
    QTemporaryDir outputDir;
    QVERIFY(inputDir.isValid() && outputDir.isValid());

    makeTestImage(Qt::red).save(inputDir.filePath("photo.png"));

    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<TagStep>(QStringLiteral("A")));
    pipeline->addStep(std::make_shared<TagStep>(QStringLiteral("B")));
    pipeline->addStep(std::make_shared<TagStep>(QStringLiteral("C")));
    BatchRunner runner(pipeline);

    // Reversed order, and step 1 ("B") left out entirely.
    const BatchResult result =
        runner.run(inputDir.path(), outputDir.path(), nullptr, std::vector<size_t>{2, 0});

    QCOMPARE(result.succeeded, 1);

    const QImage output(outputDir.filePath("photo.png"));
    QVERIFY(!output.isNull());
    QCOMPARE(output.text(QStringLiteral("tag")), QStringLiteral("CA"));
}

void TestBatchRunner::runWritesAnimatedGifPerFrameInsteadOfPng() {
    QTemporaryDir inputDir;
    QTemporaryDir outputDir;
    QVERIFY(inputDir.isValid() && outputDir.isValid());

    std::vector<GifIO::Frame> sourceFrames{{makeTestImage(Qt::red), 10},
                                            {makeTestImage(Qt::green), 10}};
    QVERIFY(GifIO::writeFrames(inputDir.filePath("anim.gif"), sourceFrames));

    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<InvertStep>());
    BatchRunner runner(pipeline);

    const BatchResult result = runner.run(inputDir.path(), outputDir.path());

    QCOMPARE(result.succeeded, 1);
    QVERIFY(result.failedFiles.isEmpty());
    QVERIFY(QFile::exists(outputDir.filePath("anim.gif")));
    QVERIFY(!QFile::exists(outputDir.filePath("anim.png")));

    const std::vector<GifIO::Frame> output = GifIO::readFrames(outputDir.filePath("anim.gif"));
    QCOMPARE(output.size(), 2u);
    // InvertStep inverts RGB (not alpha), so red -> cyan and green -> magenta.
    QCOMPARE(output[0].image.pixelColor(0, 0), QColor(Qt::cyan));
    QCOMPARE(output[1].image.pixelColor(0, 0), QColor(Qt::magenta));
}

void TestBatchRunner::runTreatsSingleFrameGifAsPlainImage() {
    QTemporaryDir inputDir;
    QTemporaryDir outputDir;
    QVERIFY(inputDir.isValid() && outputDir.isValid());

    // Qt's bundled GIF plugin is read-only (no encoder), so a single-frame
    // GIF fixture has to be built via GifIO too, same as the app itself
    // would produce one — QImage::save(..., "GIF") silently fails.
    QVERIFY(GifIO::writeFrames(inputDir.filePath("photo.gif"), {{makeTestImage(Qt::red), 10}}));

    BatchRunner runner(std::make_shared<Pipeline>());
    const BatchResult result = runner.run(inputDir.path(), outputDir.path());

    QCOMPARE(result.succeeded, 1);
    QVERIFY(QFile::exists(outputDir.filePath("photo.png")));
    QVERIFY(!QFile::exists(outputDir.filePath("photo.gif")));
}

QTEST_MAIN(TestBatchRunner)
#include "test_batch_runner.moc"
