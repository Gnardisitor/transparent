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
    // Cycles resultGifFrames_ through the preview, re-arming
    // gifPreviewTimer_ (single-shot) with each frame's own delay.
    void advanceGifPreviewFrame();
    // Loads the chosen model off the GUI thread and swaps it live into the
    // relevant Pipeline steps.
    void onModelSelected(ModelCategory category, QString filename);
    void onSegmentationModelLoaded();
    void onUpscaleModelLoaded();
    // Persists the strength to QSettings; live-previews via a cheap mask
    // reblend when Bokeh is the active output step.
    void onBokehStrengthChanged(int percent);
    void onBokehPreviewReady();

private:
    // Advanced mode's checkable, drag-reorderable step list. Shown/hidden
    // directly, not via QStackedWidget, which would reserve the larger
    // page's height even when hidden.
    QWidget* buildAdvancedPage();
    bool isAdvancedMode() const;
    std::vector<size_t> currentAdvancedStepOrder() const;
    void populateAdvancedStepList();

    // First pipeline_ step whose name() matches, downcast to T. Null if
    // absent or a different concrete type (fake steps in tests).
    template <typename T>
    std::shared_ptr<T> findStepByName(const QString& name) const;

    std::shared_ptr<BackgroundRemovalStep> backgroundRemovalStep() const;
    std::shared_ptr<BokehStep> bokehStep() const;
    std::shared_ptr<UpscaleStep> upscaleStep() const;

    // True if Bokeh is the last active step, i.e. resultImage_ is Bokeh's
    // own output and a live strength-only reblend is valid to show.
    bool isBokehTheActiveOutputStep() const;

    void loadImage(const QString& path);
    void runBatch(const QString& folderPath);
    // Runs the pipeline on the source (each GIF frame individually) on a
    // background thread so inference never blocks the GUI.
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
    SettingsPage* settingsPage_;
    QDialog* settingsDialog_;
    // One model load at a time: setBusy() blocks a second selection while
    // either is in flight. Separate watchers because the categories load
    // different model types.
    QFutureWatcher<std::shared_ptr<SegmentationModel>> segmentationModelWatcher_;
    QFutureWatcher<std::shared_ptr<UpscaleModel>> upscaleModelWatcher_;
    bool modelLoading_ = false;
    // Filename each in-flight load is fetching; the watcher result doesn't
    // carry it.
    QString pendingSegmentationFilename_;
    QString pendingUpscaleFilename_;
    // Dedicated watcher for the bokeh live-preview reblend, so a slider
    // drag never competes with processing or model loads.
    QFutureWatcher<QImage> bokehPreviewWatcher_;
    // True while the reblend task runs. Its worker reads BokehStep's cached
    // mask, which reprocess() and setModel() write on the GUI thread; this
    // flag keeps the two from overlapping.
    bool bokehPreviewInFlight_ = false;
    QLabel* previewLabel_;
    QPushButton* clearButton_;
    SpinnerWidget* spinner_;
    QPushButton* exportButton_;
    QImage sourceImage_;
    QImage resultImage_;
    QFutureWatcher<QImage> processingWatcher_;
    // Set when the dropped file is an animated GIF. sourceImage_ and
    // resultImage_ still track the current frame, so the rest of the class
    // needs no separate GIF check.
    bool isAnimatedGifSource_ = false;
    std::vector<GifIO::Frame> sourceGifFrames_;
    std::vector<GifIO::Frame> resultGifFrames_;
    QFutureWatcher<std::vector<GifIO::Frame>> gifProcessingWatcher_;
    QTimer* gifPreviewTimer_;
    int gifPreviewFrameIndex_ = 0;
    bool processing_ = false;
};
