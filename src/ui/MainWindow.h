#pragma once

#include "core/BatchRunner.h"
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
    // Runs the pipeline on sourceImage_ on a background thread (QtConcurrent)
    // so heavy inference never blocks the GUI event loop/repainting — a
    // synchronous call here used to freeze the whole window for the
    // duration of the model run. See onProcessingFinished() for the
    // completion side.
    void reprocess();
    void updatePreview();
    void repositionOverlays();
    void setControlsEnabled(bool enabled);

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
    bool processing_ = false;
};
