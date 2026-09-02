#include "MainWindow.h"

#include "core/BackgroundRemovalStep.h"
#include "core/BokehStep.h"
#include "core/DenoiseStep.h"
#include "core/GifIO.h"
#include "core/ImageFormats.h"
#include "core/ModelManager.h"
#include "core/SegmentationModel.h"
#include "core/UpscaleModel.h"
#include "core/UpscaleStep.h"
#include "core/VisionCppDenoiseModel.h"
#include "core/VisionCppSegmentationModel.h"
#include "core/VisionCppUpscaleModel.h"
#include "ui/PreviewCanvas.h"
#include "ui/SettingsPage.h"
#include "ui/SpinnerWidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QShortcut>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <optional>

namespace {

} // namespace

MainWindow::MainWindow(std::shared_ptr<Pipeline> pipeline, std::shared_ptr<ModelManager> modelManager,
                        QWidget* parent)
    : QMainWindow(parent), pipeline_(std::move(pipeline)), modelManager_(std::move(modelManager)),
      batchRunner_(pipeline_) {
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("Transparent"));

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    QAction* aboutAction = helpMenu->addAction(QStringLiteral("About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, QStringLiteral("About Transparent"),
            QStringLiteral(
                "<h3>Transparent</h3>"
                "<p>Local, GPU-accelerated background removal, image upscaling, and bokeh "
                "for Linux, with animated-GIF support (every frame runs through the same "
                "pipeline). No cloud calls, no subscriptions, no telemetry.</p>"
                "<p>By Dragos Bajanica.</p>"
                "<p>Licensed under the GNU General Public License v3 (GPLv3). "
                "Bundled third-party components (Qt, vision.cpp/ggml, BiRefNet-lite, "
                "Real-ESRGAN, giflib) keep their own licenses.</p>"));
    });

    // A menu-bar action rather than a third mode: model choice is a
    // set-occasionally preference, not a per-run one. Opens settingsDialog_,
    // built below once settingsPage_ exists.
    QAction* settingsAction = menuBar()->addAction(QStringLiteral("Settings"));
    connect(settingsAction, &QAction::triggered, this, [this]() {
        settingsDialog_->show();
        settingsDialog_->raise();
        settingsDialog_->activateWindow();
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
            QStringLiteral("No background-removal model loaded. Check models/ (see README)"));
        statusLabel_->setStyleSheet(QStringLiteral("color: #b00;"));
    }
    layout->addWidget(statusLabel_);

    // Shown/hidden directly, not via QStackedWidget, which reserves space
    // for its largest page and would leave an empty gap in Simple mode.
    advancedPage_ = buildAdvancedPage();
    advancedPage_->setVisible(false);
    layout->addWidget(advancedPage_);

    // Separate top-level dialog: unrelated to the current image. Not
    // WA_DeleteOnClose, so hiding it keeps scroll position and downloads.
    settingsPage_ = new SettingsPage(modelManager_.get());
    connect(settingsPage_, &SettingsPage::modelSelected, this, &MainWindow::onModelSelected);
    connect(settingsPage_, &SettingsPage::bokehStrengthChanged, this,
            &MainWindow::onBokehStrengthChanged);
    settingsDialog_ = new QDialog(this);
    settingsDialog_->setWindowTitle(QStringLiteral("Settings"));
    auto* settingsDialogLayout = new QVBoxLayout(settingsDialog_);
    settingsDialogLayout->addWidget(settingsPage_);

    if (modelManager_) {
        // Guarded on modelManager_ so tests without one don't touch user config.
        QSettings settings;
        settingsPage_->setActiveModel(
            ModelCategory::Segmentation,
            settings
                .value(ModelCatalog::settingsKey(ModelCategory::Segmentation),
                       ModelCatalog::defaultFilename(ModelCategory::Segmentation))
                .toString());
        settingsPage_->setActiveModel(
            ModelCategory::Denoise,
            settings
                .value(ModelCatalog::settingsKey(ModelCategory::Denoise),
                       ModelCatalog::defaultFilename(ModelCategory::Denoise))
                .toString());
        settingsPage_->setActiveModel(
            ModelCategory::Upscale,
            settings
                .value(ModelCatalog::settingsKey(ModelCategory::Upscale),
                       ModelCatalog::defaultFilename(ModelCategory::Upscale))
                .toString());
        settingsPage_->setBokehStrength(
            settings.value(BokehStep::settingsKey(), BokehStep::kDefaultStrengthPercent).toInt());
    }

    previewCanvas_ = new PreviewCanvas(this);
    previewCanvas_->setMinimumSize(400, 300);
    previewCanvas_->setFrameShape(QFrame::StyledPanel);

    // Overlays float over the preview; repositioned on resize.
    clearButton_ = new QPushButton(QStringLiteral("✕"), previewCanvas_);
    clearButton_->setFixedSize(24, 24);
    clearButton_->setToolTip(QStringLiteral("Remove image"));
    clearButton_->setVisible(false);
    connect(clearButton_, &QPushButton::clicked, this, &MainWindow::onClearImageClicked);

    // Wipe toggle sits directly under the ✕. Checkable: checked = compare
    // on. State persists in QSettings ("preview/wipeEnabled"), like the
    // bokeh strength; first-ever-launch default is enabled.
    wipeButton_ = new QPushButton(QStringLiteral("A|B"), previewCanvas_);
    wipeButton_->setFixedSize(24, 24);
    wipeButton_->setCheckable(true);
    wipeButton_->setChecked(
        QSettings().value(QStringLiteral("preview/wipeEnabled"), true).toBool());
    wipeButton_->setToolTip(QStringLiteral("Toggle before/after compare (W)"));
    wipeButton_->setVisible(false);
    connect(wipeButton_, &QPushButton::toggled, this, &MainWindow::onWipeToggled);

    // Zoom strip floats at the top-left: percent readout plus fit/100%.
    zoomStrip_ = new QWidget(previewCanvas_);
    auto* zoomStripLayout = new QHBoxLayout(zoomStrip_);
    zoomStripLayout->setContentsMargins(6, 2, 6, 2);
    zoomStripLayout->setSpacing(2);
    zoomLabel_ = new QLabel(zoomStrip_);
    auto* fitButton = new QPushButton(QStringLiteral("Fit"), zoomStrip_);
    auto* fullSizeButton = new QPushButton(QStringLiteral("1:1"), zoomStrip_);
    // Legibility: the strip floats over arbitrary images, so it paints a
    // semi-transparent dark scrim with white text (the video-player
    // pattern) — readable over anything, no image sampling heuristics.
    zoomStrip_->setAttribute(Qt::WA_StyledBackground, true);
    zoomStrip_->setStyleSheet(
        QStringLiteral("background-color: rgba(18, 18, 18, 170); border-radius: 6px;"));
    zoomLabel_->setStyleSheet(
        QStringLiteral("color: white; background: transparent; padding-right: 4px;"));
    const QString stripButtonSheet = QStringLiteral(
        "QPushButton { color: white; background: transparent; border: none;"
        " padding: 2px 6px; border-radius: 4px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 40); }");
    for (QPushButton* button : {fitButton, fullSizeButton}) {
        button->setFlat(true);
        button->setStyleSheet(stripButtonSheet);
        zoomStripLayout->addWidget(button);
    }
    zoomStripLayout->insertWidget(0, zoomLabel_);
    zoomStrip_->adjustSize();
    zoomStrip_->setVisible(false);
    connect(fitButton, &QPushButton::clicked, previewCanvas_, &PreviewCanvas::zoomToFit);
    connect(fullSizeButton, &QPushButton::clicked, previewCanvas_, &PreviewCanvas::zoomTo100);
    connect(previewCanvas_, &PreviewCanvas::zoomChanged, this, [this](double zoom) {
        zoomLabel_->setText(QString::number(qRound(zoom * 100)) + QStringLiteral("%"));
        zoomStrip_->adjustSize();
    });

    previewCanvas_->setWipeEnabled(wipeButton_->isChecked());

    // Keyboard: zoom and compare, matching the F/1/W scheme documented in
    // the button tooltips.
    auto addPreviewShortcut = [this](QKeySequence sequence, auto slot) {
        auto* shortcut = new QShortcut(sequence, this);
        connect(shortcut, &QShortcut::activated, previewCanvas_, slot);
    };
    addPreviewShortcut(QKeySequence(QStringLiteral("+")), &PreviewCanvas::zoomIn);
    addPreviewShortcut(QKeySequence(QStringLiteral("=")), &PreviewCanvas::zoomIn);
    addPreviewShortcut(QKeySequence(QStringLiteral("-")), &PreviewCanvas::zoomOut);
    addPreviewShortcut(QKeySequence(QStringLiteral("F")), &PreviewCanvas::zoomToFit);
    addPreviewShortcut(QKeySequence(QStringLiteral("1")), &PreviewCanvas::zoomTo100);
    auto* wipeShortcut = new QShortcut(QKeySequence(QStringLiteral("W")), this);
    connect(wipeShortcut, &QShortcut::activated, wipeButton_, &QPushButton::toggle);

    spinner_ = new SpinnerWidget(previewCanvas_);
    spinner_->setVisible(false);

    // Text matches what a run would save (GIF for animated sources).
    exportButton_ = new QPushButton(QStringLiteral("Export PNG"), this);
    exportButton_->setEnabled(false);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::onExportClicked);

    layout->addWidget(previewCanvas_, 1);
    layout->addWidget(exportButton_);

    setCentralWidget(central);

    // Inference runs off the GUI thread (it can take seconds); watchers
    // pick up the results.
    connect(&processingWatcher_, &QFutureWatcher<QImage>::finished, this,
            &MainWindow::onProcessingFinished);
    connect(&gifProcessingWatcher_, &QFutureWatcher<GifProcessResult>::finished, this,
            &MainWindow::onGifProcessingFinished);
    connect(&batchWatcher_, &QFutureWatcher<BatchResult>::finished, this,
            &MainWindow::onBatchFinished);
    connect(&segmentationModelWatcher_, &QFutureWatcher<std::shared_ptr<SegmentationModel>>::finished,
            this, &MainWindow::onSegmentationModelLoaded);
    connect(&denoiseModelWatcher_, &QFutureWatcher<std::shared_ptr<DenoiseModel>>::finished, this,
            &MainWindow::onDenoiseModelLoaded);
    connect(&upscaleModelWatcher_, &QFutureWatcher<std::shared_ptr<UpscaleModel>>::finished, this,
            &MainWindow::onUpscaleModelLoaded);
    connect(&bokehPreviewWatcher_, &QFutureWatcher<QImage>::finished, this,
            &MainWindow::onBokehPreviewReady);

    // Single-shot, re-armed per frame: GIF frames have individual delays.
    gifPreviewTimer_ = new QTimer(this);
    gifPreviewTimer_->setSingleShot(true);
    connect(gifPreviewTimer_, &QTimer::timeout, this, &MainWindow::advanceGifPreviewFrame);
}

