#include "Pipeline.h"

void Pipeline::addStep(std::shared_ptr<PipelineStep> step) {
    steps_.push_back({std::move(step), true});
}

QImage Pipeline::run(const QImage& input) const {
    QImage current = input;
    for (const auto& entry : steps_) {
        if (entry.enabled) {
            current = entry.step->process(current);
        }
    }
    return current;
}

QImage Pipeline::run(const QImage& input, const std::vector<size_t>& stepOrder) const {
    QImage current = input;
    for (size_t index : stepOrder) {
        current = steps_.at(index).step->process(current);
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

QString Pipeline::stepName(size_t index) const {
    return steps_.at(index).step->name();
}

bool Pipeline::isStepEnabled(size_t index) const {
    return steps_.at(index).enabled;
}

void Pipeline::setStepEnabled(size_t index, bool enabled) {
    steps_.at(index).enabled = enabled;
}
