#include "RetroAchievementsAccount.h"
#include "../../util/AtomicFile.h"
#include "../../util/ProcessOver.h"
#include "RetroAchievements.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <utility>

namespace {

// What a file holding a token or a password is written with.
constexpr QFileDevice::Permissions kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
// A sign-in RetroArch hasn't said how it went in this long is stopped (its
// token read all the same), and one stopped is killed after this.
constexpr int kSignInTimeoutMs = 20000;
constexpr int kSignInStopMs = 5000;
// An account OSD/OS Setup brought is checked as OSD/OS starts, when the
// network may not be up yet: while RetroAchievements can't be reached, again
// this much later, this many times in all.
constexpr int kSetupRetryMs = 30000;
constexpr int kSetupChecks = 10;

} // namespace

RetroAchievementsAccount::RetroAchievementsAccount(const QString &home, RetroAchievements *retroAchievements,
                                                   std::function<QString()> retroarchPath,
                                                   std::function<bool()> running, QObject *parent)
    : QObject(parent), m_home(home), m_retroAchievements(retroAchievements),
      m_retroarchPath(std::move(retroarchPath)), m_running(std::move(running))
{
    // The account, without its password, which is kept nowhere but in OSD/OS
    // Setup's file until RetroAchievements has had it (takeSetupAccount()).
    QFile account(m_home + QStringLiteral("/retroachievements.json"));
    if (account.open(QIODevice::ReadOnly)) {
        const QJsonObject kept = QJsonDocument::fromJson(account.readAll()).object();
        m_raUser = kept.value(QStringLiteral("user")).toString();
        m_raToken = kept.value(QStringLiteral("token")).toString();
        m_raKey = kept.value(QStringLiteral("key")).toString();
    }
    m_retroAchievements->setKey(m_raKey);
    m_retroAchievements->setUser(m_raUser);
    connect(m_retroAchievements, &RetroAchievements::keyRefused, this, [this] {
        if (!std::exchange(m_raKeyRefused, true))
            emit changed();
    });
    // An answer that came with the key: one refused before (a guard's page
    // taken for a refusal, or the account put right) is good again.
    connect(m_retroAchievements, &RetroAchievements::keyAccepted, this, [this] {
        m_raKeyRefused = false;
        emit changed();
    });
    // A game's config is written anew for each game: one a game left that the
    // app didn't see end (it was stopped meanwhile) may hold the password, as
    // may a sign-in's.
    QFile::remove(retroarchHome() + QStringLiteral("/retroarch.cfg"));
    QDir(signInDir()).removeRecursively();
    m_signInCore = QCoreApplication::applicationDirPath() + QStringLiteral("/osdos_signin_libretro.so");
    m_setupRetryMs = kSetupRetryMs;
}

RetroAchievementsAccount::~RetroAchievementsAccount() {
    if (QProcess *signIn = m_signIn) {
        stopSignIn();
        signIn->waitForFinished(1000);
    }
}

QString RetroAchievementsAccount::message() const {
    if (state() == QLatin1String("failed") && m_raFailure.isEmpty())
        return QStringLiteral("Sign in again: the password is only kept until the next game");
    return m_raFailure;
}

QString RetroAchievementsAccount::state() const {
    if (m_raUser.isEmpty())
        return QStringLiteral("none");
    if (!m_raToken.isEmpty())
        return QStringLiteral("signedIn");
    if (m_signIn)
        return QStringLiteral("checking");
    if (!m_raPassword.isEmpty())
        return QStringLiteral("pending");
    return QStringLiteral("failed");
}

void RetroAchievementsAccount::signIn(const QString &user, const QString &password) {
    if (user.trimmed().isEmpty() || password.isEmpty())
        return;
    // In place of an account OSD/OS Setup brought.
    forgetSetupAccount();
    beginSignIn(user, password);
}

