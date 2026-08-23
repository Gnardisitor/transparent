#pragma once

#include "core/BatchRunner.h"
#include "core/GifIO.h"
#include "core/ModelCatalog.h"
#include "core/Pipeline.h"

#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>

#include <memory>
#include <vector>

class BackgroundRemovalStep;
class BokehStep;
class ModelManager;
class QComboBox;
class QDialog;
class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QTimer;
class SegmentationModel;
class SettingsPage;
class SpinnerWidget;
class UpscaleModel;
class UpscaleStep;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::shared_ptr<Pipeline> pipeline,
                         std::shared_ptr<ModelManager> modelManager = nullptr,
                         QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onExportClicked();
    void onModeChanged(int index);
    void onStartProcessingClicked();
    void onClearImageClicked();
    void onProcessingFinished();
    void onGifProcessingFinished();
    // Cycles resultGifFrames_ through previewLabel_ at each frame's own
    // delay so an animated GIF result can actually be previewed before
    // export, not just its first frame. Re-arms itself (gifPreviewTimer_ is
    // single-shot) with the newly-current frame's delay each time, since
    // GIF frames don't share one fixed interval.
    void advanceGifPreviewFrame();
    // Settings dialog asked to switch to a different curated model. Loads it
    // off the GUI thread (construction does disk I/O + GPU pipeline setup)
    // and, once ready, swaps it live into the relevant Pipeline step(s) —
    // no app restart needed. See PLAN.md's Model management section.
    void onModelSelected(ModelCategory category, QString filename);
    void onSegmentationModelLoaded();
    void onUpscaleModelLoaded();
    // Settings dialog's bokeh-strength slider moved. Always persists to
    // QSettings; live-previews (cheap CPU reblend of the last computed
    // mask, no model rerun) only when Bokeh's own output is what's
    // currently on screen — see isBokehTheActiveOutputStep().
    void onBokehStrengthChanged(int percent);
    void onBokehPreviewReady();

private:
    // Simple mode: a single dropdown (top row) picks the one operation to
    // run, always live-reprocessing in the pipeline's own fixed order — it
    // has no page body of its own. Advanced mode: a checkable, drag-to-
    // reorder list of the same steps plus an explicit "Start Processing"
    // action, so the user picks selection, order, and timing themselves.
    // Shown/hidden directly (not QStackedWidget) so Simple mode's empty page
    // doesn't reserve Advanced's larger page's height when it's not shown.
    QWidget* buildAdvancedPage();
    bool isAdvancedMode() const;
    std::vector<size_t> currentAdvancedStepOrder() const;
    void populateAdvancedStepList();

    // Finds the first pipeline_ step whose declared name() matches and
    // downcasts it to T. Returns nullptr if pipeline_ has no such step, or
    // (e.g. in tests using fake PipelineStep subclasses) it isn't actually
    // that concrete type — model swapping then simply has nothing to act
    // on. Defined in the .cpp (not inline here) since every call site lives
    // there too.
    template <typename T>
    std::shared_ptr<T> findStepByName(const QString& name) const;

    std::shared_ptr<BackgroundRemovalStep> backgroundRemovalStep() const;
    std::shared_ptr<BokehStep> bokehStep() const;
    std::shared_ptr<UpscaleStep> upscaleStep() const;

    // True if Bokeh is the last step in whatever's currently active (the
    // one enabled step in Simple mode, or the last entry of
    // currentAdvancedStepOrder() in Advanced mode) — i.e. resultImage_ is
    // actually Bokeh's own output, so a live strength-only reblend of it is
    // valid to show. False (not an error, just "nothing to live-preview")
    // whenever Bokeh isn't active, or something else runs after it.
    bool isBokehTheActiveOutputStep() const;

    void loadImage(const QString& path);
    void runBatch(const QString& folderPath);
    // Runs the pipeline on sourceImage_ (or, for an animated GIF source,
    // sourceGifFrames_ one frame at a time) on a background thread
    // (QtConcurrent) so heavy inference never blocks the GUI event
    // loop/repainting — a synchronous call here used to freeze the whole
    // window for the duration of the model run. See onProcessingFinished()/
    // onGifProcessingFinished() for the completion side.
    void reprocess();
    void updatePreview();
    void repositionOverlays();
    void setControlsEnabled(bool enabled);
    void startGifPreviewAnimation();
    void stopGifPreviewAnimation();

    std::shared_ptr<Pipeline> pipeline_;
    std::shared_ptr<ModelManager> modelManager_;
    BatchRunner batchRunner_;
    QLabel* statusLabel_;
    QComboBox* modeCombo_;
    QComboBox* operationCombo_;
    QWidget* advancedPage_;
    QListWidget* advancedStepList_;
    QPushButton* startProcessingButton_;
    // Settings lives in its own top-level QDialog (opened from a menu-bar
    // action next to Help), not a third mode/page in `central` — see the
    // comment at settingsAction's connect() in the constructor.
    SettingsPage* settingsPage_;
    QDialog* settingsDialog_;
    // Only one at a time: SettingsPage disables every control (setBusy) as
    // soon as either fires, so a second selection can't queue up before the
    // first lands. Separate watchers because the two categories load
    // different model types.
    QFutureWatcher<std::shared_ptr<SegmentationModel>> segmentationModelWatcher_;
    QFutureWatcher<std::shared_ptr<UpscaleModel>> upscaleModelWatcher_;
    bool modelLoading_ = false;
    // Which filename each in-flight watcher above is loading, so the
    // finished handler knows what to persist to QSettings and reflect back
    // in settingsPage_ once the load completes (the watcher's result() is
    // just the loaded model, not the filename it came from).
    QString pendingSegmentationFilename_;
    QString pendingUpscaleFilename_;
    // Dedicated watcher for the bokeh live-preview reblend (cheap CPU work,
    // not model inference) — separate from processingWatcher_/the model
    // watchers above so a slider drag never competes with either for the
    // same QFutureWatcher.
    QFutureWatcher<QImage> bokehPreviewWatcher_;
    // True between dispatching a bokeh live-preview task and its finished
    // signal. reblendCached() reads BokehStep's cached mask on that task's
    // worker thread; reprocess() (via process()) and a model swap (via
    // setModel()) both write those same cached members on the GUI thread —
    // this flag keeps the two from ever overlapping, same reasoning as
    // modelLoading_ above.
    bool bokehPreviewInFlight_ = false;
    QLabel* previewLabel_;
    QPushButton* clearButton_;
    SpinnerWidget* spinner_;
    QPushButton* exportButton_;
    QImage sourceImage_;
    QImage resultImage_;
    QFutureWatcher<QImage> processingWatcher_;
    // Set instead of sourceImage_/resultImage_/processingWatcher_ when the
    // dropped file is an animated GIF (GifIO::isAnimated); sourceImage_ and
    // resultImage_ still track that case's current frame so the rest of
    // MainWindow (export enablement, preview painting) doesn't need a
    // parallel "is this a GIF" check everywhere.
    bool isAnimatedGifSource_ = false;
    std::vector<GifIO::Frame> sourceGifFrames_;
    std::vector<GifIO::Frame> resultGifFrames_;
    QFutureWatcher<std::vector<GifIO::Frame>> gifProcessingWatcher_;
    QTimer* gifPreviewTimer_;
    int gifPreviewFrameIndex_ = 0;
    bool processing_ = false;
};
