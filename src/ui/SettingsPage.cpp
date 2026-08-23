#include "SettingsPage.h"

#include "core/ModelManager.h"

#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

SettingsPage::SettingsPage(ModelManager* modelManager, QWidget* parent)
    : QWidget(parent), modelManager_(modelManager) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(buildCategorySection(ModelCategory::Segmentation,
                                            QStringLiteral("Background removal / bokeh model")));
    layout->addWidget(buildCategorySection(ModelCategory::Upscale, QStringLiteral("Upscale model")));
    layout->addWidget(buildBokehStrengthSection());
    layout->addStretch(1);

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
    // Scoped to this one category's QGroupBox, so Segmentation and Upscale
    // rows never compete for the same exclusive selection.
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
    // Slider starts at 0 before setBokehStrength() sets its real value —
    // this just keeps the label from reading "0%" for one paint.
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
    it->downloadButton->setVisible(!installed && !downloading);
    it->downloadButton->setEnabled(!busy_);
    it->progressBar->setVisible(downloading);
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
    // Reflects state without re-emitting modelSelected — this is MainWindow
    // telling the page what's active, not the user picking something.
    const QSignalBlocker blocker(it->radio);
    it->radio->setChecked(true);
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
    bokehStrengthSlider_->setEnabled(!busy_);
}