void RetroAchievementsAccount::takeSetupAccount() {
    QFile file(setupAccountPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject setup = QJsonDocument::fromJson(file.read(64 * 1024)).object();
    file.close();
    const QString key = setup.value(QStringLiteral("key")).toString().trimmed();
    const QString user = setup.value(QStringLiteral("user")).toString().trimmed();
    const QString password = setup.value(QStringLiteral("password")).toString();
    const QString token = setup.value(QStringLiteral("token")).toString();
    if (!key.isEmpty() && key != m_raKey)
        setKey(key);
    // A device's sign-in (Export to OSD/OS Setup): taken as it is, as one
    // RetroArch gave this device; the next game's RetroArch says if it no
    // longer works.
    if (!user.isEmpty() && password.isEmpty() && !token.isEmpty()) {
        if (token != m_raToken || user.compare(m_raUser, Qt::CaseInsensitive) != 0) {
            qInfo("[Arcade] RetroAchievements sign-in from OSD/OS Setup: %s", qPrintable(user));
            stopSignIn();
            m_raUser = user;
            m_raToken = token;
            m_raPassword.clear();
            m_raFailure.clear();
            m_retroAchievements->setUser(m_raUser);
            save();
            emit changed();
        }
        forgetSetupAccount();
        return;
    }
    // Without a password, or for the account signed in already: its key
    // alone (a file brought again keeps the token that works).
    if (user.isEmpty() || password.isEmpty()
        || (!m_raToken.isEmpty() && user.compare(m_raUser, Qt::CaseInsensitive) == 0)) {
        forgetSetupAccount();
        return;
    }
    qInfo("[Arcade] RetroAchievements account from OSD/OS Setup: %s", qPrintable(user));
    m_raFromSetup = true;
    m_setupChecks = 1;
    beginSignIn(user, password);
    // No check started at all (no RetroArch, or its sign-in core): none ever
    // will be, and the password waits in memory alone, for the next game.
    if (!m_signIn)
        forgetSetupAccount();
}

void RetroAchievementsAccount::forgetSetupAccount() {
    m_raFromSetup = false;
    m_setupChecks = 0;
    QFile::remove(setupAccountPath());
}

void RetroAchievementsAccount::beginSignIn(const QString &user, const QString &password) {
    const QString name = user.trimmed();
    stopSignIn();
    m_raUser = name;
    m_raToken.clear();
    m_raPassword = password;
    m_raFailure.clear();
    m_retroAchievements->setUser(m_raUser);
    save();
    checkSignIn();
    emit changed();
}

void RetroAchievementsAccount::signOut() {
    stopSignIn();
    forgetSetupAccount();
    m_raUser.clear();
    m_raToken.clear();
    m_raPassword.clear();
    m_raFailure.clear();
    m_retroAchievements->setUser(QString());
    save();
    emit changed();
}

void RetroAchievementsAccount::checkSignIn() {
    const QString program = m_retroarchPath();
    if (program.isEmpty() || !QFileInfo(m_signInCore).isFile())
        return;
    const QString dir = signInDir();
    if (!QDir().mkpath(dir))
        return;
    // What it keeps there, the password in its config, is no one else's.
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    libretro::SignIn signIn;
    signIn.core = m_signInCore;
    signIn.content = dir + QStringLiteral("/sign-in.osdos");
    signIn.config = dir + QStringLiteral("/retroarch.cfg");
    signIn.dir = dir;
    signIn.user = m_raUser;
    signIn.password = m_raPassword;
    if (!writeFileAtomically(signIn.content, QByteArrayLiteral("OSD/OS"), kOwnerOnly)
        || !writeFileAtomically(signIn.config, libretro::signInConfigText(signIn).toUtf8(), kOwnerOnly))
        return;

    m_signInResult = libretro::SignInLine::None;
    m_signInFailure.clear();
    m_signIn = new QProcess(this);
    m_signIn->setProgram(program);
    m_signIn->setArguments(libretro::signInArguments(signIn));
    m_signIn->setWorkingDirectory(dir);
    // Its log, on its output and its errors alike; and its own folder kept
    // with the rest (XDG_CONFIG_HOME), not the user's RetroArch's.
    m_signIn->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_CONFIG_HOME"), dir + QStringLiteral("/config"));
    m_signIn->setProcessEnvironment(environment);
    connect(m_signIn, &QProcess::readyReadStandardOutput, this, &RetroAchievementsAccount::readSignIn);
    whenOver(m_signIn, this, [this](const ProcessEnd &) { signInEnded(); });
    // Not said how it went in time: stopped, and what it saved read all the
    // same; and killed if even that doesn't end it. Once it has said, it is
    // being stopped already: a second SIGTERM would end it unsaved.
    QProcess *process = m_signIn;
    QTimer::singleShot(kSignInTimeoutMs, process, [this, process] {
        if (m_signInResult != libretro::SignInLine::None)
            return;
        process->terminate();
        QTimer::singleShot(kSignInStopMs, process, &QProcess::kill);
    });
    qInfo("[Arcade] RetroArch signs in as %s to check it", qPrintable(m_raUser));
    m_signIn->start();
}

void RetroAchievementsAccount::readSignIn() {
    while (m_signIn && m_signIn->canReadLine()) {
        const QString line = QString::fromUtf8(m_signIn->readLine());
        if (m_signInResult != libretro::SignInLine::None)
            continue;
        m_signInResult = libretro::readSignInLine(line.trimmed(), &m_signInFailure);
        // Said: RetroArch ends, saving the token if it has one.
        if (m_signInResult != libretro::SignInLine::None) {
            m_signIn->terminate();
            QTimer::singleShot(kSignInStopMs, m_signIn, &QProcess::kill);
        }
    }
}

void RetroAchievementsAccount::signInEnded() {
    if (!m_signIn)
        return;
    // What it said last, not read yet.
    readSignIn();
    const QString token = libretro::savedToken(signInDir() + QStringLiteral("/retroarch.cfg"));
    // The password went in its folder, the token it was swapped for too.
    stopSignIn();
    if (!token.isEmpty()) {
        qInfo("[Arcade] RetroArch signed in as %s", qPrintable(m_raUser));
        m_raToken = token;
        m_raPassword.clear();
        m_raFailure.clear();
        forgetSetupAccount();
    } else if (m_signInResult == libretro::SignInLine::Failed) {
        qInfo("[Arcade] RetroAchievements refused %s: %s", qPrintable(m_raUser), qPrintable(m_signInFailure));
        m_raPassword.clear();
        m_raFailure = m_signInFailure;
        forgetSetupAccount();
    } else {
        // The password waits for the next game; why it does: RetroAchievements
        // not reached, a sign-in RetroArch kept no token of, or nothing said
        // (in time, or at all: RetroArch couldn't start or load the core).
        qInfo("[Arcade] RetroArch couldn't sign in as %s now: at the next game", qPrintable(m_raUser));
        m_raFailure = (m_signInResult == libretro::SignInLine::Unreachable
                           ? (m_signInFailure.isEmpty() ? QStringLiteral("RetroAchievements couldn't be reached")
                                                        : m_signInFailure)
                           : m_signInResult == libretro::SignInLine::SignedIn
                               ? QStringLiteral("RetroArch signed in but kept no token")
                               : QStringLiteral("The sign-in couldn't be checked now"))
                      + QStringLiteral(": RetroArch signs in at the next game");
        // OSD/OS Setup's account, checked as OSD/OS started: again a while
        // later, the network up by then; once RetroAchievements has taken
        // the password, the file goes, and the password waits for the game.
        if (m_signInResult == libretro::SignInLine::SignedIn) {
            forgetSetupAccount();
        } else if (m_raFromSetup && m_setupChecks < kSetupChecks) {
            ++m_setupChecks;
            QTimer::singleShot(m_setupRetryMs, this, &RetroAchievementsAccount::retrySetupSignIn);
        }
    }
    save();
    emit changed();
}

void RetroAchievementsAccount::retrySetupSignIn() {
    // Done with meanwhile: signed in or out in Settings, or a game had the
    // password.
    if (!m_raFromSetup || m_signIn || !m_raToken.isEmpty() || m_raPassword.isEmpty() || m_raPasswordGiven)
        return;
    // Never a RetroArch beside a game's, nor beside a LAN game waiting for
    // its ports: once it is over.
    if (m_running()) {
        QTimer::singleShot(m_setupRetryMs, this, &RetroAchievementsAccount::retrySetupSignIn);
        return;
    }
    checkSignIn();
    emit changed();
}

bool RetroAchievementsAccount::stopSignIn() {
    QProcess *process = std::exchange(m_signIn, nullptr);
    if (!process)
        return false;
    process->disconnect(this);
    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
    } else {
        // Gone once it has ended, or once it couldn't start after all.
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
        process->kill();
    }
    QDir(signInDir()).removeRecursively();
    return true;
}

