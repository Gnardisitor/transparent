#pragma once

#include "SegmentationModel.h"

#include <QString>

#include <visp/ml.h>
#include <visp/vision.h>

#include <optional>

// vision.cpp (ggml-based, MIT) adapter for the SegmentationModel seam,
// running BiRefNet-lite. Replaces the earlier ncnn path, whose conversion
// tooling can't handle BiRefNet's transformer/deformable-conv ops.
class VisionCppSegmentationModel : public SegmentationModel {
public:
    explicit VisionCppSegmentationModel(QString modelPath);

    bool isReady() const override { return ready_; }
    QImage computeMask(const QImage& input) const override;

private:
    QString modelPath_;
    // Optional so a backend_init() failure leaves a valid not-ready object
    // instead of escaping the constructor as an exception.
    std::optional<visp::backend_device> backend_;
    mutable visp::birefnet_model model_;
    bool ready_ = false;
};
