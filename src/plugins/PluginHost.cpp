#include "PluginHost.h"

#include "PluginManager.h"
#include "storage/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariant>

namespace Templnote {

PluginHost::PluginHost(PluginManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager),
      m_network(new QNetworkAccessManager(this)) {}

namespace {

QString cachePath(const QString &cacheKey) {
  const QString dir = AppPaths::cacheDirectory() + QStringLiteral("/plugin-cache");
  return QDir(dir).filePath(cacheKey + QStringLiteral(".json"));
}

bool safeUrl(const QString &url) {
  const QUrl parsed(url);
  if (!parsed.isValid())
    return false;
  if (parsed.scheme() == QLatin1String("https"))
    return true;
  if (parsed.scheme() != QLatin1String("http"))
    return false;
  const QString host = parsed.host().toLower();
  return host == QStringLiteral("127.0.0.1") || host == QStringLiteral("localhost") ||
         host == QStringLiteral("::1");
}

} // namespace

QString PluginHost::cachedJson(const QString &cacheKey, int ttlSeconds) const {
  if (cacheKey.isEmpty())
    return {};
  QFile cached(cachePath(cacheKey));
  if (!cached.open(QIODevice::ReadOnly))
    return {};
  const QJsonDocument doc = QJsonDocument::fromJson(cached.readAll());
  const qint64 stored = static_cast<qint64>(
      doc.object().value(QStringLiteral("_tnCachedAt")).toInteger());
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (stored <= 0 || now - stored >= ttlSeconds)
    return {};
  return QString::fromUtf8(
      QJsonDocument(doc.object()).toJson(QJsonDocument::Compact));
}

void PluginHost::storeCache(const QString &cacheKey,
                            const QJsonObject &object) const {
  QJsonObject stored = object;
  stored.insert(QStringLiteral("_tnCachedAt"),
                QDateTime::currentSecsSinceEpoch());
  QFile out(cachePath(cacheKey));
  if (out.open(QIODevice::WriteOnly))
    out.write(QJsonDocument(stored).toJson(QJsonDocument::Compact));
}

QString PluginHost::fetchJson(const QString &cacheKey, int ttlSeconds) {
  // Synchronous access is cache-only so a plugin can never freeze the UI on a
  // network request. Use fetchJsonAsync() when fresh data is required.
  return cachedJson(cacheKey, ttlSeconds);
}

void PluginHost::fetchJsonAsync(const QString &url, const QString &cacheKey,
                                const QJSValue &callback, int ttlSeconds) {
  if (!callback.isCallable())
    return;
  auto invoke = [callback](const QString &value) {
    QJSValueList arguments;
    arguments << QJSValue(value);
    callback.call(arguments);
  };
  const QString cached = cachedJson(cacheKey, ttlSeconds);
  if (!cached.isEmpty()) {
    invoke(cached);
    return;
  }
  if (cacheKey.isEmpty() || !canFetch(cacheKey) || !safeUrl(url)) {
    invoke(QString());
    return;
  }

  QNetworkRequest request{QUrl(url)};
  request.setTransferTimeout(5000);
  QNetworkReply *reply = m_network->get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, cacheKey, invoke]() mutable {
            const QByteArray data = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!ok) {
              invoke(QString());
              return;
            }
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
              invoke(QString());
              return;
            }
            QJsonObject object = doc.object();
            storeCache(cacheKey, object);
            invoke(QString::fromUtf8(
                QJsonDocument(object).toJson(QJsonDocument::Compact)));
          });
}

void PluginHost::clearCache(const QString &cacheKey) {
  if (!cacheKey.isEmpty())
    QFile::remove(cachePath(cacheKey));
}

QVariant PluginHost::setting(const QString &pluginId, const QString &key,
                             const QVariant &fallback) const {
  if (!m_manager)
    return fallback;
  const QVariant value = m_manager->pluginSetting(pluginId, key);
  return value.isValid() ? value : fallback;
}

void PluginHost::setSetting(const QString &pluginId, const QString &key,
                            const QVariant &value) {
  if (m_manager)
    m_manager->setPluginSetting(pluginId, key, value);
}

bool PluginHost::canFetch(const QString &pluginId) const {
  return m_manager && m_manager->isEnabled(pluginId) &&
         m_manager->isPluginApproved(pluginId) &&
         m_manager->pluginPermissions(pluginId).contains(
             QStringLiteral("network"));
}

} // namespace Templnote
