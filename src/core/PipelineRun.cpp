#include "PipelineRun.h"

#include "SegmentationModel.h"

QImage PipelineRun::maskFor(const QImage& source, const SegmentationModel& model) {
    if (mask_.size() != source.size()) {
        const QImage computed = model.computeMask(source);
        // Cache only masks that match the source they describe; a wrong-
        // sized mask is an inference failure and stays unshared, so the
        // next step retries against its own input.
        if (computed.size() == source.size()) {
            mask_ = computed;
        }
        return computed;
    }
    return mask_;
}
