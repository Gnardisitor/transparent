#include "MainWindow.h"

#include "core/BackgroundRemovalStep.h"
#include "core/BokehStep.h"
#include "core/GifIO.h"
#include "core/ImageFormats.h"
#include "core/ModelManager.h"
#include "core/SegmentationModel.h"
#include "core/UpscaleModel.h"
#include "core/UpscaleStep.h"
#include "core/VisionCppSegmentationModel.h"
#include "core/VisionCppUpscaleModel.h"
#include "ui/SettingsPage.h"
#include "ui/SpinnerWidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
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
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
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

MainWindow::MainWindow(std::shared_ptr<Pipeline> pipeline, std::shared_ptr<ModelManager> modelManager,
                        QWidget* parent)
    : QMainWindow(parent), pipeline_(std::move(pipeline)), modelManager_(std::move(modelManager)),
      batchRunner_(pipeline_) {
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
                "for Linux, with animated-GIF support (every frame runs through the same "
                "pipeline). No cloud calls, no subscriptions, no telemetry.</p>"
                "<p>By Dragos Bajanica.</p>"
                "<p>Licensed under the GNU General Public License v3 (GPLv3). "
                "Bundled third-party components (Qt, vision.cpp/ggml, BiRefNet-lite, "
                "Real-ESRGAN, giflib) keep their own licenses.</p>"));
    });

    // A top-level menu-bar action (next to Help), not a Simple/Advanced mode
    // — model choice is a "set occasionally, then forget" preference, not a
    // per-run one, so it shouldn't require leaving whatever mode you're
    // already working in, or share the main window's image-drop status
    // label. Opens settingsDialog_, built further down once settingsPage_
    // exists.
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

    // A separate top-level dialog rather than a page inside `central`: it
    // has nothing to do with the current image (no drop-hint status label,
    // no preview), so it shouldn't share the main window's layout. Not
    // WA_DeleteOnClose — closing it (the X button, or Escape) just hides
    // it, same as any settings panel you'd reopen later without losing its
    // scroll position or in-flight download rows.
    settingsPage_ = new SettingsPage(modelManager_.get());
    connect(settingsPage_, &SettingsPage::modelSelected, this, &MainWindow::onModelSelected);
    connect(settingsPage_, &SettingsPage::bokehStrengthChanged, this,
            &MainWindow::onBokehStrengthChanged);
    settingsDialog_ = new QDialog(this);
    settingsDialog_->setWindowTitle(QStringLiteral("Settings"));
    auto* settingsDialogLayout = new QVBoxLayout(settingsDialog_);
    settingsDialogLayout->addWidget(settingsPage_);

    if (modelManager_) {
        // Reflects whatever main.cpp actually loaded (QSettings if
        // something was persisted, the catalog default otherwise) so the
        // dialog doesn't open with no radio selected. Guarded on
        // modelManager_ so tests constructing MainWindow without one (e.g.
        // test_main_window.cpp) don't touch real QSettings/user config.
        QSettings settings;
        settingsPage_->setActiveModel(
            ModelCategory::Segmentation,
            settings
                .value(ModelCatalog::settingsKey(ModelCategory::Segmentation),
                       ModelCatalog::defaultFilename(ModelCategory::Segmentation))
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
    connect(&gifProcessingWatcher_, &QFutureWatcher<std::vector<GifIO::Frame>>::finished, this,
            &MainWindow::onGifProcessingFinished);
    connect(&segmentationModelWatcher_, &QFutureWatcher<std::shared_ptr<SegmentationModel>>::finished,
            this, &MainWindow::onSegmentationModelLoaded);
    connect(&upscaleModelWatcher_, &QFutureWatcher<std::shared_ptr<UpscaleModel>>::finished, this,
            &MainWindow::onUpscaleModelLoaded);
    connect(&bokehPreviewWatcher_, &QFutureWatcher<QImage>::finished, this,
            &MainWindow::onBokehPreviewReady);

    // Single-shot and re-armed with the new current frame's own delay each
    // time it fires (advanceGifPreviewFrame), since GIF frames don't share
    // one fixed interval the way a repeating QTimer assumes.
    gifPreviewTimer_ = new QTimer(this);
    gifPreviewTimer_->setSingleShot(true);
    connect(gifPreviewTimer_, &QTimer::timeout, this, &MainWindow::advanceGifPreviewFrame);
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
    // Populating fires itemChanged reentrantly — QListWidgetItem's own
    // construction/setData/setFlags calls below each emit it at least once,
    // every time reporting the item's not-yet-set default Qt::Unchecked
    // state, before setCheckState() ever runs. Left unblocked, the
    // itemChanged handler (below) writes that transient Unchecked back into
    // pipeline_ — silently disabling a step this function meant to leave
    // enabled (e.g. Background Removal, Simple mode's default), since by
    // the time setCheckState() itself runs, isStepEnabled() already reads
    // back the corrupted false and "confirms" Unchecked instead of
    // overwriting it, so nothing ever restores the correct state. This is a
    // one-way sync (Pipeline state -> checkbox display), so the handler has
    // no business firing during it at all.
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
        // Simple mode's operationCombo_ handler enables exactly one step;
        // reprocess() then runs the plain Pipeline::run(input) overload,
        // which just skips every disabled one — so "active" here means
        // whichever single step is currently enabled.
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
    // Covers both directions: processing an image disables model swapping,
    // and (via onModelSelected/onSegmentationModelLoaded/
    // onUpscaleModelLoaded below, which also route through here) swapping a
    // model disables processing — so the two can never race each other.
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

    if (GifIO::isAnimated(path)) {
        std::vector<GifIO::Frame> frames = GifIO::readFrames(path);
        if (frames.empty()) {
            statusLabel_->setText(QStringLiteral("Could not read image: %1").arg(path));
            return;
        }
        isAnimatedGifSource_ = true;
        sourceGifFrames_ = std::move(frames);
        sourceImage_ = sourceGifFrames_.front().image;
    } else {
        const QImage source(path);
        if (source.isNull()) {
            statusLabel_->setText(QStringLiteral("Could not read image: %1").arg(path));
            return;
        }
        isAnimatedGifSource_ = false;
        sourceGifFrames_.clear();
        sourceImage_ = source;
    }
    resultGifFrames_.clear();

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
    const bool hasSource = isAnimatedGifSource_ ? !sourceGifFrames_.empty() : !sourceImage_.isNull();
    // modelLoading_ too: pipeline steps' setModel() runs on the GUI thread
    // once a swap lands, and this dispatches Pipeline::run() onto a
    // background thread that reads those same steps' model pointers — the
    // two must never overlap.
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
        const std::vector<GifIO::Frame> input = sourceGifFrames_;
        gifProcessingWatcher_.setFuture(QtConcurrent::run(
            [pipeline, input, advanced, stepOrder]() -> std::vector<GifIO::Frame> {
                std::vector<GifIO::Frame> output = input;
                for (GifIO::Frame& frame : output) {
                    if (pipeline) {
                        frame.image = advanced ? pipeline->run(frame.image, stepOrder)
                                                : pipeline->run(frame.image);
                    }
                }
                return output;
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
    resultGifFrames_ = gifProcessingWatcher_.result();
    processing_ = false;
    spinner_->stop();
    setControlsEnabled(true);
    gifPreviewFrameIndex_ = 0;
    resultImage_ = resultGifFrames_.empty() ? QImage() : resultGifFrames_.front().image;
    updatePreview();
    exportButton_->setEnabled(!resultGifFrames_.empty());
    startGifPreviewAnimation();
}

void MainWindow::advanceGifPreviewFrame() {
    if (resultGifFrames_.empty()) {
        return;
    }
    gifPreviewFrameIndex_ = (gifPreviewFrameIndex_ + 1) % static_cast<int>(resultGifFrames_.size());
    resultImage_ = resultGifFrames_[static_cast<size_t>(gifPreviewFrameIndex_)].image;
    updatePreview();
    const int delayMs =
        std::max(10, resultGifFrames_[static_cast<size_t>(gifPreviewFrameIndex_)].delayCs * 10);
    gifPreviewTimer_->start(delayMs);
}

void MainWindow::startGifPreviewAnimation() {
    if (resultGifFrames_.size() <= 1) {
        return;
    }
    const int delayMs = std::max(10, resultGifFrames_.front().delayCs * 10);
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
        // Bokeh always mirrors Background Removal's segmentation model —
        // the mask it needs is exactly the one Background Removal already
        // computes, so both steps get the same new model together.
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
        // The radio button already flipped to the failed selection (Qt
        // checks it on click, before modelSelected ever reaches here) — put
        // it back on whatever's still actually running, which is exactly
        // what QSettings still says since a failed load never writes to it.
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
    // Takes effect on the step's next real process() call regardless of
    // whether the live-preview branch below fires this time.
    bokeh->setStrengthPercent(percent);

    if (processing_ || modelLoading_ || bokehPreviewInFlight_ || !bokeh->hasCachedMask() ||
        !isBokehTheActiveOutputStep()) {
        return;
    }
    // `percent` is passed explicitly (not read back via bokeh->
    // strengthPercent() on the worker thread) so this can't race a
    // concurrent write to that member — not that one's possible anyway
    // once bokehPreviewInFlight_ is set below, but reblendCached() is
    // documented to take it this way regardless (see its own comment).
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
    sourceGifFrames_.clear();
    resultGifFrames_.clear();
    sourceImage_ = QImage();
    resultImage_ = QImage();
    previewLabel_->setPixmap(QPixmap());
    clearButton_->setVisible(false);
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
    if (isAnimatedGifSource_) {
        if (resultGifFrames_.empty()) {
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export GIF"), QStringLiteral("output.gif"),
            QStringLiteral("GIF image (*.gif)"));
        if (path.isEmpty()) {
            return;
        }
        GifIO::writeFrames(path, resultGifFrames_);
        return;
    }

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
