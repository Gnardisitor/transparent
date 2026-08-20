#pragma once

#include <QImage>

// The seam between a PipelineStep (which only knows how to composite a mask
// into alpha) and whatever inference runtime actually produces that mask.
// ncnn, vision.cpp, or anything else can sit behind this without
// BackgroundRemovalStep knowing which one it is.
class SegmentationModel {
public:
    virtual ~SegmentationModel() = default;

    // False if the model failed to load. Callers decide what to do about it
    // (e.g. not adding the owning PipelineStep to the pipeline at all).
    virtual bool isReady() const = 0;

    // Returns a single-channel mask the same size as `input`, where 0 is
    // fully background and 255 is fully foreground (QImage::Format_Alpha8).
    // Returns a null QImage if inference fails.
    virtual QImage computeMask(const QImage& input) const = 0;
};
