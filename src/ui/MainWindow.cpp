#include "MainWindow.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {

const QStringList kSupportedExtensions = {"png", "jpg", "jpeg", "bmp", "webp"};

bool isSupportedImageUrl(const QUrl& url) {
    if (!url.isLocalFile()) {
        return false;
    }
    const QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
    return kSupportedExtensions.contains(suffix);
}

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
    : QMainWindow(parent), pipeline_(std::move(pipeline)) {
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("transparent"));

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    if (pipeline_ && pipeline_->stepCount() > 0) {
        statusLabel_->setText(QStringLiteral("Drop an image here"));
    } else {
        statusLabel_->setText(
            QStringLiteral("No background-removal model loaded — check models/ (see README)"));
        statusLabel_->setStyleSheet(QStringLiteral("color: #b00;"));
    }

    previewLabel_ = new QLabel(this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(400, 300);
    previewLabel_->setFrameShape(QFrame::StyledPanel);

    exportButton_ = new QPushButton(QStringLiteral("Export PNG..."), this);
    exportButton_->setEnabled(false);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::onExportClicked);

    layout->addWidget(statusLabel_);
    layout->addWidget(previewLabel_, 1);
    layout->addWidget(exportButton_);

    setCentralWidget(central);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls() && isSupportedImageUrl(mime->urls().first())) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) {
        return;
    }
    const QUrl url = mime->urls().first();
    if (!isSupportedImageUrl(url)) {
        return;
    }
    loadImage(url.toLocalFile());
    event->acceptProposedAction();
}

void MainWindow::loadImage(const QString& path) {
    QImage source(path);
    if (source.isNull()) {
        statusLabel_->setText(QStringLiteral("Could not read image: %1").arg(path));
        return;
    }

    statusLabel_->setText(QStringLiteral("Processing..."));
    QApplication::processEvents();

    resultImage_ = pipeline_ ? pipeline_->run(source) : source;

    updatePreview();
    exportButton_->setEnabled(!resultImage_.isNull());
    statusLabel_->setText(QStringLiteral("Done"));
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
