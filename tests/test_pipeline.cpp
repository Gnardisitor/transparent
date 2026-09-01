#include <QtTest>

#include "core/BackgroundRemovalStep.h"
#include "core/BokehStep.h"
#include "core/Pipeline.h"
#include "core/PipelineRun.h"
#include "core/SegmentationModel.h"
#include "core/UpscaleModel.h"
#include "core/UpscaleStep.h"

namespace {

// Deterministic, GPU-free fakes standing in for real steps (background
// removal, upscaling, ...) so the pipeline's sequencing logic can be tested
// on its own.

class MirrorStep : public PipelineStep {
public:
    QImage process(const QImage& input, PipelineRun& /*run*/) const override {
        return input.mirrored(true, false);
    }
    QString name() const override { return QStringLiteral("Mirror"); }
};

class TagStep : public PipelineStep {
public:
    explicit TagStep(QString tag) : tag_(std::move(tag)) {}

    QImage process(const QImage& input, PipelineRun& /*run*/) const override {
        QImage out = input;
        out.setText(QStringLiteral("tag"), out.text(QStringLiteral("tag")) + tag_);
        return out;
    }
    QString name() const override { return QStringLiteral("Tag(%1)").arg(tag_); }

private:
    QString tag_;
};

// Counts computeMask() calls and records the size of each request, so the
// mask-sharing tests can assert Background Removal and Bokeh run inference
// once per image instead of once per step.
class FakeSegmentationModel : public SegmentationModel {
public:
    QImage maskToReturn;
    mutable int computeMaskCallCount = 0;
    mutable QList<QSize> maskRequestSizes;

    bool isReady() const override { return true; }
    QImage computeMask(const QImage& input) const override {
        ++computeMaskCallCount;
        maskRequestSizes.append(input.size());
        return maskToReturn;
    }
};

// Upscales 2x so the size rule of PipelineRun can be exercised: a stored
// mask no longer fits after an upscaler runs in between, forcing a
// recompute.
class FakeUpscaleModel : public UpscaleModel {
public:
    bool isReady() const override { return true; }
    QImage upscale(const QImage& input) const override {
        return input.scaled(input.width() * 2, input.height() * 2, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    }
};

QImage solidMask(const QSize& size, uchar value) {
    QImage mask(size, QImage::Format_Alpha8);
    mask.fill(value);
    return mask;
}

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
    void maskComputedOnceAcrossBackgroundRemovalAndBokeh();
    void maskSharedRegardlessOfStepOrder();
    void maskRecomputedWhenStepSizesDiverge();
    void eachBatchInputGetsItsOwnMask();
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

void TestPipeline::maskComputedOnceAcrossBackgroundRemovalAndBokeh() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(QSize(4, 4), 255);

    Pipeline pipeline;
    pipeline.addStep(std::make_shared<BackgroundRemovalStep>(model));
    pipeline.addStep(std::make_shared<BokehStep>(model));

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::red);
    const QImage output = pipeline.run(input);

    // Both steps need the subject mask; it must be inferred once, not
    // once per step. Before PipelineRun, Bokeh re-ran BiRefNet on
    // Background Removal's already-cut output, doubling the inference for
    // the most common Advanced combo.
    QCOMPARE(model->computeMaskCallCount, 1);
    QCOMPARE(model->maskRequestSizes.size(), 1);
    QCOMPARE(model->maskRequestSizes.first(), QSize(4, 4));
    // Background Removal's cutout actually reached Bokeh: the output has
    // the mask's alpha applied.
    QCOMPARE(output.pixelColor(0, 0).alpha(), 255);
}

void TestPipeline::maskSharedRegardlessOfStepOrder() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(QSize(4, 4), 255);

    Pipeline pipeline;
    pipeline.addStep(std::make_shared<BokehStep>(model));
    pipeline.addStep(std::make_shared<BackgroundRemovalStep>(model));

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::blue);
    pipeline.run(input);

    // Bokeh ran first and computed the mask; Background Removal reuses it.
    QCOMPARE(model->computeMaskCallCount, 1);
}

void TestPipeline::maskRecomputedWhenStepSizesDiverge() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(QSize(8, 8), 255);

    Pipeline pipeline;
    pipeline.addStep(std::make_shared<BackgroundRemovalStep>(model));
    pipeline.addStep(std::make_shared<UpscaleStep>(std::make_shared<FakeUpscaleModel>()));
    pipeline.addStep(std::make_shared<BokehStep>(model));

    QImage input(4, 4, QImage::Format_ARGB32);
    input.fill(Qt::red);
    pipeline.run(input);

    // The stored 4x4 mask no longer fits Bokeh's 8x8 input after the
    // upscaler, so it must be recomputed at the new size, not blindly
    // reused.
    QCOMPARE(model->computeMaskCallCount, 2);
    QCOMPARE(model->maskRequestSizes, QList<QSize>({QSize(4, 4), QSize(8, 8)}));
}

void TestPipeline::eachBatchInputGetsItsOwnMask() {
    auto model = std::make_shared<FakeSegmentationModel>();
    model->maskToReturn = solidMask(QSize(2, 2), 255);

    Pipeline pipeline;
    pipeline.addStep(std::make_shared<BackgroundRemovalStep>(model));
    pipeline.addStep(std::make_shared<BokehStep>(model));

    const QImage a(2, 2, QImage::Format_ARGB32);
    const QImage b(2, 2, QImage::Format_ARGB32);
    pipeline.runBatch({a, b});

    // A fresh run per image: two images, two mask computations. The run's
    // mask must never leak from one image into the next.
    QCOMPARE(model->computeMaskCallCount, 2);
}

QTEST_MAIN(TestPipeline)
#include "test_pipeline.moc"
