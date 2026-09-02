#pragma once

#include "DenoiseModel.h"
#include "PipelineStep.h"

#include <memory>

// Model-agnostic: denoises RGB via the model and copies the input's own alpha
// channel verbatim (denoising preserves resolution, so unlike UpscaleStep
// there is nothing to rescale).
class DenoiseStep : public PipelineStep {
public:
    using PipelineStep::process; // keep the stateless 1-arg overload visible alongside the 2-arg override.
    explicit DenoiseStep(std::shared_ptr<DenoiseModel> model);

    QImage process(const QImage& input, PipelineRun& /*run*/) const override;
    QString name() const override { return QStringLiteral("Denoise"); }

    // False if the underlying model failed to load.
    bool isReady() const { return model_ && model_->isReady(); }

    // The caller loads the new model off the GUI thread first.
    void setModel(std::shared_ptr<DenoiseModel> model) { model_ = std::move(model); }

private:
    std::shared_ptr<DenoiseModel> model_;
};