QWidget* MainWindow::buildAdvancedPage() {
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    // InternalMove gives drag-to-reorder for free. Reordering doesn't
    // reprocess; only the button below does.
    advancedStepList_ = new QListWidget(page);
    advancedStepList_->setDragDropMode(QAbstractItemView::InternalMove);
    // itemChanged fires on checkbox toggles, not on drag reordering.
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
    // Populating fires itemChanged reentrantly with a transient Unchecked
    // state; left unblocked that would write corrupted enabled flags back
    // into pipeline_. One-way sync, so block the handler entirely.
    const QSignalBlocker blocker(advancedStepList_);
    for (size_t i = 0; i < pipeline_->stepCount(); ++i) {
        auto* item = new QListWidgetItem(pipeline_->stepName(i), advancedStepList_);
        item->setData(Qt::UserRole, static_cast<qulonglong>(i));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(pipeline_->isStepEnabled(i) ? Qt::Checked : Qt::Unchecked);
    }
}

template <typename T>
std::shared_ptr<T> MainWindow::findStepByName(const QString& name) const {
    if (!pipeline_) {
        return nullptr;
    }
    for (size_t i = 0; i < pipeline_->stepCount(); ++i) {
        if (pipeline_->stepName(i) == name) {
            return std::dynamic_pointer_cast<T>(pipeline_->stepAt(i));
        }
    }
    return nullptr;
}

