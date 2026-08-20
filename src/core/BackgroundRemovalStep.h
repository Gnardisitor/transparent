#pragma once

#include "PipelineStep.h"
#include "SegmentationModel.h"

#include <memory>

// Model-agnostic: turns whatever mask a SegmentationModel produces into an
// alpha-composited image. Every ncnn/vision.cpp/etc.-specific detail (input
// size, normalization, blob names, resizing) lives behind that seam, not here.
class BackgroundRemovalStep : public PipelineStep {
public:
    explicit BackgroundRemovalStep(std::shared_ptr<SegmentationModel> model);

    QImage process(const QImage& input) const override;
    QString name() const override { return QStringLiteral("Background Removal"); }

    // False if the underlying model failed to load. The caller decides what
    // to do about it (e.g. not adding this step to the pipeline at all).
    bool isReady() const { return model_ && model_->isReady(); }

private:
    std::shared_ptr<SegmentationModel> model_;
};
