#include "MainWindow.h"

#include "core/ImageFormats.h"
#include "ui/SpinnerWidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrentRun>

#include <optional>

namespace {

// Standard transparency checkerboard so the user can actually tell removed
// background apart from an opaque white/gray fill.
QPixmap checkerboardPattern(int cell = 12) {
    QPixmap pattern(cell * 2, cell * 2);
    QPainter painter(&pattern);
    painter.fillRect(0, 0, cell * 2, cell * 2, QColor(210, 210, 210));
    painter.fillRect(0, 0, cell, cell, QColor(160, 160, 160));
    painter.fillRect(cell, cell, cell, cell, QColor(160, 160, 160));
    return pattern;
}

} // namespace

MainWindow::MainWindow(std::shared_ptr<Pipeline> pipeline, QWidget* parent)
    : QMainWindow(parent), pipeline_(std::move(pipeline)), batchRunner_(pipeline_) {
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("transparent"));

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    QAction* aboutAction = helpMenu->addAction(QStringLiteral("About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, QStringLiteral("About transparent"),
            QStringLiteral(
                "<h3>transparent</h3>"
                "<p>Local, GPU-accelerated background removal, image upscaling, and bokeh "
                "for Linux. No cloud calls, no subscriptions, no telemetry.</p>"
                "<p>By Dragos Bajanica.</p>"
                "<p>Licensed under the GNU General Public License v3 (GPLv3). "
                "Bundled third-party components (Qt, vision.cpp/ggml, BiRefNet-lite, "
                "Real-ESRGAN) keep their own licenses.</p>"));
    });

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    // Top row: Simple/Advanced mode selector, plus (Simple mode only) a
    // dropdown to pick the one operation Simple mode runs. Advanced mode's
    // own step selection/ordering lives in its page below instead.
    auto* modeRow = new QHBoxLayout();
    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(QStringLiteral("Simple"));
    modeCombo_->addItem(QStringLiteral("Advanced"));
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::onModeChanged);
    modeRow->addWidget(modeCombo_);

    operationCombo_ = new QComboBox(this);
    if (pipeline_) {
        int defaultIndex = -1;
        for (size_t i = 0; i < pipeline_->stepCount(); ++i) {
            operationCombo_->addItem(pipeline_->stepName(i));
            if (pipeline_->stepName(i) == QStringLiteral("Background Removal")) {
                defaultIndex = static_cast<int>(i);
            }
        }
        if (defaultIndex < 0 && pipeline_->stepCount() > 0) {
            defaultIndex = 0;
        }
        for (size_t i = 0; i < pipeline_->stepCount(); ++i) {
            pipeline_->setStepEnabled(i, static_cast<int>(i) == defaultIndex);
        }
        if (defaultIndex >= 0) {
            operationCombo_->setCurrentIndex(defaultIndex);
        }
    }
    connect(operationCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!pipeline_ || index < 0) {
            return;
        }
        for (int i = 0; i < operationCombo_->count(); ++i) {
            pipeline_->setStepEnabled(static_cast<size_t>(i), i == index);
        }
        reprocess();
    });
    modeRow->addWidget(operationCombo_);
    modeRow->addStretch(1);
    layout->addLayout(modeRow);

    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    if (pipeline_ && pipeline_->stepCount() > 0) {
        statusLabel_->setText(QStringLiteral("Drop an image or a folder of images here"));
    } else {
        statusLabel_->setText(
            QStringLiteral("No background-removal model loaded — check models/ (see README)"));
        statusLabel_->setStyleSheet(QStringLiteral("color: #b00;"));
    }
    layout->addWidget(statusLabel_);

    // Shown/hidden directly rather than via QStackedWidget: Simple mode has
    // no page body of its own (its only control is the dropdown above), and
    // a QStackedWidget always reserves space for its *largest* page even
    // while showing a smaller one — which left a big empty gap under the
    // top row in Simple mode, since it was sized to fit Advanced's list.
    advancedPage_ = buildAdvancedPage();
    advancedPage_->setVisible(false);
    layout->addWidget(advancedPage_);

    previewLabel_ = new QLabel(this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(400, 300);
    previewLabel_->setFrameShape(QFrame::StyledPanel);

    // Floating "remove image" badge and busy spinner over the preview,
    // repositioned on every resize/preview update since previewLabel_'s
    // size isn't known until first shown.
    clearButton_ = new QPushButton(QStringLiteral("✕"), previewLabel_);
    clearButton_->setFixedSize(24, 24);
    clearButton_->setToolTip(QStringLiteral("Remove image"));
    clearButton_->setVisible(false);
    connect(clearButton_, &QPushButton::clicked, this, &MainWindow::onClearImageClicked);

    spinner_ = new SpinnerWidget(previewLabel_);
    spinner_->setVisible(false);

    exportButton_ = new QPushButton(QStringLiteral("Export PNG..."), this);
    exportButton_->setEnabled(false);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::onExportClicked);

    layout->addWidget(previewLabel_, 1);
    layout->addWidget(exportButton_);

    setCentralWidget(central);

    // Pipeline steps (background removal, upscaling, ...) run real GPU/CPU
    // inference that can take seconds; running that on the GUI thread froze
    // the whole window (no repainting) for the duration. QtConcurrent::run
    // moves the work off-thread; this watcher picks up the result.
    connect(&processingWatcher_, &QFutureWatcher<QImage>::finished, this,
            &MainWindow::onProcessingFinished);
}

