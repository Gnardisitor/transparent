#pragma once

#include "SegmentationModel.h"

#include <QString>

#include <visp/ml.h>
#include <visp/vision.h>

#include <optional>

// vision.cpp (ggml-based, MIT, CPU + Vulkan) adapter for the
// SegmentationModel seam — runs BiRefNet-lite. Replaces the earlier
// ncnn/IS-Net path: see models/source/LICENSE_NOTICE.md for why (ncnn's
// conversion tooling can't translate BiRefNet's transformer backbone or its
// deformable-conv decoder block; vision.cpp implements both natively).
class VisionCppSegmentationModel : public SegmentationModel {
public:
    explicit VisionCppSegmentationModel(QString modelPath);

    bool isReady() const override { return ready_; }
    QImage computeMask(const QImage& input) const override;

private:
    QString modelPath_;
    // Optional so a backend_init() failure (e.g. no usable device at all)
    // can be caught and leave the object in a valid "not ready" state,
    // rather than escaping the constructor as an uncaught exception.
    std::optional<visp::backend_device> backend_;
    mutable visp::birefnet_model model_;
    bool ready_ = false;
};
