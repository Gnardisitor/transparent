#pragma once

#include "DenoiseModel.h"

#include <QString>

#include <visp/ml.h>
#include <visp/vision.h>

#include <optional>

// vision.cpp (ggml-based, MIT) adapter for the DenoiseModel seam, running
// SCUNet GGUFs (Swin-Conv-UNet blind real-world color denoising). Large images
// are tiled inside visp's scunet_compute (full-res up to ~2.25MP, then 512px
// tiles with 32px overlap).
class VisionCppDenoiseModel : public DenoiseModel {
public:
    explicit VisionCppDenoiseModel(QString modelPath);

    bool isReady() const override { return ready_; }
    QImage denoise(const QImage& input) const override;

private:
    QString modelPath_;
    // Optional so a backend_init() failure leaves a valid not-ready object
    // instead of escaping the constructor as an exception.
    std::optional<visp::backend_device> backend_;
    mutable visp::scunet_model model_;
    bool ready_ = false;
};
