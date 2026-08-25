#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>

namespace Theme {

void apply(QApplication& app) {
    const QString activeStyle = app.style() ? app.style()->objectName() : QString();
    if (activeStyle.compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0 &&
        activeStyle.compare(QStringLiteral("windows"), Qt::CaseInsensitive) != 0) {
        return;
    }

    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFile styleFile(QStringLiteral(":/qdarkstyle/dark/style.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QTextStream(&styleFile).readAll());
    }
}

} // namespace Theme
