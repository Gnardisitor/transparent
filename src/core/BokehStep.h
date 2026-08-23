#pragma once

#include "PipelineStep.h"
#include "SegmentationModel.h"

#include <memory>

// Mask-only depth-of-field: blurs everything the mask calls background,
// keeps the foreground sharp, and lets the mask's own soft edge (the same
// SegmentationModel already used for BackgroundRemovalStep) feather the
// transition — no separate depth model for this first cut.
class BokehStep : public PipelineStep {
public:
    // No strong opinion behind 50 beyond "middle of the slider is a sane
    // default" — see SettingsPage for the actual UI.
    static constexpr int kDefaultStrengthPercent = 50;

    // QSettings key the persisted strength lives under (main.cpp reads it,
    // MainWindow writes it) — one source of truth instead of a duplicated
    // string literal in both places.
    static QString settingsKey();

    explicit BokehStep(std::shared_ptr<SegmentationModel> model,
                        int strengthPercent = kDefaultStrengthPercent);

    QImage process(const QImage& input) const override;
    QString name() const override { return QStringLiteral("Bokeh"); }

    // False if the underlying model failed to load. The caller decides what
    // to do about it (e.g. not adding this step to the pipeline at all).
    bool isReady() const { return model_ && model_->isReady(); }

    // Swaps in a newly loaded model. Bokeh always mirrors whichever
    // segmentation model Background Removal is set to (see PLAN.md's Model
    // management section), so callers update both steps together. Also
    // invalidates the process()/reblendCached() cache below, since a
    // different model means a different mask.
    void setModel(std::shared_ptr<SegmentationModel> model);

    // 0-100, clamped. Takes effect on the *next* process() call (or
    // reblendCached() call, for a live-preview reblend using the cache from
    // the last process() run) — setting it alone doesn't recompute or
    // re-render anything by itself.
    void setStrengthPercent(int percent);
    int strengthPercent() const { return strengthPercent_; }

    // True once process() has run at least once since construction or the
    // last setModel(), meaning reblendCached() has something to reuse.
    bool hasCachedMask() const { return !cachedMask_.isNull(); }

    // Re-runs just the blur+composite over the input/mask that the last
    // process() call cached, at `strengthPercent` (independent of
    // strengthPercent_ — a caller driving a live slider passes the in-flight
    // value explicitly rather than racing setStrengthPercent() against this
    // running on a worker thread). No model_->computeMask() call, so this
    // is cheap CPU-only work — safe to call on every slider tick, including
    // from a background thread (see MainWindow's Model management /
    // "Bokeh strength control" design in PLAN.md). Null QImage if
    // hasCachedMask() is false.
    QImage reblendCached(int strengthPercent) const;

private:
    // Shared by process() and reblendCached(): blurs `input` by `radius`
    // and composites back to the sharp original by `mask` weight. Pure
    // function of its arguments — no model call, no cache read/write.
    QImage blend(const QImage& input, const QImage& mask, int radius) const;

    // Scales `strengthPercent` (0-100) to a blur radius relative to
    // `image`'s own shorter side, so the effect reads the same on a
    // thumbnail and a full-resolution photo rather than a fixed pixel
    // radius that's barely visible on one and extreme on the other. At
    // 100%, the radius is 5% of the shorter side.
    int radiusForImage(const QImage& image, int strengthPercent) const;

    std::shared_ptr<SegmentationModel> model_;
    int strengthPercent_;
    // Written by process(), read by reblendCached(); both mutable since
    // PipelineStep::process() is const. Guarded against concurrent
    // process()/reblendCached() calls by the caller (MainWindow only ever
    // calls reblendCached() while !processing_, i.e. while no process()
    // call is in flight on another thread).
    mutable QImage cachedInput_;
    mutable QImage cachedMask_;
};
