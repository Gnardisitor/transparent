#pragma once

#include "PipelineStep.h"

#include <memory>
#include <vector>

// Runs an ordered sequence of PipelineSteps over an image (or a batch of
// images). Empty pipeline is a valid no-op state.
class Pipeline {
public:
    void addStep(std::shared_ptr<PipelineStep> step);

    QImage run(const QImage& input) const;
    std::vector<QImage> runBatch(const std::vector<QImage>& inputs) const;

    size_t stepCount() const;

private:
    std::vector<std::shared_ptr<PipelineStep>> steps_;
};
