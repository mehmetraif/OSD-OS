// What a view says under "… is not installed" (util/InstallHint.h): the
// system from its os-release, and the hint for each program on each kind of
// system.
#include "util/InstallHint.h"

#include <QtTest>

class InstallHintTest : public QObject {
    Q_OBJECT

private slots:
    // A Linux from its os-release: Raspberry Pi OS is Debian's on a Pi; a
    // distribution like another installs as it does; one of a kind not known
    // here keeps its name with no installer.
    void linuxSystems() {
        const QString debian = QStringLiteral("PRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\nNAME=\"Debian GNU/Linux\"\n"
                                              "VERSION_ID=\"13\"\nID=debian\n");
        installhint::System pi = installhint::linuxSystem(debian, true);
        QCOMPARE(pi.name, QStringLiteral("Raspberry Pi OS"));
        QCOMPARE(pi.installer, QStringLiteral("apt"));
        installhint::System plain = installhint::linuxSystem(debian, false);
        QCOMPARE(plain.name, QStringLiteral("Debian GNU/Linux"));
        QCOMPARE(plain.installer, QStringLiteral("apt"));

        installhint::System mint = installhint::linuxSystem(
            QStringLiteral("NAME=\"Linux Mint\"\nID=linuxmint\nID_LIKE=\"ubuntu debian\"\n"), false);
        QCOMPARE(mint.name, QStringLiteral("Linux Mint"));
        QCOMPARE(mint.installer, QStringLiteral("apt"));

        installhint::System rocky = installhint::linuxSystem(
            QStringLiteral("NAME=\"Rocky Linux\"\nID=\"rocky\"\nID_LIKE=\"rhel centos fedora\"\n"), false);
        QCOMPARE(rocky.installer, QStringLiteral("dnf"));
        QCOMPARE(installhint::linuxSystem(QStringLiteral("NAME=\"Fedora Linux\"\nID=fedora\n"), false).installer,
                 QStringLiteral("dnf"));

        installhint::System manjaro = installhint::linuxSystem(
            QStringLiteral("NAME=\"Manjaro Linux\"\nID=manjaro\nID_LIKE=arch\n"), false);
        QCOMPARE(manjaro.installer, QStringLiteral("pacman"));
        QCOMPARE(installhint::linuxSystem(QStringLiteral("NAME=\"Arch Linux\"\nID=arch\n"), false).installer,
                 QStringLiteral("pacman"));

        installhint::System suse = installhint::linuxSystem(
            QStringLiteral("NAME=\"openSUSE Tumbleweed\"\nID=\"opensuse-tumbleweed\"\nID_LIKE=\"opensuse suse\"\n"), false);
        QCOMPARE(suse.name, QStringLiteral("openSUSE Tumbleweed"));
        QCOMPARE(suse.installer, QStringLiteral("zypper"));

        installhint::System other = installhint::linuxSystem(
            QStringLiteral("NAME='Void'\nID=void\n"), false);
        QCOMPARE(other.name, QStringLiteral("Void"));
        QCOMPARE(other.installer, QString());
        // A Pi that isn't Debian's (Ubuntu) is named as itself.
        installhint::System ubuntuPi = installhint::linuxSystem(
            QStringLiteral("NAME=\"Ubuntu\"\nID=ubuntu\nID_LIKE=debian\n"), true);
        QCOMPARE(ubuntuPi.name, QStringLiteral("Ubuntu"));
        QCOMPARE(ubuntuPi.installer, QStringLiteral("apt"));
        // Nothing readable: Linux, with no installer.
        installhint::System none = installhint::linuxSystem(QString(), false);
        QCOMPARE(none.name, QStringLiteral("Linux"));
        QCOMPARE(none.installer, QString());
    }

