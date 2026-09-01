#pragma once

#include "PipelineStep.h"
#include "UpscaleModel.h"

#include <memory>

// Model-agnostic: upscales RGB via the model, then rescales the input's own
// alpha to match, so transparency from earlier steps survives.
class UpscaleStep : public PipelineStep {
public:
    using PipelineStep::process; // keep the stateless 1-arg overload visible alongside the 2-arg override.
    explicit UpscaleStep(std::shared_ptr<UpscaleModel> model);

    QImage process(const QImage& input, PipelineRun& /*run*/) const override;
    QString name() const override { return QStringLiteral("Upscale"); }

    // False if the underlying model failed to load.
    bool isReady() const { return model_ && model_->isReady(); }

    // The caller loads the new model off the GUI thread first.
    void setModel(std::shared_ptr<UpscaleModel> model) { model_ = std::move(model); }

private:
    std::shared_ptr<UpscaleModel> model_;
};
