#pragma once

#include "core/BatchRunner.h"
#include "core/Pipeline.h"

#include <QImage>
#include <QMainWindow>

#include <memory>

class QLabel;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::shared_ptr<Pipeline> pipeline, QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onExportClicked();

private:
    void loadImage(const QString& path);
    void runBatch(const QString& folderPath);
    void updatePreview();

    std::shared_ptr<Pipeline> pipeline_;
    BatchRunner batchRunner_;
    QLabel* statusLabel_;
    QLabel* previewLabel_;
    QPushButton* exportButton_;
    QImage resultImage_;
};
