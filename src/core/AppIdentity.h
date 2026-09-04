// Sets the application identity used by QSettings and QStandardPaths.
// Call before any QSettings is constructed: the default constructor needs
// organizationName set, otherwise QSettings goes to AccessError and silently
// drops writes (the Windows registry backend refuses everything).
#pragma once

namespace AppIdentity {

// Organization domain 'transparent.com' exists only so Qt's macOS QSettings
// backend derives the preferences identifier com.transparent.transparent,
// matching the app bundle identifier.
void init();

} // namespace AppIdentity