std::shared_ptr<BackgroundRemovalStep> MainWindow::backgroundRemovalStep() const {
    return findStepByName<BackgroundRemovalStep>(QStringLiteral("Background Removal"));
}

std::shared_ptr<BokehStep> MainWindow::bokehStep() const {
    return findStepByName<BokehStep>(QStringLiteral("Bokeh"));
}

std::shared_ptr<DenoiseStep> MainWindow::denoiseStep() const {
    return findStepByName<DenoiseStep>(QStringLiteral("Denoise"));
}

std::shared_ptr<UpscaleStep> MainWindow::upscaleStep() const {
    return findStepByName<UpscaleStep>(QStringLiteral("Upscale"));
}

bool MainWindow::isBokehTheActiveOutputStep() const {
    if (!pipeline_) {
        return false;
    }
    std::vector<size_t> order;
    if (isAdvancedMode()) {
        order = currentAdvancedStepOrder();
    } else {
        // In Simple mode the enabled step is the active one.
        for (size_t i = 0; i < pipeline_->stepCount(); ++i) {
            if (pipeline_->isStepEnabled(i)) {
                order.push_back(i);
                break;
            }
        }
    }
    return !order.empty() && pipeline_->stepName(order.back()) == QStringLiteral("Bokeh");
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
        // Re-derive from Pipeline so Simple-mode selection changes show up.
        populateAdvancedStepList();
    } else if (pipeline_) {
        // Advanced can leave several steps enabled; Simple mode collapses
        // to the first enabled one (or the dropdown's current selection).
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
    clearButton_->move(previewCanvas_->width() - clearButton_->width() - margin, margin);
    wipeButton_->move(previewCanvas_->width() - wipeButton_->width() - margin,
                      margin + clearButton_->height() + 4);
    zoomStrip_->move(margin, margin);
    spinner_->move((previewCanvas_->width() - spinner_->width()) / 2,
                   (previewCanvas_->height() - spinner_->height()) / 2);
}

