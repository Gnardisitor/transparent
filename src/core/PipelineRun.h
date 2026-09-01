#pragma once

#include <QImage>

class SegmentationModel;

// Per-run state shared by the steps of one pipeline->run() call. Steps ask
// here for the subject mask instead of running inference themselves, so
// Background Removal and Bokeh (which share one segmentation model) infer
// the mask once per image, not once per step. Never shared across images.
class PipelineRun {
public:
    // The run's subject mask for `source`: computed by `model` on the first
    // request, reused while the size fits, recomputed when it doesn't (the
    // upscaler ran in between). Null if inference fails.
    QImage maskFor(const QImage& source, const SegmentationModel& model);

private:
    QImage mask_;
};