void RetroAchievementsAccount::setKey(const QString &key) {
    m_raKey = key.trimmed();
    m_raKeyRefused = false;
    m_retroAchievements->setKey(m_raKey);
    m_retroAchievements->checkKey();
    save();
    emit changed();
    emit keyChanged();
}

void RetroAchievementsAccount::checkKey() {
    if (!m_raKeyRefused)
        m_retroAchievements->checkKey();
}

bool RetroAchievementsAccount::keyWorks() const {
    return m_retroAchievements->keyWorks();
}

void RetroAchievementsAccount::save() const {
    QDir().mkpath(m_home);
    const QJsonObject kept{ { QStringLiteral("user"), m_raUser }, { QStringLiteral("token"), m_raToken },
                            { QStringLiteral("key"), m_raKey } };
    writeFileAtomically(m_home + QStringLiteral("/retroachievements.json"),
                        QJsonDocument(kept).toJson(QJsonDocument::Compact), kOwnerOnly);
}

void RetroAchievementsAccount::lendTo(libretro::Launch &launch, bool achievements) {
    m_raPasswordGiven = false;
    if (!achievements || m_raUser.isEmpty())
        return;
    // A sign-in still being checked: the game's RetroArch signs in
    // instead. (Without Online High Scores it goes on beside the game.)
    if (stopSignIn())
        emit changed();
    launch.achievementsUser = m_raUser;
    launch.achievementsToken = m_raToken;
    if (m_raToken.isEmpty() && !m_raPassword.isEmpty()) {
        launch.achievementsPassword = m_raPassword;
        m_raPasswordGiven = true;
    }
}

