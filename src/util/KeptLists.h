#pragma once

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <optional>

// The lists of items a media server's backend keeps (Plex's, Jellyfin's and
// Emby's loadList()), so going back to one from a film's detail screen shows
// it without asking the server again. Each is kept by a key its backend
// makes (server, user and list), a while (kKeepMs), and only the last few
// (kKept), the oldest going first. An empty key is never kept.
class KeptLists {
public:
    // Long enough to go into a film's detail screen and back, or play a
    // trailer, short enough to show what was added on the server meanwhile.
    static constexpr qint64 kKeepMs = 10 * 60 * 1000;
    static constexpr int kKept = 6;

    std::optional<QVariantList> find(const QString &key,
                                     qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        const auto found = m_lists.constFind(key);
        if (key.isEmpty() || found == m_lists.constEnd() || now - found->at >= kKeepMs)
            return std::nullopt;
        return found->rows;
    }
    void keep(const QString &key, const QVariantList &rows,
              qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        if (key.isEmpty())
            return;
        m_lists.insert(key, { rows, now });
        m_order.removeAll(key);
        m_order.append(key);
        while (m_order.size() > kKept)
            m_lists.remove(m_order.takeFirst());
    }
    void clear() {
        m_lists.clear();
        m_order.clear();
    }

private:
    struct Kept { QVariantList rows; qint64 at = 0; };
    QHash<QString, Kept> m_lists;
    QStringList m_order;    // oldest first
};
