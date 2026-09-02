#include "SettingsPage.h"

#include "core/ModelManager.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

SettingsPage::SettingsPage(ModelManager* modelManager, QWidget* parent)
    : QWidget(parent), modelManager_(modelManager) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(buildCategorySection(ModelCategory::Segmentation,
                                            QStringLiteral("Background removal / bokeh model")));
    layout->addWidget(buildCategorySection(ModelCategory::Denoise, QStringLiteral("Denoise model")));
    layout->addWidget(buildCategorySection(ModelCategory::Upscale, QStringLiteral("Upscale model")));
    layout->addWidget(buildBokehStrengthSection());

    // Files in the models folder that no category can load, grayed out with
    // an explanatory tooltip (e.g. an unsupported architecture).
    otherFilesBox_ = new QGroupBox(QStringLiteral("Other files in the models folder"), this);
    otherFilesLayout_ = new QVBoxLayout(otherFilesBox_);
    otherFilesBox_->setVisible(false);
    layout->addWidget(otherFilesBox_);

    // Single import entry point for advanced users: the architecture routes
    // the file to its category automatically (the directory scan is the
    // primary mechanism and works without this button).
    auto* addButton = new QPushButton(QStringLiteral("Add model from disk\u2026"), this);
    addButton->setToolTip(
        QStringLiteral("Copy a .gguf file into the models folder; it is classified by "
                       "architecture and appears in its section"));
    layout->addWidget(addButton);
    importButtons_.append(addButton);
    connect(addButton, &QPushButton::clicked, this, [this]() { importFromDisk(); });

    layout->addStretch(1);

    // Folder-as-state: build the scanned custom rows before any
    // setActiveModel() call, so persisted custom selections resolve.
    refreshCustomModels();

    if (!modelManager_) {
        return;
    }

    connect(modelManager_, &ModelManager::downloadProgress, this,
            [this](const QString& filename, qint64 received, qint64 total) {
                const auto it = rows_.constFind(filename);
                if (it == rows_.constEnd()) {
                    return;
                }
                it->progressBar->setVisible(true);
                it->downloadButton->setVisible(false);
                if (total > 0) {
                    it->progressBar->setRange(0, 100);
                    it->progressBar->setValue(static_cast<int>(received * 100 / total));
                } else {
                    it->progressBar->setRange(0, 0); // Indeterminate: server didn't send a length.
                }
            });
    connect(modelManager_, &ModelManager::downloadFinished, this,
            [this](const QString& filename, bool success, const QString& errorMessage) {
                const auto it = rows_.constFind(filename);
                if (it == rows_.constEnd()) {
                    return;
                }
                it->progressBar->setVisible(false);
                if (!success) {
                    it->statusLabel->setText(QStringLiteral("Download failed: %1").arg(errorMessage));
                }
                refreshRow(filename);
            });
}

QWidget* SettingsPage::buildCategorySection(ModelCategory category, const QString& title) {
    auto* box = new QGroupBox(title, this);
    auto* layout = new QVBoxLayout(box);
    // One button group per category, so the two lists don't compete. Custom
    // radios join the same group via customAreas_.
    auto* group = new QButtonGroup(box);

    for (const ModelInfo& info : ModelCatalog::modelsForCategory(category)) {
        auto* row = new QWidget(box);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto* radio = new QRadioButton(info.displayName, row);
        group->addButton(radio);
        rowLayout->addWidget(radio);

        const double approxMb = info.approxSizeBytes / (1024.0 * 1024.0);
        auto* status = new QLabel(
            QStringLiteral("%1, ~%2 MB").arg(info.license, QString::number(approxMb, 'f', 0)), row);
        rowLayout->addWidget(status);
        rowLayout->addStretch(1);

        auto* progress = new QProgressBar(row);
        progress->setVisible(false);
        progress->setFixedWidth(100);
        rowLayout->addWidget(progress);

        auto* downloadButton = new QPushButton(QStringLiteral("Download"), row);
        rowLayout->addWidget(downloadButton);

        layout->addWidget(row);
        rows_.insert(info.filename, {radio, status, downloadButton, progress});

        connect(downloadButton, &QPushButton::clicked, this, [this, info]() {
            if (modelManager_) {
                modelManager_->downloadModel(info);
            }
        });
        connect(radio, &QRadioButton::toggled, this,
                [this, category, filename = info.filename](bool checked) {
                    if (checked) {
                        emit modelSelected(category, filename);
                    }
                });

        refreshRow(info.filename);
    }

    // Home for scanned custom rows, rebuilt on every refreshCustomModels().
    auto* customArea = new QWidget(box);
    auto* customLayout = new QVBoxLayout(customArea);
    customLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(customArea);
    customAreas_.insert(category, {customArea, customLayout, group});

    return box;
}

QWidget* SettingsPage::buildBokehStrengthSection() {
    auto* box = new QGroupBox(QStringLiteral("Bokeh strength"), this);
    auto* rowLayout = new QHBoxLayout(box);

    bokehStrengthSlider_ = new QSlider(Qt::Horizontal, box);
    bokehStrengthSlider_->setRange(0, 100);
    rowLayout->addWidget(bokehStrengthSlider_);

    bokehStrengthValueLabel_ = new QLabel(box);
    bokehStrengthValueLabel_->setMinimumWidth(40);
    rowLayout->addWidget(bokehStrengthValueLabel_);

    connect(bokehStrengthSlider_, &QSlider::valueChanged, this, [this](int value) {
        bokehStrengthValueLabel_->setText(QStringLiteral("%1%").arg(value));
        emit bokehStrengthChanged(value);
    });
    // Avoids a one-paint "0%" label before setBokehStrength() runs.
    bokehStrengthValueLabel_->setText(QStringLiteral("%1%").arg(bokehStrengthSlider_->value()));

    return box;
}

