#pragma once

#include <QString>

// Whether a module is on before anyone turns it on or off: its manifest's
// "enabled" toggle's default, on unless that says OFF (a toggle that names
// no default is on, as is a module without the toggle). The main menu
// (AppCore) and Export to OSD/OS Setup (util/SetupExport) both go by this,
// so the export leaves out only what a card set up from it would have
// anyway.
inline bool moduleEnabledByDefault(const QString &manifestDefault) {
    return manifestDefault.toUpper() != QLatin1String("OFF");
}
