#pragma once

#include <QNetworkRequest>
#include <QString>
#include <QVariantList>

#include <functional>

class QObject;

// What the Playlists module asks of a media server, Jellyfin or Emby: one
// interface, so that it never minds which it has. The two backends implement
// it in the base class they share (EmbyApiBackend), with their requests,
// their item format and their TLS allowances.
class MediaServer {
public:
    virtual ~MediaServer() = default;

    // Signed in to a server.
    virtual bool signedIn() const = 0;

    // The item's original file, as the server lets this user download it
    // (401 or 403 otherwise). A request rather than a reply: the download
    // runs on a thread of its own (ServerDownload).
    virtual QNetworkRequest downloadRequest(const QString &itemId) const = 0;

    // A URL mpv streams the item from, with the token in its query rather
    // than a header, so that in a playlist mixing sources the token goes to
    // this server and nowhere else.
    virtual QString streamUrl(const QString &itemId) const = 0;

    // A folder's children as the backend's items ({ itemId, title, type,
    // isFolder, index, grandparentTitle, … }): parentId "" for this user's
    // libraries with videos in them, "resume" and "nextup" for what they are
    // watching and the episodes next in their shows, else a library's, show's
    // or season's. done runs on context, and not at all once context is gone.
    virtual void browse(const QString &parentId, QObject *context,
                        std::function<void(bool ok, const QVariantList &items)> done) = 0;
};
