#pragma once

#include "PipelineStep.h"
#include "SegmentationModel.h"

#include <memory>

// Mask-only depth-of-field: blurs everything the mask calls background,
// keeps the foreground sharp, and lets the mask's own soft edge (the same
// SegmentationModel already used for BackgroundRemovalStep) feather the
// transition — no separate depth model for this first cut.
class BokehStep : public PipelineStep {
public:
    explicit BokehStep(std::shared_ptr<SegmentationModel> model, int blurRadius = 20);

    QImage process(const QImage& input) const override;
    QString name() const override { return QStringLiteral("Bokeh"); }

    // False if the underlying model failed to load. The caller decides what
    // to do about it (e.g. not adding this step to the pipeline at all).
    bool isReady() const { return model_ && model_->isReady(); }

private:
    std::shared_ptr<SegmentationModel> model_;
    int blurRadius_;
};