void SettingsPage::refreshRow(const QString& filename) {
    const auto it = rows_.constFind(filename);
    if (it == rows_.constEnd()) {
        return;
    }
    const bool installed = modelManager_ && modelManager_->isInstalled(filename);
    const bool downloading = modelManager_ && modelManager_->isDownloading(filename);

    it->radio->setEnabled(installed && !busy_);
    if (it->downloadButton) {
        it->downloadButton->setVisible(!installed && !downloading);
        it->downloadButton->setEnabled(!busy_);
    }
    if (it->progressBar) {
        it->progressBar->setVisible(downloading);
    }
    if (installed) {
        it->statusLabel->setText(QStringLiteral("Installed"));
    } else if (!downloading) {
        it->statusLabel->setText(QStringLiteral("Not installed"));
    }
}

void SettingsPage::setActiveModel(ModelCategory category, const QString& filename) {
    Q_UNUSED(category);
    const auto it = rows_.constFind(filename);
    if (it == rows_.constEnd()) {
        return;
    }
    // Reporting state, not a user pick; don't re-emit.
    const QSignalBlocker blocker(it->radio);
    it->radio->setChecked(true);
}

void SettingsPage::refreshCustomModels() {
    // Rebuild from scratch: the folder is the state, so anything may have
    // changed on disk (files added, removed, or renamed) since last time.
    // Immediate delete is safe: no refresh is ever triggered from a widget
    // that is being deleted here.
    for (QWidget* widget : std::exchange(customWidgets_, {})) {
        delete widget;
    }
    for (QWidget* widget : std::exchange(unusableWidgets_, {})) {
        delete widget;
    }
    // Custom rows are keyed by filename in rows_; drop the stale entries
    // (curated entries stay, they are never custom).
    for (auto it = rows_.begin(); it != rows_.end();) {
        it = ModelCatalog::findByFilename(it.key()) ? std::next(it) : rows_.erase(it);
    }

    if (!modelManager_) {
        return;
    }

    int unusableCount = 0;
    for (const ScannedModel& model : modelManager_->scanModels()) {
        if (model.usable) {
            buildCustomRow(model.info);
        } else {
            auto* label = new QLabel(model.info.filename, otherFilesBox_);
            label->setEnabled(false);
            label->setToolTip(model.note);
            otherFilesLayout_->addWidget(label);
            unusableWidgets_.push_back(label);
            ++unusableCount;
        }
    }
    otherFilesBox_->setVisible(unusableCount > 0);

    // Rebuilt radios start unchecked; restore whatever model QSettings says
    // is active per category so a custom active model keeps its check.
    QSettings settings;
    for (auto it = customAreas_.constBegin(); it != customAreas_.constEnd(); ++it) {
        const QString active =
            settings.value(ModelCatalog::settingsKey(it.key()),
                           ModelCatalog::defaultFilename(it.key()))
                .toString();
        setActiveModel(it.key(), active);
    }
}

void SettingsPage::buildCustomRow(const ModelInfo& info) {
    const CustomArea& area = customAreas_[info.category];
    auto* row = new QWidget(area.container);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto* radio = new QRadioButton(info.displayName, row);
    area.group->addButton(radio);
    rowLayout->addWidget(radio);

    const double approxMb = info.approxSizeBytes / (1024.0 * 1024.0);
    auto* status = new QLabel(
        QStringLiteral("%1, ~%2 MB").arg(info.license, QString::number(approxMb, 'f', 0)), row);
    rowLayout->addWidget(status);
    rowLayout->addStretch(1);

    // The file is on disk by construction, so custom rows are always
    // "installed" and never download.
    area.layout->addWidget(row);
    rows_.insert(info.filename, {radio, status, nullptr, nullptr});
    customWidgets_.push_back(row);

    connect(radio, &QRadioButton::toggled, this,
            [this, category = info.category, filename = info.filename](bool checked) {
                if (checked) {
                    emit modelSelected(category, filename);
                }
            });

    refreshRow(info.filename);
}

void SettingsPage::importFromDisk() {
    if (!modelManager_) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose a GGUF model file"), QString(),
        QStringLiteral("GGUF models (*.gguf)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!modelManager_->importModel(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Could not import model"), error);
    }
    refreshCustomModels();
}

void SettingsPage::showEvent(QShowEvent* /*event*/) {
    refreshCustomModels();
}

void SettingsPage::setBokehStrength(int percent) {
    const QSignalBlocker blocker(bokehStrengthSlider_);
    bokehStrengthSlider_->setValue(percent);
    bokehStrengthValueLabel_->setText(QStringLiteral("%1%").arg(bokehStrengthSlider_->value()));
}

void SettingsPage::setBusy(bool busy) {
    busy_ = busy;
    for (auto it = rows_.constBegin(); it != rows_.constEnd(); ++it) {
        refreshRow(it.key());
    }
    for (QPushButton* button : importButtons_) {
        button->setEnabled(!busy_);
    }
    bokehStrengthSlider_->setEnabled(!busy_);
}
