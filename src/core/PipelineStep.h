#pragma once

#include <QImage>
#include <QString>

// A single image-in, image-out operation. Background removal is the only
// step today; upscaling and per-frame video processing are meant to be added
// later as additional PipelineStep implementations without changing this
// interface or Pipeline itself.
class PipelineStep {
public:
    virtual ~PipelineStep() = default;

    virtual QImage process(const QImage& input) const = 0;
    virtual QString name() const = 0;
};
