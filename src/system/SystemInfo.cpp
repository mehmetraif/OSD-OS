#include "SystemInfo.h"
#include "../util/Board.h"
#include "../util/DeviceName.h"
#include "../util/LegacyNames.h"
#include "../util/RootRequest.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QVariantMap>

#include <algorithm>

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <ctime>
#include <initializer_list>
#endif
#ifdef Q_OS_FREEBSD
#include <kenv.h>
#endif
#ifdef Q_OS_OPENBSD
#include <uvm/uvm_param.h>   // VM_UVMEXP
#include <uvm/uvmexp.h>
#endif
#ifdef Q_OS_WIN
#include <QSettings>
#include <windows.h>
#endif

namespace {

// Launcher API 8: osdos-stop names the device, through osdos-name.socket.
constexpr int kNameApi = 8;
// hostnamectl, then avahi started again.
constexpr int kRenameTimeoutMs = 20 * 1000;

// One of the system's files, under OSDOS_SYSTEM_ROOT for tests.
QString systemPath(const QString &path) {
    return legacy::env("SYSTEM_ROOT") + path;
}
QByteArray systemFile(const QString &path) {
    QFile file(systemPath(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
// As text: a device tree's string without the NUL that ends it.
QString systemText(const QString &path) {
    return QString::fromUtf8(systemFile(path)).remove(QChar(u'\0')).trimmed();
}

// A /proc/meminfo line's kilobytes, in bytes; -1 without it.
qint64 meminfoBytes(const QString &meminfo, const QString &key) {
    const QRegularExpression line(QStringLiteral("^%1:\\s+(\\d+) kB").arg(key), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = line.match(meminfo);
    return match.hasMatch() ? match.captured(1).toLongLong() * 1024 : -1;
}

// The processor's name from /proc/cpuinfo: an x86's own, or an ARM core's
// by its part number (a Raspberry Pi's); "" for another.
QString cpuName(const QString &cpuinfo) {
    static const QRegularExpression model(QStringLiteral("^model name\\s*:\\s*(.+)$"), QRegularExpression::MultilineOption);
    if (const QRegularExpressionMatch match = model.match(cpuinfo); match.hasMatch())
        return match.captured(1).simplified();
    static const QRegularExpression part(QStringLiteral("^CPU part\\s*:\\s*0x([0-9a-f]+)$"),
                                         QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    static const QHash<QString, QString> kArmCores = {
        { QStringLiteral("b76"), QStringLiteral("ARM1176") },    // Pi 1, Zero
        { QStringLiteral("c07"), QStringLiteral("Cortex-A7") },  // Pi 2
        { QStringLiteral("d03"), QStringLiteral("Cortex-A53") }, // Pi 3, Zero 2
        { QStringLiteral("d08"), QStringLiteral("Cortex-A72") }, // Pi 4, 400
        { QStringLiteral("d0b"), QStringLiteral("Cortex-A76") }, // Pi 5, 500
    };
    const QRegularExpressionMatch match = part.match(cpuinfo);
    return match.hasMatch() ? kArmCores.value(match.captured(1).toLower()) : QString();
}

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD)
QString sysctlText(const char *name) {
    size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0)
        return QString();
    QByteArray buffer(qsizetype(size), '\0');
    if (sysctlbyname(name, buffer.data(), &size, nullptr, 0) != 0)
        return QString();
    return QString::fromUtf8(buffer.constData()).trimmed();
}
template <typename T>
bool sysctlValue(const char *name, T *value) {
    size_t size = sizeof(T);
    return sysctlbyname(name, value, &size, nullptr, 0) == 0 && size == sizeof(T);
}
#elif defined(Q_OS_OPENBSD)
// OpenBSD's sysctl knows no names, only a MIB.
QString sysctlText(std::initializer_list<int> mib) {
    int name[CTL_MAXNAME] = {};
    std::copy(mib.begin(), mib.end(), name);
    size_t size = 0;
    if (sysctl(name, u_int(mib.size()), nullptr, &size, nullptr, 0) != 0 || size == 0)
        return QString();
    QByteArray buffer(qsizetype(size), '\0');
    if (sysctl(name, u_int(mib.size()), buffer.data(), &size, nullptr, 0) != 0)
        return QString();
    return QString::fromUtf8(buffer.constData()).trimmed();
}
template <typename T>
bool sysctlValue(std::initializer_list<int> mib, T *value) {
    int name[CTL_MAXNAME] = {};
    std::copy(mib.begin(), mib.end(), name);
    size_t size = sizeof(T);
    return sysctl(name, u_int(mib.size()), value, &size, nullptr, 0) == 0 && size == sizeof(T);
}
#endif
#ifdef Q_OS_FREEBSD
// The kernel's environment: what the firmware says of the machine.
QString kenvText(const char *name) {
    char buffer[256] = {};
    if (kenv(KENV_GET, name, buffer, sizeof buffer - 1) < 0)
        return QString();
    return QString::fromLatin1(buffer).trimmed();
}
#endif

// A size as the page says it: "7.9 GB", "238 GB", "512 MB".
QString sizeText(qint64 bytes) {
    const double gb = double(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 100)
        return QStringLiteral("%1 GB").arg(qRound64(gb));
    if (gb >= 1)
        return QStringLiteral("%1 GB").arg(gb, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(qRound64(double(bytes) / (1024.0 * 1024.0)));
}

// A time as the arcade's play time is said: "3D 4H 12M", "4H 12M", "12M".
QString timeText(qint64 seconds) {
    const qint64 minutes = seconds / 60;
    const qint64 days = minutes / (24 * 60);
    const qint64 hours = minutes / 60 % 24;
    if (days > 0)
        return QStringLiteral("%1D %2H %3M").arg(days).arg(hours).arg(minutes % 60);
    if (hours > 0)
        return QStringLiteral("%1H %2M").arg(hours).arg(minutes % 60);
    return QStringLiteral("%1M").arg(minutes);
}

QVariantMap row(const QString &key, const QString &label, const QString &value, const QString &description) {
    return { { QStringLiteral("key"), key }, { QStringLiteral("label"), label },
             { QStringLiteral("value"), value }, { QStringLiteral("description"), description } };
}

} // namespace

SystemInfo::SystemInfo(const QString &dataRoot, std::function<QString()> mediaRoot, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_mediaRoot(std::move(mediaRoot)), m_name(device::name()),
      m_renameAvailable(rootrequest::available(QStringLiteral("name"), kNameApi)) {}

void SystemInfo::refresh() {
    if (!m_fixed) {
        m_fixed.emplace();
        readFixed(&*m_fixed);
    }
    Facts facts = *m_fixed;
    readChanging(&facts, m_dataRoot, m_mediaRoot ? m_mediaRoot() : QString());
    QVariantList rows = rowsOf(facts);
    if (facts.name == m_name && rows == m_rows)
        return;
    m_name = facts.name;
    m_rows = std::move(rows);
    emit changed();
}

SystemInfo::Facts SystemInfo::read(const QString &dataRoot, const QString &mediaRoot) {
    Facts facts;
    readFixed(&facts);
    readChanging(&facts, dataRoot, mediaRoot);
    return facts;
}

void SystemInfo::readFixed(Facts *facts) {
    facts->version = QCoreApplication::applicationVersion();
    facts->arch = QSysInfo::currentCpuArchitecture();
    facts->cores = QThread::idealThreadCount();
    facts->model = board::model();
#if defined(Q_OS_WIN)
    // The PC as its firmware names it, and its processor, from the registry.
    if (facts->model.isEmpty()) {
        const QSettings bios(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
                             QSettings::NativeFormat);
        facts->model = (bios.value(QStringLiteral("SystemManufacturer")).toString() + QLatin1Char(' ')
                        + bios.value(QStringLiteral("SystemProductName")).toString()).simplified();
    }
    const QSettings processor(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                              QSettings::NativeFormat);
    facts->cpu = processor.value(QStringLiteral("ProcessorNameString")).toString().simplified();
    MEMORYSTATUSEX memory {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        facts->memoryTotal = qint64(memory.ullTotalPhys);
#elif defined(Q_OS_MACOS)
    if (facts->model.isEmpty())
        facts->model = sysctlText("hw.model");
    facts->cpu = sysctlText("machdep.cpu.brand_string");
    quint64 memory = 0;
    if (sysctlValue("hw.memsize", &memory))
        facts->memoryTotal = qint64(memory);
#elif defined(Q_OS_FREEBSD)
    if (facts->model.isEmpty()) {
        // The PC as its firmware names it.
        facts->model = (kenvText("smbios.system.maker") + QLatin1Char(' ') + kenvText("smbios.system.product")).simplified();
        facts->serial = kenvText("smbios.system.serial");
    }
    facts->cpu = sysctlText("hw.model");
    unsigned long memory = 0;
    if (sysctlValue("hw.physmem", &memory))
        facts->memoryTotal = qint64(memory);
#elif defined(Q_OS_OPENBSD)
    if (facts->model.isEmpty())
        facts->model = (sysctlText({ CTL_HW, HW_VENDOR }) + QLatin1Char(' ') + sysctlText({ CTL_HW, HW_PRODUCT })).simplified();
    facts->cpu = sysctlText({ CTL_HW, HW_MODEL });
    int64_t memory = 0;
    if (sysctlValue({ CTL_HW, HW_PHYSMEM64 }, &memory))
        facts->memoryTotal = qint64(memory);
#else
    if (facts->model.isEmpty()) {
        // A PC's, as its firmware names it.
        const QString vendor = systemText(QStringLiteral("/sys/class/dmi/id/sys_vendor"));
        const QString product = systemText(QStringLiteral("/sys/class/dmi/id/product_name"));
        facts->model = (vendor + QLatin1Char(' ') + product).trimmed();
    }
    facts->serial = systemText(QStringLiteral("/proc/device-tree/serial-number"));
    facts->cpu = cpuName(QString::fromUtf8(systemFile(QStringLiteral("/proc/cpuinfo"))));
    facts->memoryTotal = meminfoBytes(QString::fromUtf8(systemFile(QStringLiteral("/proc/meminfo"))),
                                      QStringLiteral("MemTotal"));
    // The OS and its kernel from the files QSysInfo reads them from, so
    // that a stand-in can name them.
    static const QRegularExpression pretty(QStringLiteral("^PRETTY_NAME=\"?([^\"\\n]*)\"?$"),
                                           QRegularExpression::MultilineOption);
    for (const QString &release : { QStringLiteral("/etc/os-release"), QStringLiteral("/usr/lib/os-release") }) {
        const QRegularExpressionMatch match = pretty.match(QString::fromUtf8(systemFile(release)));
        if (match.hasMatch()) {
            facts->system = match.captured(1).trimmed();
            break;
        }
    }
    if (const QString release = systemText(QStringLiteral("/proc/sys/kernel/osrelease")); !release.isEmpty())
        facts->kernel = QStringLiteral("Linux ") + release;
#endif
    if (facts->system.isEmpty())
        facts->system = QSysInfo::prettyProductName();
    if (facts->kernel.isEmpty()) {
        QString kernel = QSysInfo::kernelType();
        if (!kernel.isEmpty())
            kernel[0] = kernel[0].toUpper();
        facts->kernel = kernel + QLatin1Char(' ') + QSysInfo::kernelVersion();
    }
}

void SystemInfo::readChanging(Facts *facts, const QString &dataRoot, const QString &mediaRoot) {
    facts->name = device::name();
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX memory {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        facts->memoryFree = qint64(memory.ullAvailPhys);
    facts->uptime = qint64(GetTickCount64() / 1000);
#elif defined(Q_OS_MACOS)
    timeval boot {};
    if (sysctlValue("kern.boottime", &boot) && boot.tv_sec > 0)
        facts->uptime = qint64(std::time(nullptr)) - qint64(boot.tv_sec);
#elif defined(Q_OS_FREEBSD)
    timeval boot {};
    if (sysctlValue("kern.boottime", &boot) && boot.tv_sec > 0)
        facts->uptime = qint64(std::time(nullptr)) - qint64(boot.tv_sec);
    // The pages free, and the inactive ones the kernel gives up at once.
    unsigned int freePages = 0, inactivePages = 0;
    int pageSize = 0;
    if (sysctlValue("vm.stats.vm.v_free_count", &freePages) && sysctlValue("hw.pagesize", &pageSize)) {
        sysctlValue("vm.stats.vm.v_inactive_count", &inactivePages);
        facts->memoryFree = (qint64(freePages) + qint64(inactivePages)) * pageSize;
    }
    // The processor's, in tenths of a kelvin (coretemp, amdtemp).
    int kelvinTenths = 0;
    if (sysctlValue("dev.cpu.0.temperature", &kelvinTenths) && kelvinTenths > 0)
        facts->temperature = kelvinTenths / 10.0 - 273.15;
#elif defined(Q_OS_OPENBSD)
    timeval boot {};
    if (sysctlValue({ CTL_KERN, KERN_BOOTTIME }, &boot) && boot.tv_sec > 0)
        facts->uptime = qint64(std::time(nullptr)) - qint64(boot.tv_sec);
    struct uvmexp vm {};
    if (sysctlValue({ CTL_VM, VM_UVMEXP }, &vm))
        facts->memoryFree = qint64(vm.free) * vm.pagesize;
#else
    facts->memoryFree = meminfoBytes(QString::fromUtf8(systemFile(QStringLiteral("/proc/meminfo"))),
                                     QStringLiteral("MemAvailable"));
    bool known = false;
    const double up = systemText(QStringLiteral("/proc/uptime")).section(QLatin1Char(' '), 0, 0).toDouble(&known);
    if (known)
        facts->uptime = qint64(up);
    // The processor's, in thousandths of a degree.
    const int milli = systemText(QStringLiteral("/sys/class/thermal/thermal_zone0/temp")).toInt(&known);
    if (known)
        facts->temperature = milli / 1000.0;
#endif

    const QStorageInfo data(dataRoot);
    if (data.isValid() && data.isReady()) {
        facts->storagePath = dataRoot;
        facts->storageTotal = data.bytesTotal();
        facts->storageFree = data.bytesAvailable();
    }
    // The media folder's own drive: the image's film partition, a USB drive.
    if (!mediaRoot.isEmpty() && QFileInfo::exists(mediaRoot)) {
        const QStorageInfo media(mediaRoot);
        if (media.isValid() && media.isReady() && (!data.isValid() || media.device() != data.device())) {
            facts->mediaPath = mediaRoot;
            facts->mediaTotal = media.bytesTotal();
            facts->mediaFree = media.bytesAvailable();
        }
    }

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QString name = iface.name();
#if defined(Q_OS_LINUX)
        // A device's own, not a bridge's, a tunnel's or a container's.
        const QString sys = QStringLiteral("/sys/class/net/") + name;
        if (!QFileInfo::exists(systemPath(sys + QStringLiteral("/device"))))
            continue;
        const bool wifi = iface.type() == QNetworkInterface::Wifi
                          || QFileInfo::exists(systemPath(sys + QStringLiteral("/wireless")))
                          || QFileInfo::exists(systemPath(sys + QStringLiteral("/phy80211")));
#elif defined(Q_OS_MACOS)
        // en0, en1: the Mac's own, not its tunnels' and bridges'.
        if (!name.startsWith(QLatin1String("en")))
            continue;
        const bool wifi = iface.type() == QNetworkInterface::Wifi;
#else
        const bool wifi = iface.type() == QNetworkInterface::Wifi;
        if (!wifi && iface.type() != QNetworkInterface::Ethernet)
            continue;
#endif
        Connection connection;
        connection.name = name;
        connection.wifi = wifi;
        connection.mac = iface.hardwareAddress();
        if (flags.testFlag(QNetworkInterface::IsUp) && flags.testFlag(QNetworkInterface::IsRunning)) {
            const QList<QNetworkAddressEntry> entries = iface.addressEntries();
            for (const QNetworkAddressEntry &entry : entries) {
                const QHostAddress ip = entry.ip();
                if (ip.protocol() == QAbstractSocket::IPv4Protocol)
                    connection.ipv4 << ip.toString();
                else if (ip.protocol() == QAbstractSocket::IPv6Protocol && !ip.isLinkLocal())
                    connection.ipv6 << ip.toString();
            }
        }
        facts->connections << connection;
    }
    // Ethernet first, then Wi-Fi.
    std::sort(facts->connections.begin(), facts->connections.end(), [](const Connection &a, const Connection &b) {
        return a.wifi != b.wifi ? b.wifi : a.name < b.name;
    });
}

QVariantList SystemInfo::rowsOf(const Facts &facts) {
    QVariantList rows;
    rows << row(QStringLiteral("name"), QStringLiteral("Device Name"), facts.name,
                QStringLiteral("This device's name on the network: what a router lists it under, and others reach it at"
                               " as %1.local • LAN Play hosts its games under it, unless Online Name says another")
                    .arg(facts.name));
    if (!facts.model.isEmpty()) {
        // Without a Pi's revision, which the help line has, so that it fits
        // its line.
        static const QRegularExpression revision(QStringLiteral(" Rev [0-9.]+$"));
        rows << row(QStringLiteral("model"), QStringLiteral("Model"), QString(facts.model).remove(revision),
                    QStringLiteral("The board or computer OSD/OS runs on: ") + facts.model
                        + (facts.serial.isEmpty() ? QString() : QStringLiteral(" • Serial number ") + facts.serial));
    }
    rows << row(QStringLiteral("system"), QStringLiteral("OS"), facts.system,
                QStringLiteral("OSD/OS %1 • %2").arg(facts.version, facts.kernel));
    const QString cores = facts.cores == 1 ? QStringLiteral("1 core") : QStringLiteral("%1 cores").arg(facts.cores);
    rows << row(QStringLiteral("processor"), QStringLiteral("Processor"),
                facts.cpu.isEmpty() ? facts.arch + QStringLiteral(", ") + cores : facts.cpu + QStringLiteral(", ") + cores,
                QStringLiteral("Its kind and how many cores it has • %1").arg(facts.arch));
    if (facts.memoryTotal > 0)
        rows << row(QStringLiteral("memory"), QStringLiteral("Memory"), sizeText(facts.memoryTotal),
                    facts.memoryFree >= 0 ? sizeText(facts.memoryFree) + QStringLiteral(" free now")
                                          : QStringLiteral("All the memory the system has"));
    if (facts.storageTotal > 0)
        rows << row(QStringLiteral("storage"), QStringLiteral("Storage"), sizeText(facts.storageFree) + QStringLiteral(" free"),
                    QStringLiteral("Of %1, where OSD/OS keeps its settings and what it downloads (%2)")
                        .arg(sizeText(facts.storageTotal), facts.storagePath));
    if (facts.mediaTotal > 0)
        rows << row(QStringLiteral("media"), QStringLiteral("Media Storage"), sizeText(facts.mediaFree) + QStringLiteral(" free"),
                    QStringLiteral("Of %1, where the films and games are (%2)").arg(sizeText(facts.mediaTotal), facts.mediaPath));
    if (facts.temperature)
        rows << row(QStringLiteral("temperature"), QStringLiteral("Temperature"),
                    QStringLiteral("%1 °C").arg(qRound(*facts.temperature)),
                    QStringLiteral("The processor's, read again every few seconds"));
    if (facts.uptime >= 0)
        rows << row(QStringLiteral("uptime"), QStringLiteral("Up For"), timeText(facts.uptime),
                    QStringLiteral("Since the system last started"));

    rows << QVariantMap{ { QStringLiteral("type"), QStringLiteral("section") }, { QStringLiteral("label"), QStringLiteral("Network") } };
    if (facts.connections.isEmpty())
        rows << row(QStringLiteral("none"), QStringLiteral("No Network"), QString(),
                    QStringLiteral("This device has no Ethernet or Wi-Fi of its own"));
    // A second of a kind as "Ethernet 2".
    int seen[2] = { 0, 0 };
    for (const Connection &connection : facts.connections) {
        const int nth = ++seen[connection.wifi];
        const QString label = (connection.wifi ? QStringLiteral("Wi-Fi") : QStringLiteral("Ethernet"))
                              + (nth > 1 ? QStringLiteral(" %1").arg(nth) : QString());
        const QStringList addresses = connection.ipv4 + connection.ipv6;
        rows << row(QStringLiteral("ip:") + connection.name, label + QStringLiteral(" IP"),
                    connection.ipv4.isEmpty() ? (connection.ipv6.isEmpty() ? QStringLiteral("Not Connected") : connection.ipv6.first())
                                              : connection.ipv4.first(),
                    addresses.isEmpty()
                        ? QStringLiteral("%1 has no address: not connected, or not given one yet").arg(connection.name)
                        : QStringLiteral("%1's addresses: %2").arg(connection.name, addresses.join(QStringLiteral(", "))));
        rows << row(QStringLiteral("mac:") + connection.name, label + QStringLiteral(" MAC"), connection.mac,
                    QStringLiteral("%1's hardware address, by which a router can keep the same IP address for this device")
                        .arg(connection.name));
    }
    return rows;
}

QString SystemInfo::hostnameFor(const QString &typed, QString *why) {
    static const QRegularExpression spaces(QStringLiteral("[\\s_]+"));
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
    // In small letters, as host names are written.
    QString name = typed.trimmed().toLower();
    name.replace(spaces, QStringLiteral("-"));
    if (valid.match(name).hasMatch())
        return name;
    if (why) {
        *why = name.isEmpty()      ? QStringLiteral("No name typed")
             : name.size() > 63    ? QStringLiteral("A name has 63 letters at most")
                                   : QStringLiteral("Letters, digits and hyphens only, and no hyphen first or last");
    }
    return QString();
}

bool SystemInfo::rename(const QString &typed) {
    if (!m_renameAvailable || m_renaming)
        return false;
    QString why;
    const QString name = hostnameFor(typed, &why);
    if (name.isEmpty()) {
        setMessage(why);
        return false;
    }
    if (name == m_name) {
        setMessage(QStringLiteral("It is called %1 already").arg(name));
        return false;
    }
    clearMessage();
    m_renaming = true;
    emit renamingChanged();
    // osdos-stop device-name, as root, behind the socket: the name on a
    // line, "ok" or why not back.
    rootrequest::ask(this, QStringLiteral("name"), name.toLatin1(), kRenameTimeoutMs,
                     [this, name](const rootrequest::Answer &answer) {
        m_renaming = false;
        emit renamingChanged();
        // As the system has it now, whatever the answer.
        refresh();
        if (answer.ok) {
            setMessage(QStringLiteral("Now called %1").arg(m_name));
        } else {
            qWarning("[SystemInfo] Not renamed %s: %s", qPrintable(name), qPrintable(answer.why));
            setMessage(QStringLiteral("Couldn't rename it: ") + answer.why);
        }
    });
    return true;
}

void SystemInfo::clearMessage() {
    setMessage(QString());
}

void SystemInfo::setMessage(const QString &text) {
    if (m_message == text)
        return;
    m_message = text;
    emit messageChanged();
}
