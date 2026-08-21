#pragma once

#include "UpscaleModel.h"

#include <QString>

#include <visp/ml.h>
#include <visp/vision.h>

#include <optional>

// vision.cpp (ggml-based, MIT, CPU + Vulkan) adapter for the UpscaleModel
// seam — runs a Real-ESRGAN-family GGUF model (see esrgan_load_model: plain
// ESRGAN/Real-ESRGAN variants only, not the "plus"/pixel-shuffle family).
class VisionCppUpscaleModel : public UpscaleModel {
public:
    explicit VisionCppUpscaleModel(QString modelPath);

    bool isReady() const override { return ready_; }
    QImage upscale(const QImage& input) const override;

private:
    QString modelPath_;
    // Optional so a backend_init() failure (e.g. no usable device at all)
    // can be caught and leave the object in a valid "not ready" state,
    // rather than escaping the constructor as an uncaught exception.
    std::optional<visp::backend_device> backend_;
    mutable visp::esrgan_model model_;
    bool ready_ = false;
};
