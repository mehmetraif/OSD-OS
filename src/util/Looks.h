#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Settings → Look's colour schemes, skins and themes, read for QML (AppCore's
// wrappers of the same names): from the app's folder (appRoot: assets/skins,
// assets/themes) and the data folder's (dataRoot: skins, themes and the
// custom colour schemes' files), the data folder's first.
namespace looks {

// The user's own colour scheme (custom_color_scheme.json in the data
// folder): { primary, secondary, tertiary, surface, accent }, each #rrggbb;
// empty for none, or one that can't be used (the reason in the log).
QVariantMap customColorScheme(const QString &dataRoot);
// Their colour schemes by name (custom_color_schemes.json): { name: a scheme
// as above }, those that can be used.
QVariantMap customColorSchemes(const QString &dataRoot);

// Settings → Look → Skin: how the window is dressed, apart from the colours: the
// skins there are, [{ id, name }] by name. A skin is a folder with a
// skin.json, the app's own (assets/skins) or the data folder's (skins),
// one there in place of the app's of the same name; its id is the
// folder's name. One from before skins had their name, a theme.json of
// window pictures in the data folder's themes, is a skin too.
QVariantList skins(const QString &appRoot, const QString &dataRoot);
// One skin read for QML (Main.qml's root.skin): { id, name, window,
// titleBar, hintBar, selection, icons }, a picture of each of the
// window's parts it dresses ({ source URL, border, tile }) and icons
// ({ name: URL }) in place of OSD/OS's of those names, only those it has
// that can be used; empty for none, or for one gone or unreadable.
QVariantMap skin(const QString &appRoot, const QString &dataRoot, const QString &id);
// Settings → Look → Theme: the whole look in one folder with a theme.json, found
// as skins are (assets/themes, the data folder's themes). The themes
// there are, [{ id, name }] by name.
QVariantList themes(const QString &appRoot, const QString &dataRoot);
// One theme read for QML (Main.qml's root.theme): { id, name, colors,
// skin, effects: { text, background, selector, screen, transition },
// music, arcade }, those it has. colors is a scheme's name or { primary,
// surface }; skin as skin() reads one; each effect a preset's name or its
// own ({ its numbers, 0 to 1; animate; shader, a .qsb of its own });
// music a sound file's URL; arcade the Arcade screen's look, "orb". Empty
// for none, or for one gone or unreadable.
QVariantMap theme(const QString &appRoot, const QString &dataRoot, const QString &id);
// One of the effects' shaders built into the app (shaders/<name>.frag),
// as a URL; "" in a build without them (one made without Qt Shader
// Tools).
QString effectShader(const QString &name);

} // namespace looks