    // The hint on each kind of system, for each program.
    void hints() {
        using installhint::forProgram;
        const installhint::System pi{ QStringLiteral("Raspberry Pi OS"), QStringLiteral("apt") };
        QCOMPARE(forProgram(QStringLiteral("mame"), pi), QStringLiteral("On Raspberry Pi OS: sudo apt install mame"));
        QCOMPARE(forProgram(QStringLiteral("retroarch"), pi), QStringLiteral("On Raspberry Pi OS: sudo apt install retroarch"));
        QCOMPARE(forProgram(QStringLiteral("yt-dlp"), pi), QStringLiteral("On Raspberry Pi OS: sudo apt install yt-dlp"));
        QCOMPARE(forProgram(QStringLiteral("mpv"), pi), QStringLiteral("On Raspberry Pi OS: sudo apt install mpv"));
        QCOMPARE(forProgram(QStringLiteral("browser"), pi),
                 QStringLiteral("On Raspberry Pi OS: sudo apt install chromium libwidevinecdm0 cage wtype"));
        QCOMPARE(forProgram(QStringLiteral("cage"), pi), QStringLiteral("On Raspberry Pi OS: sudo apt install cage wtype"));

        const installhint::System ubuntu{ QStringLiteral("Ubuntu"), QStringLiteral("apt") };
        QCOMPARE(forProgram(QStringLiteral("browser"), ubuntu), QStringLiteral("On Ubuntu: sudo apt install chromium"));

        const installhint::System mac{ QStringLiteral("macOS"), QStringLiteral("brew") };
        QCOMPARE(forProgram(QStringLiteral("mame"), mac), QStringLiteral("On macOS: brew install mame"));
        QCOMPARE(forProgram(QStringLiteral("retroarch"), mac), QStringLiteral("On macOS: brew install --cask retroarch"));
        QCOMPARE(forProgram(QStringLiteral("yt-dlp"), mac), QStringLiteral("On macOS: brew install yt-dlp"));
        QCOMPARE(forProgram(QStringLiteral("mpv"), mac), QStringLiteral("On macOS: brew install mpv"));
        QCOMPARE(forProgram(QStringLiteral("browser"), mac), QStringLiteral("On macOS: brew install --cask google-chrome"));
        QCOMPARE(forProgram(QStringLiteral("cage"), mac), QString());

        const installhint::System freebsd{ QStringLiteral("FreeBSD"), QStringLiteral("pkg") };
        QCOMPARE(forProgram(QStringLiteral("mame"), freebsd), QStringLiteral("On FreeBSD: pkg install mame, as root"));
        QCOMPARE(forProgram(QStringLiteral("retroarch"), freebsd), QStringLiteral("On FreeBSD: pkg install retroarch, as root"));
        QCOMPARE(forProgram(QStringLiteral("browser"), freebsd), QStringLiteral("On FreeBSD: pkg install chromium, as root"));

        const installhint::System openbsd{ QStringLiteral("OpenBSD"), QStringLiteral("pkg_add") };
        QCOMPARE(forProgram(QStringLiteral("mame"), openbsd), QStringLiteral("On OpenBSD: doas pkg_add mame"));
        QCOMPARE(forProgram(QStringLiteral("mpv"), openbsd), QStringLiteral("On OpenBSD: doas pkg_add mpv"));
        QCOMPARE(forProgram(QStringLiteral("cage"), openbsd), QStringLiteral("On OpenBSD: doas pkg_add cage wtype"));

        const installhint::System fedora{ QStringLiteral("Fedora Linux"), QStringLiteral("dnf") };
        QCOMPARE(forProgram(QStringLiteral("mame"), fedora), QStringLiteral("On Fedora Linux: sudo dnf install mame (from RPM Fusion)"));
        QCOMPARE(forProgram(QStringLiteral("yt-dlp"), fedora), QStringLiteral("On Fedora Linux: sudo dnf install yt-dlp"));
        const installhint::System arch{ QStringLiteral("Arch Linux"), QStringLiteral("pacman") };
        QCOMPARE(forProgram(QStringLiteral("retroarch"), arch), QStringLiteral("On Arch Linux: sudo pacman -S retroarch"));
        const installhint::System suse{ QStringLiteral("openSUSE Tumbleweed"), QStringLiteral("zypper") };
        QCOMPARE(forProgram(QStringLiteral("mpv"), suse), QStringLiteral("On openSUSE Tumbleweed: sudo zypper install mpv"));
        const installhint::System other{ QStringLiteral("Void"), QString() };
        QCOMPARE(forProgram(QStringLiteral("mame"), other),
                 QStringLiteral("On Void: install mame with the distribution's package manager"));

        const installhint::System windows{ QStringLiteral("Windows"), QStringLiteral("windows") };
        QVERIFY(forProgram(QStringLiteral("mame"), windows).startsWith(QStringLiteral("On Windows: Settings → Arcade → MAME downloads it")));
        QVERIFY(forProgram(QStringLiteral("retroarch"), windows)
                    .startsWith(QStringLiteral("On Windows: Settings → Arcade → FinalBurn Neo downloads RetroArch")));
        QVERIFY(forProgram(QStringLiteral("yt-dlp"), windows).contains(QStringLiteral("yt-dlp.exe")));
        QVERIFY(forProgram(QStringLiteral("mpv"), windows).contains(QStringLiteral("mpv.io")));
        QVERIFY(forProgram(QStringLiteral("browser"), windows).contains(QStringLiteral("not on Windows")));
        QCOMPARE(forProgram(QStringLiteral("cage"), windows), QString());
    }

    // On this machine: a hint that names the system and the program.
    void here() {
        const QString hint = installhint::forProgram(QStringLiteral("mame"));
        QVERIFY(hint.startsWith(QStringLiteral("On ")));
        QVERIFY(hint.contains(QStringLiteral("mame"), Qt::CaseInsensitive));
        QCOMPARE(installhint::current().name, installhint::current().name);
    }
};

QTEST_GUILESS_MAIN(InstallHintTest)
#include "install_hint_test.moc"
