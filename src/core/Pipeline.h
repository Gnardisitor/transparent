#pragma once

#include "PipelineStep.h"

#include <memory>
#include <vector>

// Runs an ordered sequence of PipelineSteps over an image (or a batch of
// images). Empty pipeline is a valid no-op state. Steps are enabled by
// default; disabling one (e.g. from a UI checkbox letting the user pick
// which operations to apply, independently of each other) skips it without
// removing it from the sequence, so toggling doesn't need to rebuild the
// pipeline or touch whatever holds the shared_ptr to it.
class Pipeline {
public:
    void addStep(std::shared_ptr<PipelineStep> step);

    QImage run(const QImage& input) const;
    std::vector<QImage> runBatch(const std::vector<QImage>& inputs) const;

    // Runs exactly the given step indices, in the given order, regardless of
    // each step's enabled flag — the caller already decided inclusion by
    // what it put in stepOrder. Used by an Advanced-mode UI that lets the
    // user pick which steps run and in what sequence, independent of the
    // fixed declaration order run()/runBatch() use.
    QImage run(const QImage& input, const std::vector<size_t>& stepOrder) const;

    size_t stepCount() const;
    QString stepName(size_t index) const;
    bool isStepEnabled(size_t index) const;
    void setStepEnabled(size_t index, bool enabled);

    // The step object itself, type-erased same as addStep() took it.
    // Callers that need to act on a specific concrete step (e.g. swapping a
    // model into BackgroundRemovalStep from Settings) find it by name via
    // stepName()/stepCount() and dynamic_pointer_cast the result.
    std::shared_ptr<PipelineStep> stepAt(size_t index) const;

private:
    struct Entry {
        std::shared_ptr<PipelineStep> step;
        bool enabled = true;
    };
    std::vector<Entry> steps_;
};
