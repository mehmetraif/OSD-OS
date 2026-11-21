#pragma once
#include "Libretro.h"
#include <QFileDevice>
#include <QObject>
#include <QString>
#include <functional>

class QProcess;
class RetroAchievements;

// What a file holding a token, a key or a password is written with: the
// account's file, and the RetroArch config a game is lent it in.
inline constexpr QFileDevice::Permissions kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

// The RetroAchievements account RetroArch plays FinalBurn Neo's games under
// (Online High Scores; Settings → Arcade → RetroAchievements Account): its
// user, the token RetroArch swapped its password for, and the account's Web
// API key, for reading leaderboards, pictures and achievements, kept in
// <data>/arcade/retroachievements.json, owner-only. The password is kept in
// memory only, for RetroArch to sign in with (RetroAchievements' own way: a
// launcher may not), never written but into the config of the RetroArch it
// goes to, which goes as it ends: at once, with the sign-in core
// (checkSignIn()), or else at the next game (lendTo()). OSD/OS Setup's alone
// is on disk until then (takeSetupAccount()).
class RetroAchievementsAccount : public QObject {
    Q_OBJECT
public:
    // home: <data>/arcade. retroarchPath: RetroArch's program ("" for none),
    // which checks a sign-in; running: whether a game is being played, or a
    // LAN game waits for its ports (ArcadeBackend::running()), beside which
    // no RetroArch of the account's is started.
    RetroAchievementsAccount(const QString &home, RetroAchievements *retroAchievements,
                             std::function<QString()> retroarchPath, std::function<bool()> running,
                             QObject *parent = nullptr);
    ~RetroAchievementsAccount() override;

    // Its user ("" for none), and its state: "none", "checking" (RetroArch
    // signing in with it now, with the sign-in core: checkSignIn()),
    // "pending" (RetroArch to take the password at the next game and swap it
    // for a token; message() says why not now, when it was tried),
    // "signedIn" (a token), or "failed" (message() says why).
    QString user() const { return m_raUser; }
    QString state() const;
    QString message() const;
    // The Web API key ("" for none), whether RetroAchievements refused it,
    // and whether it took it (an answer came with it).
    QString key() const { return m_raKey; }
    bool keyRefused() const { return m_raKeyRefused; }
    bool keyWorks() const;

    // ArcadeBackend::signInAchievements(), signOutAchievements(),
    // setAchievementsKey() and checkAchievementsKey().
    void signIn(const QString &user, const QString &password);
    void signOut();
    void setKey(const QString &key);
    void checkKey();
    // ArcadeBackend::takeSetupAccount().
    void takeSetupAccount();
    // The sign-in core RetroArch checks a sign-in with: osdos_signin_libretro
    // beside the app, or a stand-in's.
    void setSignInCore(const QString &path) { m_signInCore = path; }
    // How long after OSD/OS Setup's account couldn't be checked it is
    // checked again: 30 s, or a test's.
    void setSetupRetryInterval(int ms) { m_setupRetryMs = ms; }

    // A game's RetroArch, while Online High Scores is on (achievements),
    // signs in as the account: its user and token, or the password, once (a
    // sign-in still being checked is stopped: the game's signs in instead).
    void lendTo(libretro::Launch &launch, bool achievements);
    // The game wasn't started after all: whether its RetroArch had been
    // given the password, which waits for the next game again.
    bool takeBack();
    // What a FinalBurn Neo game's run left: the token RetroArch swapped the
    // password for, or why it couldn't; the config, with the password or the
    // token in it, goes.
    void takeRun(const libretro::Run &run);

signals:
    // The user, the state, the message or the key's state changed.
    void changed();
    // Another key was given (setKey()).
    void keyChanged();

private:
    void save() const;
    // The sign-in checked at once (libretro::SignIn): RetroArch, with no
    // picture, sound or controllers, signs in with the password and the
    // sign-in core in <data>/arcade/retroarch/sign-in, and is stopped as it
    // says how it went. Without RetroArch or the core (a Mac's build has
    // none), nothing: the password waits for the next game.
    void checkSignIn();
    void readSignIn();
    // RetroArch ended: the token it saved, or why there is none.
    void signInEnded();
    // Stopped without a result (signed out, in again, a game started): the
    // password, if kept, waits for the next game. False when none was under
    // way.
    bool stopSignIn();
    // signIn()'s sign-in, whoever gave the password.
    void beginSignIn(const QString &user, const QString &password);
    // takeSetupAccount()'s file gone, with the password.
    void forgetSetupAccount();
    void retrySetupSignIn();
    // RetroArch's folder in <data>/arcade, OSD/OS Setup's account file and
    // the sign-in's folder.
    QString retroarchHome() const { return m_home + QStringLiteral("/retroarch"); }
    QString setupAccountPath() const { return m_home + QStringLiteral("/retroachievements-setup.json"); }
    QString signInDir() const { return retroarchHome() + QStringLiteral("/sign-in"); }

    QString m_home;
    RetroAchievements *m_retroAchievements = nullptr;
    std::function<QString()> m_retroarchPath;
    std::function<bool()> m_running;

    // The account: <data>/arcade/retroachievements.json, owner-only, but the
    // password, which RetroArch has once and swaps.
    QString m_raUser;
    QString m_raToken;
    QString m_raKey;
    bool    m_raKeyRefused = false;
    QString m_raPassword;
    QString m_raFailure;
    bool    m_raPasswordGiven = false;   // the game playing was given it
    bool    m_raFromSetup = false;       // the password is OSD/OS Setup's (takeSetupAccount())
    int     m_setupChecks = 0;           // its sign-in's checks, started or due
    int     m_setupRetryMs = 0;
    // The sign-in being checked: RetroArch with the sign-in core, and what it
    // said of the sign-in.
    QProcess *m_signIn = nullptr;
    libretro::SignInLine m_signInResult = libretro::SignInLine::None;
    QString m_signInFailure;
    QString m_signInCore;
};
