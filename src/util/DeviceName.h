#pragma once
#include "LegacyNames.h"

#include <QSysInfo>

namespace device {

// This device's name: the system's host name, what a router lists it under
// and others reach it at (<name>.local), without a Mac's ".local".
// OSDOS_DEVICE_NAME stands in for it, for tests.
inline QString name() {
    QString name = legacy::env("DEVICE_NAME");
    if (name.isEmpty())
        name = QSysInfo::machineHostName();
    if (name.endsWith(QLatin1String(".local")))
        name.chop(6);
    return name;
}

} // namespace device
