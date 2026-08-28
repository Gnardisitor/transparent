#pragma once

#include <QImage>

// The seam between a PipelineStep and whatever inference runtime produces
// the mask.
class SegmentationModel {
public:
    virtual ~SegmentationModel() = default;

    // False if the model failed to load.
    virtual bool isReady() const = 0;

    // Returns a single-channel mask the same size as `input`, where 0 is
    // fully background and 255 is fully foreground (QImage::Format_Alpha8).
    // Returns a null QImage if inference fails.
    virtual QImage computeMask(const QImage& input) const = 0;
};
