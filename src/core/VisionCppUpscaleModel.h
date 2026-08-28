#pragma once

#include "UpscaleModel.h"

#include <QString>

#include <visp/ml.h>
#include <visp/vision.h>

#include <optional>

// vision.cpp (ggml-based, MIT) adapter for the UpscaleModel seam, running
// Real-ESRGAN-family GGUFs (plain variants only, not "plus"/pixel-shuffle).
class VisionCppUpscaleModel : public UpscaleModel {
public:
    explicit VisionCppUpscaleModel(QString modelPath);

    bool isReady() const override { return ready_; }
    QImage upscale(const QImage& input) const override;

private:
    QString modelPath_;
    // Optional so a backend_init() failure leaves a valid not-ready object
    // instead of escaping the constructor as an exception.
    std::optional<visp::backend_device> backend_;
    mutable visp::esrgan_model model_;
    bool ready_ = false;
};
