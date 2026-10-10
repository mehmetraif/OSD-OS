#pragma once
#include <QCache>
#include <QFutureWatcher>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>

// Directory readers capture values, never a backend: a slow read may finish
// after its view or backend is destroyed. At most two reads run at a time.
class AsyncDirectoryCache : public QObject {
public:
    explicit AsyncDirectoryCache(QObject *parent) : QObject(parent) {}
    std::function<void(const QString &)> ready;
    QVariant fetch(const QString &key, std::function<QVariantList()> read) {
        if (auto *items = m_cache.object(key)) return *items;
        if (!m_pending.contains(key)) {
            m_pending.insert(key);
            m_queue.enqueue({key, std::move(read)});
            pump();
        }
        return {};
    }
    void clear() {
        ++m_generation;
        m_cache.clear();
        m_pending.clear();
        m_queue.clear();
    }
private:
    struct Request { QString key; std::function<QVariantList()> read; };
    void pump() {
        while (m_running < 2 && !m_queue.isEmpty()) {
            auto request = m_queue.dequeue();
            const auto generation = m_generation;
            ++m_running;
            auto *watcher = new QFutureWatcher<QVariantList>(this);
            connect(watcher, &QFutureWatcher<QVariantList>::finished, this,
                    [this, watcher, key = request.key, generation]() {
                --m_running;
                if (generation == m_generation) {
                    m_pending.remove(key);
                    m_cache.insert(key, new QVariantList(watcher->result()));
                    if (ready) ready(key);
                }
                watcher->deleteLater();
                pump();
            });
            watcher->setFuture(QtConcurrent::run(std::move(request.read)));
        }
    }
    QCache<QString, QVariantList> m_cache {64};
    QSet<QString> m_pending;
    QQueue<Request> m_queue;
    int m_running = 0;
    quint64 m_generation = 0;
};
