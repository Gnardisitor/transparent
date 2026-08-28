#pragma once

#include "PipelineStep.h"

#include <memory>
#include <vector>

// Runs an ordered sequence of PipelineSteps over an image. Empty is a
// valid no-op. Disabling a step skips it without removing it, so toggling
// doesn't rebuild the pipeline.
class Pipeline {
public:
    void addStep(std::shared_ptr<PipelineStep> step);

    QImage run(const QImage& input) const;
    std::vector<QImage> runBatch(const std::vector<QImage>& inputs) const;

    // Runs exactly the given step indices in order, ignoring enabled
    // flags. For an Advanced-mode UI's explicit selection.
    QImage run(const QImage& input, const std::vector<size_t>& stepOrder) const;

    size_t stepCount() const;
    QString stepName(size_t index) const;
    bool isStepEnabled(size_t index) const;
    void setStepEnabled(size_t index, bool enabled);

    // Type-erased step access, for callers that need a concrete step.
    std::shared_ptr<PipelineStep> stepAt(size_t index) const;

private:
    struct Entry {
        std::shared_ptr<PipelineStep> step;
        bool enabled = true;
    };
    std::vector<Entry> steps_;
};
