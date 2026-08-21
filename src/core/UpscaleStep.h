#pragma once

#include "PipelineStep.h"
#include "UpscaleModel.h"

#include <memory>

// Model-agnostic: hands RGB pixels to an UpscaleModel and rebuilds the alpha
// channel by scaling the input's own alpha up to match, since super-
// resolution models only ever operate on RGB. Keeping that recombination
// here (rather than inside the model) means a prior BackgroundRemovalStep's
// transparency survives regardless of which step runs first.
class UpscaleStep : public PipelineStep {
public:
    explicit UpscaleStep(std::shared_ptr<UpscaleModel> model);

    QImage process(const QImage& input) const override;
    QString name() const override { return QStringLiteral("Upscale"); }

    // False if the underlying model failed to load. The caller decides what
    // to do about it (e.g. not adding this step to the pipeline at all).
    bool isReady() const { return model_ && model_->isReady(); }

private:
    std::shared_ptr<UpscaleModel> model_;
};
