#pragma once

#include "PipelineRun.h"

#include <QImage>
#include <QString>

// A single image-in, image-out operation.
class PipelineStep {
public:
    virtual ~PipelineStep() = default;

    // `run` carries state shared across the steps of one pipeline run
    // (currently the subject mask) so steps that need the same mask
    // compute it once per image instead of once per step.
    virtual QImage process(const QImage& input, PipelineRun& run) const = 0;

    // Convenience for callers that don't have a run: stateless single-step
    // processing, with no mask shared across steps.
    QImage process(const QImage& input) const {
        PipelineRun run;
        return process(input, run);
    }

    virtual QString name() const = 0;
};
