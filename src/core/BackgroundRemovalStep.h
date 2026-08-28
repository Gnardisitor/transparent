#pragma once

#include "PipelineStep.h"
#include "SegmentationModel.h"

#include <memory>

// Model-agnostic: composites a SegmentationModel's mask into an
// alpha image. Runtime-specific detail lives behind that seam.
class BackgroundRemovalStep : public PipelineStep {
public:
    explicit BackgroundRemovalStep(std::shared_ptr<SegmentationModel> model);

    QImage process(const QImage& input) const override;
    QString name() const override { return QStringLiteral("Background Removal"); }

    // False if the underlying model failed to load.
    bool isReady() const { return model_ && model_->isReady(); }

    // The caller loads the new model off the GUI thread first.
    void setModel(std::shared_ptr<SegmentationModel> model) { model_ = std::move(model); }

private:
    std::shared_ptr<SegmentationModel> model_;
};
