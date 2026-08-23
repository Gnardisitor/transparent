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

    // Swaps in a newly loaded model (e.g. the user picked a different one in
    // Settings). The caller is responsible for loading it off the GUI
    // thread first — construction (disk read, backend init, GPU pipeline
    // setup) can take real time.
    void setModel(std::shared_ptr<SegmentationModel> model) { model_ = std::move(model); }

private:
    std::shared_ptr<SegmentationModel> model_;
};
