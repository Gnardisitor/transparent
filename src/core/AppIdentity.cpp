#include "AppIdentity.h"

#include <QCoreApplication>

namespace AppIdentity {

void init() {
    QCoreApplication::setOrganizationName(QStringLiteral("transparent"));
    // Only used by macOS, reversed into the preferences identifier
    // com.transparent.transparent.
    QCoreApplication::setOrganizationDomain(QStringLiteral("transparent.com"));
    QCoreApplication::setApplicationName(QStringLiteral("transparent"));
}

} // namespace AppIdentity