QWidget* MainWindow::buildAdvancedPage() {
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    // Checkable, reorderable list of the same steps: InternalMove gives
    // drag-to-reorder for free, no custom drag/drop code needed. Checking or
    // reordering rows doesn't reprocess by itself — only the button below
    // does, since Advanced mode's whole point is an explicit trigger.
    advancedStepList_ = new QListWidget(page);
    advancedStepList_->setDragDropMode(QAbstractItemView::InternalMove);
    // Reordering rows (drag/drop) doesn't fire itemChanged — only a row's
    // own data changing does — so this only reacts to checkbox toggles.
    connect(advancedStepList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!pipeline_) {
            return;
        }
        const size_t index = static_cast<size_t>(item->data(Qt::UserRole).toULongLong());
        pipeline_->setStepEnabled(index, item->checkState() == Qt::Checked);
    });
    pageLayout->addWidget(advancedStepList_);
    populateAdvancedStepList();

    startProcessingButton_ = new QPushButton(QStringLiteral("Start Processing"), page);
    connect(startProcessingButton_, &QPushButton::clicked, this,
            &MainWindow::onStartProcessingClicked);
    pageLayout->addWidget(startProcessingButton_);

    return page;
}

void MainWindow::populateAdvancedStepList() {
    advancedStepList_->clear();
    if (!pipeline_) {
        return;
    }
    for (size_t i = 0; i < pipeline_->stepCount(); ++i) {
        auto* item = new QListWidgetItem(pipeline_->stepName(i), advancedStepList_);
        // Set before setCheckState(): the itemChanged handler above reads
        // this back out, so it must already be correct when that fires.
        item->setData(Qt::UserRole, static_cast<qulonglong>(i));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(pipeline_->isStepEnabled(i) ? Qt::Checked : Qt::Unchecked);
    }
}

bool MainWindow::isAdvancedMode() const {
    return modeCombo_->currentIndex() == 1;
}

std::vector<size_t> MainWindow::currentAdvancedStepOrder() const {
    std::vector<size_t> order;
    for (int row = 0; row < advancedStepList_->count(); ++row) {
        const QListWidgetItem* item = advancedStepList_->item(row);
        if (item->checkState() == Qt::Checked) {
            order.push_back(static_cast<size_t>(item->data(Qt::UserRole).toULongLong()));
        }
    }
    return order;
}

void MainWindow::onModeChanged(int index) {
    advancedPage_->setVisible(index == 1);
    operationCombo_->setVisible(index == 0);

    if (index == 1) {
        // Re-derive from Pipeline every time Advanced is shown, so a
        // selection change made in Simple mode is reflected. Any reordering
        // from an earlier Advanced session is intentionally not preserved —
        // Pipeline's declaration order is always the starting point.
        populateAdvancedStepList();
    } else if (pipeline_) {
        // Advanced mode may leave zero, one, or several steps enabled at
        // once; Simple mode allows exactly one, so collapse to whichever
        // was enabled first (or the dropdown's current selection if none
        // were), and write that collapsed state back to Pipeline so both
        // views stay consistent.
        int selected = -1;
        for (int i = 0; i < operationCombo_->count(); ++i) {
            if (pipeline_->isStepEnabled(static_cast<size_t>(i)) && selected < 0) {
                selected = i;
            }
        }
        if (selected < 0) {
            selected = operationCombo_->currentIndex();
        }
        if (selected < 0 && operationCombo_->count() > 0) {
            selected = 0;
        }
        {
            const QSignalBlocker blocker(operationCombo_);
            operationCombo_->setCurrentIndex(selected);
        }
        for (int i = 0; i < operationCombo_->count(); ++i) {
            pipeline_->setStepEnabled(static_cast<size_t>(i), i == selected);
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    repositionOverlays();
}

void MainWindow::repositionOverlays() {
    const int margin = 8;
    clearButton_->move(previewLabel_->width() - clearButton_->width() - margin, margin);
    spinner_->move((previewLabel_->width() - spinner_->width()) / 2,
                   (previewLabel_->height() - spinner_->height()) / 2);
}

void MainWindow::setControlsEnabled(bool enabled) {
    modeCombo_->setEnabled(enabled);
    operationCombo_->setEnabled(enabled);
    advancedStepList_->setEnabled(enabled);
    startProcessingButton_->setEnabled(enabled);
    if (!enabled) {
        exportButton_->setEnabled(false);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (processing_) {
        return;
    }
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls() || !mime->urls().first().isLocalFile()) {
        return;
    }
    const QFileInfo info(mime->urls().first().toLocalFile());
    if (info.isDir() || ImageFormats::isSupported(info.filePath())) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) {
        return;
    }
    const QUrl url = mime->urls().first();
    if (!url.isLocalFile()) {
        return;
    }

    const QString path = url.toLocalFile();
    const QFileInfo info(path);
    if (info.isDir()) {
        runBatch(path);
        event->acceptProposedAction();
    } else if (ImageFormats::isSupported(path)) {
        loadImage(path);
        event->acceptProposedAction();
    }
}

void MainWindow::loadImage(const QString& path) {
    if (processing_) {
        return;
    }

    QImage source(path);
    if (source.isNull()) {
        statusLabel_->setText(QStringLiteral("Could not read image: %1").arg(path));
        return;
    }

    sourceImage_ = source;
    clearButton_->setVisible(true);
    repositionOverlays();

    if (isAdvancedMode()) {
        // Advanced mode's whole point is an explicit trigger: show the
        // just-loaded (unprocessed) image and wait for Start Processing.
        resultImage_ = sourceImage_;
        updatePreview();
        exportButton_->setEnabled(!resultImage_.isNull());
        statusLabel_->setText(QStringLiteral("Loaded — press Start Processing"));
    } else {
        reprocess();
    }
}

void MainWindow::reprocess() {
    if (sourceImage_.isNull() || processing_) {
        return;
    }

    processing_ = true;
    setControlsEnabled(false);
    statusLabel_->clear();
    repositionOverlays();
    spinner_->start();

    const QImage input = sourceImage_;
    const std::shared_ptr<Pipeline> pipeline = pipeline_;
    const bool advanced = isAdvancedMode();
    const std::vector<size_t> stepOrder = advanced ? currentAdvancedStepOrder() : std::vector<size_t>();

    processingWatcher_.setFuture(QtConcurrent::run([pipeline, input, advanced, stepOrder]() -> QImage {
        if (!pipeline) {
            return input;
        }
        return advanced ? pipeline->run(input, stepOrder) : pipeline->run(input);
    }));
}

void MainWindow::onProcessingFinished() {
    resultImage_ = processingWatcher_.result();
    processing_ = false;
    spinner_->stop();
    setControlsEnabled(true);
    updatePreview();
    exportButton_->setEnabled(!resultImage_.isNull());
    statusLabel_->setText(QStringLiteral("Done"));
}

void MainWindow::onStartProcessingClicked() {
    reprocess();
}

void MainWindow::onClearImageClicked() {
    if (processing_) {
        return;
    }
    sourceImage_ = QImage();
    resultImage_ = QImage();
    previewLabel_->setPixmap(QPixmap());
    clearButton_->setVisible(false);
    exportButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("Drop an image or a folder of images here"));
}

