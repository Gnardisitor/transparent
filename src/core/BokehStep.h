#pragma once

#include "PipelineStep.h"
#include "SegmentationModel.h"

#include <memory>

// Mask-only bokeh: blurs what the mask marks as background, feathered by
// the mask's own soft edge. No separate depth model.
class BokehStep : public PipelineStep {
public:
    static constexpr int kDefaultStrengthPercent = 50;

    // QSettings key for the persisted strength (read in main.cpp, written
    // in MainWindow).
    static QString settingsKey();

    explicit BokehStep(std::shared_ptr<SegmentationModel> model,
                        int strengthPercent = kDefaultStrengthPercent);

    QImage process(const QImage& input) const override;
    QString name() const override { return QStringLiteral("Bokeh"); }

    // False if the underlying model failed to load.
    bool isReady() const { return model_ && model_->isReady(); }

    // Swaps in a new model and invalidates the mask cache. Bokeh mirrors
    // Background Removal's model, so callers update both steps together.
    void setModel(std::shared_ptr<SegmentationModel> model);

    // 0-100, clamped. Takes effect at the next process()/reblendCached()
    // call.
    void setStrengthPercent(int percent);
    int strengthPercent() const { return strengthPercent_; }

    // True once process() has run, so reblendCached() has data to reuse.
    bool hasCachedMask() const { return !cachedMask_.isNull(); }

    // Re-runs blur+composite over the input/mask cached by the last
    // process(), at `strengthPercent`. No model call, so cheap enough for
    // every slider tick. The value is passed explicitly because this runs
    // on a worker thread. Null if hasCachedMask() is false.
    QImage reblendCached(int strengthPercent) const;

private:
    // Blurs `input` by `radius` and composites back to the sharp original
    // by `mask` weight. Pure function of its arguments.
    QImage blend(const QImage& input, const QImage& mask, int radius) const;

    // Scales strengthPercent (0-100) to a blur radius relative to `image`'s
    // shorter side (5% at 100%).
    int radiusForImage(const QImage& image, int strengthPercent) const;

    std::shared_ptr<SegmentationModel> model_;
    int strengthPercent_;
    // Written by process(), read by reblendCached(); mutable since
    // process() is const. The caller guarantees no concurrent
    // process()/reblendCached() calls.
    mutable QImage cachedInput_;
    mutable QImage cachedMask_;
};
