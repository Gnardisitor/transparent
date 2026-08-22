#pragma once

#include "core/BatchRunner.h"
#include "core/GifIO.h"
#include "core/Pipeline.h"

#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QTimer;
class SpinnerWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::shared_ptr<Pipeline> pipeline, QWidget* parent = nullptr);

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
    BatchRunner batchRunner_;
    QLabel* statusLabel_;
    QComboBox* modeCombo_;
    QComboBox* operationCombo_;
    QWidget* advancedPage_;
    QListWidget* advancedStepList_;
    QPushButton* startProcessingButton_;
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