void MainWindow::runBatch(const QString& folderPath) {
    if (processing_) {
        return;
    }
    if (!pipeline_ || pipeline_->stepCount() == 0) {
        statusLabel_->setText(QStringLiteral("No pipeline steps loaded, cannot process a folder"));
        return;
    }

    const QStringList images = batchRunner_.discoverImages(folderPath);
    if (images.isEmpty()) {
        statusLabel_->setText(QStringLiteral("No supported images found in that folder"));
        return;
    }

    const QString outputFolder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose output folder for %1 image(s)").arg(images.size()));
    if (outputFolder.isEmpty()) {
        return;
    }
    if (QDir(outputFolder) == QDir(folderPath)) {
        QMessageBox::warning(this, QStringLiteral("Same folder"),
                              QStringLiteral("Output folder must be different from the input "
                                              "folder, to avoid overwriting source images."));
        return;
    }

    const std::optional<std::vector<size_t>> stepOrder =
        isAdvancedMode() ? std::make_optional(currentAdvancedStepOrder()) : std::nullopt;

    processing_ = true;
    setControlsEnabled(false);

    const BatchResult result = batchRunner_.run(
        folderPath, outputFolder,
        [this](int done, int total, const QString& fileName) {
            statusLabel_->setText(
                QStringLiteral("Processing %1/%2: %3").arg(done).arg(total).arg(fileName));
            QApplication::processEvents();
        },
        stepOrder);

    processing_ = false;
    setControlsEnabled(true);

    if (result.failedFiles.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Batch done: %1 image(s) exported to %2")
                                   .arg(result.succeeded)
                                   .arg(outputFolder));
    } else {
        statusLabel_->setText(QStringLiteral("Batch done: %1 succeeded, %2 failed (%3)")
                                   .arg(result.succeeded)
                                   .arg(result.failedFiles.size())
                                   .arg(result.failedFiles.join(QStringLiteral(", "))));
    }
}

void MainWindow::updatePreview() {
    if (resultImage_.isNull()) {
        return;
    }

    QPixmap canvas(previewLabel_->size());
    QPainter painter(&canvas);
    painter.drawTiledPixmap(canvas.rect(), checkerboardPattern());

    const QImage scaled =
        resultImage_.scaled(previewLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint offset((canvas.width() - scaled.width()) / 2,
                         (canvas.height() - scaled.height()) / 2);
    painter.drawImage(offset, scaled);
    painter.end();

    previewLabel_->setPixmap(canvas);
    repositionOverlays();
}

void MainWindow::onExportClicked() {
    if (resultImage_.isNull()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export PNG"), QStringLiteral("output.png"),
        QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    resultImage_.save(path, "PNG");
}
