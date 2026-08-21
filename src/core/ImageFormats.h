#pragma once

#include <QString>
#include <QStringList>

// The single list of image extensions this app knows how to open, shared by
// MainWindow's single-file drop handling and BatchRunner's folder scan so
// the two paths can't silently drift apart.
namespace ImageFormats {

QStringList supportedExtensions();

// True if filePath's suffix (case-insensitive) is one of supportedExtensions().
bool isSupported(const QString& filePath);

} // namespace ImageFormats
