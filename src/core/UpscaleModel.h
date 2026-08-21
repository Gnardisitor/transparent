#pragma once

#include <QImage>

// The seam between an UpscaleStep (which only knows how to rebuild the alpha
// channel around a resized RGB image) and whatever inference runtime actually
// performs the upscale, mirroring SegmentationModel's role for background
// removal.
class UpscaleModel {
public:
    virtual ~UpscaleModel() = default;

    // False if the model failed to load. Callers decide what to do about it
    // (e.g. not adding the owning PipelineStep to the pipeline at all).
    virtual bool isReady() const = 0;

    // Upscales `input`'s RGB channels by the model's fixed scale factor
    // (applied to both dimensions), returned as Format_RGBA8888 with alpha
    // forced fully opaque. Returns a null QImage if inference fails.
    virtual QImage upscale(const QImage& input) const = 0;
};
