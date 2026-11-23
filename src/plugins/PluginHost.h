#pragma once

// Host services available to plugin QML: cached HTTP JSON fetch, document
// context and settings. Plugins use `pluginHost` instead of raw networking so
// caching, timeouts and permissions stay under the app's control.

#include <QJSValue>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariant>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

namespace Templnote {
class PluginManager;

class PluginHost : public QObject {
  Q_OBJECT
public:
  explicit PluginHost(PluginManager *manager, QObject *parent = nullptr);

  // Returns a fresh cached JSON string, or an empty string when there is no
  // fresh cache entry. Never blocks the UI; use fetchJsonAsync for network.
  Q_INVOKABLE QString fetchJson(const QString &cacheKey, int ttlSeconds = 3600);
  // Asynchronous fetch with a QML callback `(jsonString) => void`. The callback
  // receives an empty string on error. Only HTTPS/loopback URLs are accepted.
  Q_INVOKABLE void fetchJsonAsync(const QString &url, const QString &cacheKey,
                                  const QJSValue &callback,
                                  int ttlSeconds = 3600);
  Q_INVOKABLE void clearCache(const QString &cacheKey);

  Q_INVOKABLE QVariant setting(const QString &pluginId, const QString &key,
                               const QVariant &fallback = QVariant()) const;
  Q_INVOKABLE void setSetting(const QString &pluginId, const QString &key,
                              const QVariant &value);

  // Only enabled, approved plugins may call fetchJson.
  Q_INVOKABLE bool canFetch(const QString &pluginId) const;

private:
  QString cachedJson(const QString &cacheKey, int ttlSeconds) const;
  void storeCache(const QString &cacheKey, const QJsonObject &object) const;

  PluginManager *m_manager = nullptr;
  QNetworkAccessManager *m_network = nullptr;
};
} // namespace Templnote
