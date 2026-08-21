#pragma once

#include "core/BatchRunner.h"
#include "core/Pipeline.h"

#include <QImage>
#include <QMainWindow>

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::shared_ptr<Pipeline> pipeline, QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onExportClicked();
    void onModeChanged(int index);
    void onStartProcessingClicked();

private:
    // Simple mode: a single dropdown (top row) picks the one operation to
    // run, always live-reprocessing in the pipeline's own fixed order.
    // Advanced mode: a checkable, drag-to-reorder list of the same steps
    // plus an explicit "Start Processing" action, so the user picks
    // selection, order, and timing themselves.
    QWidget* buildSimplePage();
    QWidget* buildAdvancedPage();
    bool isAdvancedMode() const;
    std::vector<size_t> currentAdvancedStepOrder() const;
    void populateAdvancedStepList();

    void loadImage(const QString& path);
    void runBatch(const QString& folderPath);
    void reprocess();
    void updatePreview();

    std::shared_ptr<Pipeline> pipeline_;
    BatchRunner batchRunner_;
    QLabel* statusLabel_;
    QComboBox* modeCombo_;
    QComboBox* operationCombo_;
    QStackedWidget* modeStack_;
    QListWidget* advancedStepList_;
    QLabel* previewLabel_;
    QPushButton* exportButton_;
    QImage sourceImage_;
    QImage resultImage_;
};
