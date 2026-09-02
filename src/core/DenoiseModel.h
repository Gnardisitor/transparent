#pragma once

#include <QImage>

// The seam between DenoiseStep and whatever runtime performs the denoising.
class DenoiseModel {
public:
    virtual ~DenoiseModel() = default;

    // False if the model failed to load.
    virtual bool isReady() const = 0;

    // Denoises `input`'s RGB channels in place (output has the same size as the
    // input), returned as Format_RGBA8888 with alpha forced fully opaque.
    // Returns a null QImage if inference fails.
    virtual QImage denoise(const QImage& input) const = 0;
};
