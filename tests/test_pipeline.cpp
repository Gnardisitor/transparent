#include <QtTest>

#include "core/Pipeline.h"

namespace {

// Deterministic, GPU-free fakes standing in for real steps (background
// removal, upscaling, ...) so the pipeline's sequencing logic can be tested
// on its own.

class MirrorStep : public PipelineStep {
public:
    QImage process(const QImage& input) const override {
        return input.mirrored(true, false);
    }
    QString name() const override { return QStringLiteral("Mirror"); }
};

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

class TestPipeline : public QObject {
    Q_OBJECT

private slots:
    void emptyPipelineReturnsInputUnchanged();
    void singleStepIsApplied();
    void stepsRunInOrder();
    void batchRunsEachInputIndependently();
    void stepsAreEnabledByDefault();
    void disabledStepIsSkippedButOthersStillRun();
    void customOrderRunsOnlyListedStepsInGivenOrder();
    void customOrderIgnoresEnabledFlag();
};

void TestPipeline::emptyPipelineReturnsInputUnchanged() {
    Pipeline pipeline;
    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::red);

    const QImage output = pipeline.run(input);

    QCOMPARE(output, input);
    QCOMPARE(pipeline.stepCount(), size_t(0));
}

void TestPipeline::singleStepIsApplied() {
    Pipeline pipeline;
    pipeline.addStep(std::make_shared<MirrorStep>());

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::red);
    input.setPixelColor(0, 0, Qt::blue);

    const QImage output = pipeline.run(input);

    QCOMPARE(output.pixelColor(3, 0), QColor(Qt::blue));
    QCOMPARE(pipeline.stepCount(), size_t(1));
}

void TestPipeline::stepsRunInOrder() {
    Pipeline pipeline;
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("A")));
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("B")));

    QImage input(2, 2, QImage::Format_ARGB32);
    const QImage output = pipeline.run(input);

    QCOMPARE(output.text(QStringLiteral("tag")), QStringLiteral("AB"));
}

void TestPipeline::batchRunsEachInputIndependently() {
    Pipeline pipeline;
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("X")));

    const QImage a(2, 2, QImage::Format_ARGB32);
    const QImage b(2, 2, QImage::Format_ARGB32);

    const auto outputs = pipeline.runBatch({a, b});

    QCOMPARE(outputs.size(), size_t(2));
    QCOMPARE(outputs[0].text(QStringLiteral("tag")), QStringLiteral("X"));
    QCOMPARE(outputs[1].text(QStringLiteral("tag")), QStringLiteral("X"));
}

void TestPipeline::stepsAreEnabledByDefault() {
    Pipeline pipeline;
    pipeline.addStep(std::make_shared<MirrorStep>());

    QVERIFY(pipeline.isStepEnabled(0));
    QCOMPARE(pipeline.stepName(0), QStringLiteral("Mirror"));
}

void TestPipeline::disabledStepIsSkippedButOthersStillRun() {
    Pipeline pipeline;
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("A")));
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("B")));

    pipeline.setStepEnabled(0, false);

    QImage input(2, 2, QImage::Format_ARGB32);
    const QImage output = pipeline.run(input);

    QCOMPARE(output.text(QStringLiteral("tag")), QStringLiteral("B"));
    QVERIFY(!pipeline.isStepEnabled(0));
    QVERIFY(pipeline.isStepEnabled(1));
}

void TestPipeline::customOrderRunsOnlyListedStepsInGivenOrder() {
    Pipeline pipeline;
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("A")));
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("B")));
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("C")));

    QImage input(2, 2, QImage::Format_ARGB32);
    // Reversed order, and step 1 ("B") left out entirely.
    const QImage output = pipeline.run(input, {2, 0});

    QCOMPARE(output.text(QStringLiteral("tag")), QStringLiteral("CA"));
}

void TestPipeline::customOrderIgnoresEnabledFlag() {
    Pipeline pipeline;
    pipeline.addStep(std::make_shared<TagStep>(QStringLiteral("A")));
    pipeline.setStepEnabled(0, false);

    QImage input(2, 2, QImage::Format_ARGB32);
    // Explicitly listed, so it runs even though disabled: the caller already
    // decided inclusion by what it put in the order list.
    const QImage output = pipeline.run(input, {0});

    QCOMPARE(output.text(QStringLiteral("tag")), QStringLiteral("A"));
}

QTEST_MAIN(TestPipeline)
#include "test_pipeline.moc"