bool RetroAchievementsAccount::takeBack() {
    return std::exchange(m_raPasswordGiven, false);
}

void RetroAchievementsAccount::takeRun(const libretro::Run &run) {
    const QString config = retroarchHome() + QStringLiteral("/retroarch.cfg");
    // A RetroArch that never ran (it couldn't be started, or left no log)
    // saw neither the password nor the token: nothing became of them, and
    // the password stays for the next game, no game's any more (OSD/OS
    // Setup's sign-in may try it meanwhile, retrySetupSignIn()).
    if (!run.logged) {
        m_raPasswordGiven = false;
        QFile::remove(config);
        return;
    }
    bool changedNow = false;
    if (std::exchange(m_raPasswordGiven, false)) {
        // RetroArch saved its config on exit, the token in it if it signed
        // in; the password goes whatever became of it, and OSD/OS Setup's
        // account with it.
        const QString token = libretro::savedToken(config);
        m_raPassword.clear();
        forgetSetupAccount();
        if (!token.isEmpty()) {
            m_raToken = token;
            m_raFailure.clear();
        } else {
            m_raFailure = run.loginFailure.isEmpty() ? QStringLiteral("RetroArch did not sign in: sign in again")
                                                     : run.loginFailure;
        }
        save();
        changedNow = true;
    } else if (!m_raToken.isEmpty() && libretro::loginRefused(run.loginFailure)) {
        // The token no longer taken (the password changed since, or it
        // expired): signed out.
        m_raToken.clear();
        m_raFailure = run.loginFailure;
        save();
        changedNow = true;
    } else if (!run.loginFailure.isEmpty()) {
        // RetroAchievements not reached this time: still signed in.
        qWarning("[Arcade] RetroArch did not sign in to RetroAchievements: %s", qPrintable(run.loginFailure));
    }
    // A game's config is written anew for each game; the one it ended with
    // holds the token, or the password RetroArch was given: not left behind.
    QFile::remove(config);
    if (changedNow)
        emit changed();
}