void MainWindow::setControlsEnabled(bool enabled) {
    modeCombo_->setEnabled(enabled);
    operationCombo_->setEnabled(enabled);
    advancedStepList_->setEnabled(enabled);
    startProcessingButton_->setEnabled(enabled);
    // Processing and model swapping disable each other so they can't race.
    settingsPage_->setBusy(!enabled);
    if (!enabled) {
        exportButton_->setEnabled(false);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (processing_ || modelLoading_ || bokehPreviewInFlight_) {
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
    if (processing_ || modelLoading_ || bokehPreviewInFlight_) {
        return;
    }
    stopGifPreviewAnimation();

    isAnimatedGifSource_ = GifIO::isAnimated(path);

    // Only the first frame is decoded here, just enough to preview the raw
    // source; the processing worker decodes the rest. GIF source frames are
    // never kept, they stream through the pipeline one at a time.
    QImageReader reader(path);
    const QImage firstFrame = reader.read();
    if (firstFrame.isNull()) {
        statusLabel_->setText(QStringLiteral("Could not read image: %1").arg(path));
        return;
    }
    sourceImagePath_ = path;
    sourceImage_ = firstFrame;

    exportButton_->setText(isAnimatedGifSource_ ? QStringLiteral("Export GIF")
                                                  : QStringLiteral("Export PNG"));

    previewCanvas_->setBeforeImage(sourceImage_);
    previewCanvas_->setCompareAllowed(!isAnimatedGifSource_);
    previewCanvas_->resetView();
    updateCompareControls();

    clearButton_->setVisible(true);
    repositionOverlays();

    if (isAdvancedMode()) {
        // Wait for an explicit Start Processing; show the raw image.
        resultImage_ = sourceImage_;
        updatePreview();
        exportButton_->setEnabled(!resultImage_.isNull());
        statusLabel_->setText(QStringLiteral("Loaded. Press Start Processing"));
    } else {
        reprocess();
    }
}

void MainWindow::reprocess() {
    const bool hasSource = isAnimatedGifSource_ ? !sourceImagePath_.isEmpty() : !sourceImage_.isNull();
    // modelLoading_ too: a swap writes the steps' model pointers on the GUI
    // thread while this reads them on a worker thread.
    if (!hasSource || processing_ || modelLoading_ || bokehPreviewInFlight_) {
        return;
    }
    stopGifPreviewAnimation();

    processing_ = true;
    setControlsEnabled(false);
    statusLabel_->clear();
    repositionOverlays();
    spinner_->start();

    const std::shared_ptr<Pipeline> pipeline = pipeline_;
    const bool advanced = isAdvancedMode();
    const std::vector<size_t> stepOrder = advanced ? currentAdvancedStepOrder() : std::vector<size_t>();

    if (isAnimatedGifSource_) {
        // The result GIF streams into a fresh temp file; only downscaled
        // preview frames come back to the GUI.
        gifResultFile_ = std::make_unique<QTemporaryFile>(
            QDir::temp().filePath(QStringLiteral("transparent-XXXXXX.gif")));
        if (!gifResultFile_->open()) {
            processing_ = false;
            setControlsEnabled(true);
            statusLabel_->setText(QStringLiteral("Could not create a temporary file"));
            return;
        }
        const QString resultPath = gifResultFile_->fileName();
        // giflib opens the path itself; a second open handle would conflict
        // on Windows. QTemporaryFile removes the file on destruction.
        gifResultFile_->close();

        const QString sourcePath = sourceImagePath_;
        QSize previewBounds = previewCanvas_->size();
        if (previewBounds.isEmpty()) {
            previewBounds = QSize(400, 300);
        }

        gifProcessingWatcher_.setFuture(
            QtConcurrent::run([pipeline, sourcePath, resultPath, advanced, stepOrder,
                                previewBounds]() -> GifProcessResult {
                GifProcessResult result;

                GifIO::Writer writer;
                if (!writer.open(resultPath)) {
                    return result;
                }

                GifIO::Reader reader(sourcePath);
                while (std::optional<GifIO::Frame> frame = reader.next()) {
                    QImage processed;
                    if (pipeline) {
                        processed = advanced ? pipeline->run(frame->image, stepOrder)
                                             : pipeline->run(frame->image);
                    } else {
                        processed = frame->image;
                    }
                    if (!writer.encode({processed, frame->delayCs})) {
                        return result;
                    }

                    // Preview copy: only shrink to the preview bounds.
                    QImage preview = processed;
                    if (preview.width() > previewBounds.width() ||
                        preview.height() > previewBounds.height()) {
                        preview = preview.scaled(previewBounds, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation);
                    }
                    result.previewFrames.push_back({std::move(preview), frame->delayCs});
                }

                // finish() fails when no frame was encoded (unreadable input).
                result.ok = writer.finish();
                if (!result.ok) {
                    result.previewFrames.clear();
                }
                return result;
            }));
    } else {
        const QImage input = sourceImage_;
        processingWatcher_.setFuture(
            QtConcurrent::run([pipeline, input, advanced, stepOrder]() -> QImage {
                if (!pipeline) {
                    return input;
                }
                return advanced ? pipeline->run(input, stepOrder) : pipeline->run(input);
            }));
    }
}

void MainWindow::onProcessingFinished() {
    resultImage_ = processingWatcher_.result();
    processing_ = false;
    spinner_->stop();
    setControlsEnabled(true);
    updatePreview();
    exportButton_->setEnabled(!resultImage_.isNull());
}

void MainWindow::onGifProcessingFinished() {
    const GifProcessResult result = gifProcessingWatcher_.result();
    processing_ = false;
    spinner_->stop();
    setControlsEnabled(true);
    gifPreviewFrames_ = result.previewFrames;
    gifPreviewFrameIndex_ = 0;
    if (result.ok && !gifPreviewFrames_.empty()) {
        resultImage_ = gifPreviewFrames_.front().image;
        exportButton_->setEnabled(true);
        statusLabel_->clear();
    } else {
        resultImage_ = QImage();
        exportButton_->setEnabled(false);
        statusLabel_->setText(QStringLiteral("Could not process the GIF"));
    }
    updatePreview();
    startGifPreviewAnimation();
}

void MainWindow::advanceGifPreviewFrame() {
    if (gifPreviewFrames_.empty()) {
        return;
    }
    gifPreviewFrameIndex_ =
        (gifPreviewFrameIndex_ + 1) % static_cast<int>(gifPreviewFrames_.size());
    resultImage_ = gifPreviewFrames_[static_cast<size_t>(gifPreviewFrameIndex_)].image;
    updatePreview();
    const int delayMs =
        std::max(10, gifPreviewFrames_[static_cast<size_t>(gifPreviewFrameIndex_)].delayCs * 10);
    gifPreviewTimer_->start(delayMs);
}

void MainWindow::startGifPreviewAnimation() {
    if (gifPreviewFrames_.size() <= 1) {
        return;
    }
    const int delayMs = std::max(10, gifPreviewFrames_.front().delayCs * 10);
    gifPreviewTimer_->start(delayMs);
}

void MainWindow::stopGifPreviewAnimation() {
    gifPreviewTimer_->stop();
}

void MainWindow::onStartProcessingClicked() {
    reprocess();
}

void MainWindow::onModelSelected(ModelCategory category, QString filename) {
    if (!modelManager_ || processing_ || modelLoading_ || bokehPreviewInFlight_ ||
        !modelManager_->isInstalled(filename)) {
        return;
    }

    modelLoading_ = true;
    setControlsEnabled(false);
    repositionOverlays();
    spinner_->start();
    statusLabel_->setText(QStringLiteral("Loading model..."));

    const QString path = modelManager_->pathFor(filename);
    if (category == ModelCategory::Segmentation) {
        pendingSegmentationFilename_ = filename;
        segmentationModelWatcher_.setFuture(
            QtConcurrent::run([path]() -> std::shared_ptr<SegmentationModel> {
                return std::make_shared<VisionCppSegmentationModel>(path);
            }));
    } else if (category == ModelCategory::Denoise) {
        pendingDenoiseFilename_ = filename;
        denoiseModelWatcher_.setFuture(
            QtConcurrent::run([path]() -> std::shared_ptr<DenoiseModel> {
                return std::make_shared<VisionCppDenoiseModel>(path);
            }));
    } else {
        pendingUpscaleFilename_ = filename;
        upscaleModelWatcher_.setFuture(QtConcurrent::run([path]() -> std::shared_ptr<UpscaleModel> {
            return std::make_shared<VisionCppUpscaleModel>(path);
        }));
    }
}

void MainWindow::onSegmentationModelLoaded() {
    const std::shared_ptr<SegmentationModel> model = segmentationModelWatcher_.result();
    if (model && model->isReady()) {
        // Bokeh shares Background Removal's model; update both together.
        if (auto step = backgroundRemovalStep()) {
            step->setModel(model);
        }
        if (auto bokeh = bokehStep()) {
            bokeh->setModel(model);
        }
        QSettings settings;
        settings.setValue(ModelCatalog::settingsKey(ModelCategory::Segmentation),
                           pendingSegmentationFilename_);
        settingsPage_->setActiveModel(ModelCategory::Segmentation, pendingSegmentationFilename_);
        statusLabel_->clear();
    } else {
        statusLabel_->setText(QStringLiteral("Could not load that model"));
        // Qt checks the radio on click before modelSelected arrives; put it
        // back on what QSettings still says.
        QSettings settings;
        settingsPage_->setActiveModel(
            ModelCategory::Segmentation,
            settings
                .value(ModelCatalog::settingsKey(ModelCategory::Segmentation),
                       ModelCatalog::defaultFilename(ModelCategory::Segmentation))
                .toString());
    }
    modelLoading_ = false;
    spinner_->stop();
    setControlsEnabled(true);
}

void MainWindow::onDenoiseModelLoaded() {
    const std::shared_ptr<DenoiseModel> model = denoiseModelWatcher_.result();
    if (model && model->isReady()) {
        if (auto step = denoiseStep()) {
            step->setModel(model);
        }
        QSettings settings;
        settings.setValue(ModelCatalog::settingsKey(ModelCategory::Denoise), pendingDenoiseFilename_);
        settingsPage_->setActiveModel(ModelCategory::Denoise, pendingDenoiseFilename_);
        statusLabel_->clear();
    } else {
        statusLabel_->setText(QStringLiteral("Could not load that model"));
        // Qt checks the radio on click before modelSelected arrives; put it
        // back on what QSettings still says.
        QSettings settings;
        settingsPage_->setActiveModel(
            ModelCategory::Denoise,
            settings
                .value(ModelCatalog::settingsKey(ModelCategory::Denoise),
                       ModelCatalog::defaultFilename(ModelCategory::Denoise))
                .toString());
    }
    modelLoading_ = false;
    spinner_->stop();
    setControlsEnabled(true);
}

void MainWindow::onUpscaleModelLoaded() {
    const std::shared_ptr<UpscaleModel> model = upscaleModelWatcher_.result();
    if (model && model->isReady()) {
        if (auto step = upscaleStep()) {
            step->setModel(model);
        }
        QSettings settings;
        settings.setValue(ModelCatalog::settingsKey(ModelCategory::Upscale), pendingUpscaleFilename_);
        settingsPage_->setActiveModel(ModelCategory::Upscale, pendingUpscaleFilename_);
        statusLabel_->clear();
    } else {
        statusLabel_->setText(QStringLiteral("Could not load that model"));
        QSettings settings;
        settingsPage_->setActiveModel(
            ModelCategory::Upscale,
            settings
                .value(ModelCatalog::settingsKey(ModelCategory::Upscale),
                       ModelCatalog::defaultFilename(ModelCategory::Upscale))
                .toString());
    }
    modelLoading_ = false;
    spinner_->stop();
    setControlsEnabled(true);
}

void MainWindow::onBokehStrengthChanged(int percent) {
    QSettings settings;
    settings.setValue(BokehStep::settingsKey(), percent);

    const auto bokeh = bokehStep();
    if (!bokeh) {
        return;
    }
    // Takes effect at the next real process() call.
    bokeh->setStrengthPercent(percent);

    if (processing_ || modelLoading_ || bokehPreviewInFlight_ || !bokeh->hasCachedMask() ||
        !isBokehTheActiveOutputStep()) {
        return;
    }
    // Pass the in-flight value explicitly; this runs on a worker thread.
    bokehPreviewInFlight_ = true;
    bokehPreviewWatcher_.setFuture(
        QtConcurrent::run([bokeh, percent]() -> QImage { return bokeh->reblendCached(percent); }));
}

void MainWindow::onBokehPreviewReady() {
    bokehPreviewInFlight_ = false;
    const QImage preview = bokehPreviewWatcher_.result();
    if (!preview.isNull()) {
        resultImage_ = preview;
        updatePreview();
    }
}

void MainWindow::onClearImageClicked() {
    if (processing_ || modelLoading_ || bokehPreviewInFlight_) {
        return;
    }
    stopGifPreviewAnimation();
    isAnimatedGifSource_ = false;
    sourceImagePath_.clear();
    gifResultFile_.reset();
    gifPreviewFrames_.clear();
    sourceImage_ = QImage();
    resultImage_ = QImage();
    previewCanvas_->clearImages();
    clearButton_->setVisible(false);
    updateCompareControls();
    exportButton_->setText(QStringLiteral("Export PNG"));
    exportButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("Drop an image or a folder of images here"));
}

void MainWindow::runBatch(const QString& folderPath) {
    if (processing_ || modelLoading_ || bokehPreviewInFlight_) {
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
    batchRunning_ = true;
    updateCompareControls();
    batchOutputFolder_ = outputFolder;
    statusLabel_->setText(QStringLiteral("Batch starting..."));

    // BatchRunner is copied by value so the worker never touches MainWindow
    // state directly; progress is marshalled to the GUI thread per image,
    // completion arrives via onBatchFinished().
    const BatchRunner runner = batchRunner_;
    const QPointer<MainWindow> guard(this);
    batchWatcher_.setFuture(
        QtConcurrent::run([runner, folderPath, outputFolder, stepOrder, guard]() -> BatchResult {
            return runner.run(
                folderPath, outputFolder,
                [guard](int done, int total, const QString& fileName) {
                    if (guard) {
                        QMetaObject::invokeMethod(guard, &MainWindow::showBatchProgress,
                                                   Qt::QueuedConnection, done, total, fileName);
                    }
                },
                stepOrder);
        }));
}

void MainWindow::showBatchProgress(int done, int total, QString fileName) {
    statusLabel_->setText(
        QStringLiteral("Processing %1/%2: %3").arg(done).arg(total).arg(fileName));
}

void MainWindow::onBatchFinished() {
    const BatchResult result = batchWatcher_.result();
    processing_ = false;
    batchRunning_ = false;
    setControlsEnabled(true);
    updateCompareControls();

    if (result.failedFiles.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Batch done: %1 image(s) exported to %2")
                                   .arg(result.succeeded)
                                   .arg(batchOutputFolder_));
    } else {
        statusLabel_->setText(QStringLiteral("Batch done: %1 succeeded, %2 failed (%3)")
                                   .arg(result.succeeded)
                                   .arg(result.failedFiles.size())
                                   .arg(result.failedFiles.join(QStringLiteral(", "))));
    }
}

void MainWindow::updatePreview() {
    previewCanvas_->setAfterImage(resultImage_);
    updateCompareControls();
}

void MainWindow::updateCompareControls() {
    const bool hasImage = !sourceImage_.isNull();
    const bool available = previewCanvas_->isCompareAvailable();
    wipeButton_->setEnabled(available);
    // Batch runs save directly to the output folder; no compare UI there.
    wipeButton_->setVisible(hasImage && !batchRunning_);
    zoomStrip_->setVisible(hasImage && !batchRunning_);
    if (available) {
        wipeButton_->setToolTip(QStringLiteral("Toggle before/after compare (W)"));
    } else if (isAnimatedGifSource_) {
        wipeButton_->setToolTip(QStringLiteral("Compare is not available for animations"));
    } else {
        wipeButton_->setToolTip(QStringLiteral("Compare needs a loaded image"));
    }
}

void MainWindow::onWipeToggled(bool enabled) {
    previewCanvas_->setWipeEnabled(enabled);
    QSettings settings;
    settings.setValue(QStringLiteral("preview/wipeEnabled"), enabled);
}

QString MainWindow::defaultExportPath(const QString& sourcePath, const QString& suffix) {
    QString name = QStringLiteral("output");
    if (!sourcePath.isEmpty()) {
        const QString base = QFileInfo(sourcePath).completeBaseName();
        if (!base.isEmpty()) {
            name = base + QStringLiteral("_cutout");
        }
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath();
    }
    // Absolute, because a relative default resolves against the process's
    // cwd, which is arbitrary under AppImage launches.
    return QDir(dir).filePath(name + suffix);
}

void MainWindow::onExportClicked() {
    if (isAnimatedGifSource_) {
        if (!gifResultFile_ || gifResultFile_->fileName().isEmpty()) {
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export GIF"),
            defaultExportPath(sourceImagePath_, QStringLiteral(".gif")),
            QStringLiteral("GIF image (*.gif)"));
        if (path.isEmpty()) {
            return;
        }
        // getSaveFileName() has already confirmed overwriting.
        QFile::remove(path);
        if (!QFile::copy(gifResultFile_->fileName(), path)) {
            QMessageBox::warning(this, QStringLiteral("Export failed"),
                                  QStringLiteral("Could not write %1").arg(path));
        }
        return;
    }

    if (resultImage_.isNull()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export PNG"),
        defaultExportPath(sourceImagePath_, QStringLiteral(".png")),
        QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    resultImage_.save(path, "PNG");
}
