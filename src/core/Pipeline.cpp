#include "Pipeline.h"

void Pipeline::addStep(std::shared_ptr<PipelineStep> step) {
    steps_.push_back(std::move(step));
}

QImage Pipeline::run(const QImage& input) const {
    QImage current = input;
    for (const auto& step : steps_) {
        current = step->process(current);
    }
    return current;
}

std::vector<QImage> Pipeline::runBatch(const std::vector<QImage>& inputs) const {
    std::vector<QImage> outputs;
    outputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        outputs.push_back(run(input));
    }
    return outputs;
}

size_t Pipeline::stepCount() const {
    return steps_.size();
}
