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

// Checkerboard behind the preview so removed background is visible. Drawn
// at physical-pixel resolution with the DPR set so it stays crisp on HiDPI.
QPixmap checkerboardPattern(qreal devicePixelRatio, int cell = 12) {
    QPixmap pattern(cell * 2 * devicePixelRatio, cell * 2 * devicePixelRatio);
    pattern.setDevicePixelRatio(devicePixelRatio);
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
            QStringLiteral("No background-removal model loaded — check models/ (see README)"));
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

    // Overlays float over the preview; repositioned on resize.
    clearButton_ = new QPushButton(QStringLiteral("✕"), previewLabel_);
    clearButton_->setFixedSize(24, 24);
    clearButton_->setToolTip(QStringLiteral("Remove image"));
    clearButton_->setVisible(false);
    connect(clearButton_, &QPushButton::clicked, this, &MainWindow::onClearImageClicked);

    spinner_ = new SpinnerWidget(previewLabel_);
    spinner_->setVisible(false);

    // Text matches what a run would save (GIF for animated sources).
    exportButton_ = new QPushButton(QStringLiteral("Export PNG"), this);
    exportButton_->setEnabled(false);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::onExportClicked);

    layout->addWidget(previewLabel_, 1);
    layout->addWidget(exportButton_);

    setCentralWidget(central);

    // Inference runs off the GUI thread (it can take seconds); watchers
    // pick up the results.
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
    clearButton_->move(previewLabel_->width() - clearButton_->width() - margin, margin);
    spinner_->move((previewLabel_->width() - spinner_->width()) / 2,
                   (previewLabel_->height() - spinner_->height()) / 2);
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

    exportButton_->setText(isAnimatedGifSource_ ? QStringLiteral("Export GIF")
                                                  : QStringLiteral("Export PNG"));

    clearButton_->setVisible(true);
    repositionOverlays();

    if (isAdvancedMode()) {
        // Wait for an explicit Start Processing; show the raw image.
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
    sourceGifFrames_.clear();
    resultGifFrames_.clear();
    sourceImage_ = QImage();
    resultImage_ = QImage();
    previewLabel_->setPixmap(QPixmap());
    clearButton_->setVisible(false);
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

    // Canvas at physical resolution with the DPR set, for crisp HiDPI.
    const qreal dpr = devicePixelRatioF();
    const QSize viewSize = previewLabel_->size();
    QPixmap canvas(viewSize * dpr);
    canvas.setDevicePixelRatio(dpr);
    QPainter painter(&canvas);
    painter.drawTiledPixmap(canvas.rect(), checkerboardPattern(dpr));

    // Only shrink to fit; smaller images render at 1:1.
    QSize displaySize = resultImage_.size();
    if (displaySize.width() > viewSize.width() || displaySize.height() > viewSize.height()) {
        displaySize.scale(viewSize, Qt::KeepAspectRatio);
    }
    const QPoint offset((viewSize.width() - displaySize.width()) / 2,
                         (viewSize.height() - displaySize.height()) / 2);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          displaySize != resultImage_.size());
    painter.drawImage(QRect(offset, displaySize), resultImage_);
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
