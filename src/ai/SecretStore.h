#pragma once

// Secret storage for AI provider API keys.
//
// Priority order on read:
//   1. Environment override (TEMPLNOTE_SECRET_<ID>) — for headless nodes.
//   2. OS keychain (libsecret / Keychain / DPAPI-backed credential store).
//   3. Encrypted local file (0600) when no keychain is available.
//
// Keys are addressed by an opaque id such as "ai.provider.openrouter.apiKey".
// Nothing in the plugin settings system ever sees the raw value.

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace Templnote::AI {

class SecretStore : public QObject {
  Q_OBJECT
public:
  explicit SecretStore(QObject *parent = nullptr);

  QString get(const QString &id) const;
  bool set(const QString &id, const QString &value);
  bool remove(const QString &id);
  bool has(const QString &id) const { return !get(id).isEmpty(); }

  // Tests use this to bypass the OS keychain and exercise the file backend
  // deterministically.
  void setTestMode(bool enabled) { m_testMode = enabled; }
  bool testMode() const { return m_testMode; }

  // Override the fallback file location (used by tests).
  void setFilePath(const QString &path) { m_filePath = path; }

  static QString environmentName(const QString &id);

private:
  QString readPlatform(const QString &id) const;
  bool writePlatform(const QString &id, const QString &value);
  bool removePlatform(const QString &id);

  QJsonObject readAllSecrets() const;
  bool writeAllSecrets(const QJsonObject &secrets) const;

  QString fallbackFilePath() const;
  QByteArray machineSecret() const;

  bool m_testMode = false;
  mutable bool m_platformUnavailable = false;
  mutable QHash<QString, QString> m_cache;
  QString m_filePath;
};

} // namespace Templnote::AI
