#pragma once

#include <QImage>
#include <QString>

// A single image-in, image-out operation.
class PipelineStep {
public:
    virtual ~PipelineStep() = default;

    virtual QImage process(const QImage& input) const = 0;
    virtual QString name() const = 0;
};
