#include "SecretStore.h"

#include "storage/AppPaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QMessageAuthenticationCode>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <cstring>

namespace Templnote::AI {
namespace {

// --- Small self-contained ChaCha20 implementation for the file fallback -----

constexpr quint32 rotl(quint32 v, int n) {
  return (v << n) | (v >> (32 - n));
}

void chachaQuarter(quint32 *x, int a, int b, int c, int d) {
  x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 16);
  x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 12);
  x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 8);
  x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 7);
}

QByteArray chacha20(const QByteArray &key, const QByteArray &nonce,
                    const QByteArray &input) {
  QByteArray output;
  output.fill(char(0), input.size());
  if (key.size() != 32 || nonce.size() != 12 || input.isEmpty())
    return output;

  quint32 state[16];
  std::memcpy(state + 0, "expand 32-byte k", 16);
  const uchar *k = reinterpret_cast<const uchar *>(key.constData());
  for (int i = 0; i < 8; ++i)
    state[4 + i] = quint32(k[i * 4]) | (quint32(k[i * 4 + 1]) << 8) |
                   (quint32(k[i * 4 + 2]) << 16) |
                   (quint32(k[i * 4 + 3]) << 24);
  const uchar *n = reinterpret_cast<const uchar *>(nonce.constData());
  for (int i = 0; i < 3; ++i)
    state[13 + i] = quint32(n[i * 4]) | (quint32(n[i * 4 + 1]) << 8) |
                    (quint32(n[i * 4 + 2]) << 16) |
                    (quint32(n[i * 4 + 3]) << 24);

  for (qint64 offset = 0; offset < input.size(); offset += 64) {
    quint32 counter = quint32(offset / 64);
    state[12] = counter;
    quint32 working[16];
    std::memcpy(working, state, sizeof(working));
    for (int round = 0; round < 10; ++round) {
      chachaQuarter(working, 0, 4, 8, 12);
      chachaQuarter(working, 1, 5, 9, 13);
      chachaQuarter(working, 2, 6, 10, 14);
      chachaQuarter(working, 3, 7, 11, 15);
      chachaQuarter(working, 0, 5, 10, 15);
      chachaQuarter(working, 1, 6, 11, 12);
      chachaQuarter(working, 2, 7, 8, 13);
      chachaQuarter(working, 3, 4, 9, 14);
    }
    QByteArray block;
    block.resize(64);
    for (int i = 0; i < 16; ++i) {
      const quint32 value = working[i] + state[i];
      uchar *out = reinterpret_cast<uchar *>(block.data()) + i * 4;
      out[0] = uchar(value);
      out[1] = uchar(value >> 8);
      out[2] = uchar(value >> 16);
      out[3] = uchar(value >> 24);
    }
    const int nbytes = qMin(64, int(input.size() - offset));
    const uchar *src = reinterpret_cast<const uchar *>(input.constData() + offset);
    uchar *dst = reinterpret_cast<uchar *>(output.data() + offset);
    for (int i = 0; i < nbytes; ++i)
      dst[i] = src[i] ^ uchar(block.at(i));
  }
  return output;
}

QByteArray randomBytes(int size) {
  QByteArray result;
  result.resize(size);
  uchar *data = reinterpret_cast<uchar *>(result.data());
  for (int i = 0; i < size; ++i)
    data[i] = uchar(QRandomGenerator::global()->bounded(256));
  return result;
}

} // namespace

SecretStore::SecretStore(QObject *parent) : QObject(parent) {}

QString SecretStore::environmentName(const QString &id) {
  QString name;
  name.reserve(id.size() + 16);
  for (const QChar c : id) {
    if (c.isLetterOrNumber())
      name.append(c.toUpper());
    else
      name.append(QLatin1Char('_'));
  }
  return QStringLiteral("TEMPLNOTE_SECRET_") + name;
}

QString SecretStore::get(const QString &id) const {
  if (id.isEmpty())
    return {};
  const QString fromEnv = qEnvironmentVariable(environmentName(id).toUtf8().constData());
  if (!fromEnv.isEmpty())
    return fromEnv;
  // Resolving a key touches a QProcess (keychain) or the encrypted file. Cache
  // the result: provider/model lists are read on every UI repaint.
  auto cached = m_cache.constFind(id);
  if (cached != m_cache.constEnd())
    return cached.value();
  const QString fromPlatform = readPlatform(id);
  const QString value = !fromPlatform.isEmpty()
                            ? fromPlatform
                            : readAllSecrets().value(id).toString();
  m_cache.insert(id, value);
  return value;
}

bool SecretStore::set(const QString &id, const QString &value) {
  if (id.isEmpty())
    return false;
  if (value.isEmpty())
    return remove(id);
  // An env override wins on read, but still persist to the normal store so
  // removing the override leaves a usable connection.
  const bool stored = writePlatform(id, value) || [&] {
    QJsonObject secrets = readAllSecrets();
    secrets.insert(id, value);
    return writeAllSecrets(secrets);
  }();
  if (stored)
    m_cache.insert(id, value);
  return stored;
}

bool SecretStore::remove(const QString &id) {
  if (id.isEmpty())
    return false;
  removePlatform(id);
  m_cache.remove(id);
  QJsonObject secrets = readAllSecrets();
  if (!secrets.contains(id))
    return true;
  secrets.remove(id);
  return writeAllSecrets(secrets);
}

QString SecretStore::fallbackFilePath() const {
  if (!m_filePath.isEmpty())
    return m_filePath;
  const QString dir = AppPaths::configDirectory();
  return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("ai-secrets.dat"));
}

QByteArray SecretStore::machineSecret() const {
#if defined(Q_OS_LINUX)
  QFile f(QStringLiteral("/etc/machine-id"));
  if (f.open(QIODevice::ReadOnly))
    return f.readAll().trimmed();
#elif defined(Q_OS_WIN)
  const QSettings registry(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography"),
                           QSettings::NativeFormat);
  const QString guid = registry.value(QStringLiteral("MachineGuid")).toString();
  if (!guid.isEmpty())
    return guid.toUtf8();
#endif
  // macOS uses the keychain; the file fallback is only a safety net there.
  return QByteArray("templnote-local-fallback");
}

QJsonObject SecretStore::readAllSecrets() const {
  const QString path = fallbackFilePath();
  if (path.isEmpty())
    return {};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  const QByteArray blob = file.readAll();
  if (blob.size() < 66 || blob.left(6) != "TNSEC1")
    return {};
  const QByteArray salt = blob.mid(6, 16);
  const QByteArray nonce = blob.mid(22, 12);
  const QByteArray mac = blob.mid(34, 32);
  const QByteArray cipher = blob.mid(66);

  const QByteArray master = QCryptographicHash::hash(
      machineSecret() + salt + "Templnote.AI.SecretStore.v1",
      QCryptographicHash::Sha256);
  const QByteArray macKey = QCryptographicHash::hash(master + "mac",
                                                    QCryptographicHash::Sha256);
  QMessageAuthenticationCode hmac(QCryptographicHash::Sha256);
  hmac.setKey(macKey);
  hmac.addData(salt);
  hmac.addData(nonce);
  hmac.addData(cipher);
  if (hmac.result() != mac)
    return {};

  const QByteArray encKey = QCryptographicHash::hash(master + "enc",
                                                    QCryptographicHash::Sha256);
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(
      chacha20(encKey, nonce, cipher), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    return {};
  return doc.object();
}

bool SecretStore::writeAllSecrets(const QJsonObject &secrets) const {
  const QString path = fallbackFilePath();
  if (path.isEmpty())
    return false;
  const QByteArray salt = randomBytes(16);
  const QByteArray nonce = randomBytes(12);
  const QByteArray master = QCryptographicHash::hash(
      machineSecret() + salt + "Templnote.AI.SecretStore.v1",
      QCryptographicHash::Sha256);
  const QByteArray encKey = QCryptographicHash::hash(master + "enc",
                                                    QCryptographicHash::Sha256);
  const QByteArray macKey = QCryptographicHash::hash(master + "mac",
                                                    QCryptographicHash::Sha256);
  const QByteArray plain = QJsonDocument(secrets).toJson(QJsonDocument::Compact);
  const QByteArray cipher = chacha20(encKey, nonce, plain);
  QMessageAuthenticationCode hmac(QCryptographicHash::Sha256);
  hmac.setKey(macKey);
  hmac.addData(salt);
  hmac.addData(nonce);
  hmac.addData(cipher);

  QByteArray blob("TNSEC1");
  blob.append(salt).append(nonce).append(hmac.result()).append(cipher);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  file.write(blob);
  return file.commit();
}

QString SecretStore::readPlatform(const QString &id) const {
  if (m_testMode || m_platformUnavailable)
    return {};
#if defined(Q_OS_MACOS)
  QProcess process;
  process.start(QStringLiteral("/usr/bin/security"),
                {QStringLiteral("find-generic-password"), QStringLiteral("-s"),
                 QStringLiteral("Templnote"), QStringLiteral("-a"), id,
                 QStringLiteral("-w")});
  if (!process.waitForFinished(2000)) {
    m_platformUnavailable = true;
    return {};
  }
  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
#elif defined(Q_OS_LINUX)
  QProcess process;
  process.start(QStringLiteral("secret-tool"),
                {QStringLiteral("lookup"), QStringLiteral("service"),
                 QStringLiteral("Templnote"), QStringLiteral("account"), id});
  if (!process.waitForFinished(2000)) {
    m_platformUnavailable = true;
    return {};
  }
  if (process.exitCode() != 0)
    return {};
  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
#endif
  return {};
}

bool SecretStore::writePlatform(const QString &id, const QString &value) {
  if (m_testMode || m_platformUnavailable)
    return false;
#if defined(Q_OS_MACOS)
  QProcess process;
  process.start(QStringLiteral("/usr/bin/security"),
                {QStringLiteral("add-generic-password"), QStringLiteral("-U"),
                 QStringLiteral("-s"), QStringLiteral("Templnote"),
                 QStringLiteral("-a"), id, QStringLiteral("-w"), value});
  const bool stored = process.waitForFinished(2000) && process.exitCode() == 0;
  if (!stored)
    m_platformUnavailable = true;
  return stored;
#elif defined(Q_OS_LINUX)
  QProcess process;
  process.start(QStringLiteral("secret-tool"),
                {QStringLiteral("store"), QStringLiteral("--label=Templnote"),
                 QStringLiteral("service"), QStringLiteral("Templnote"),
                 QStringLiteral("account"), id});
  if (!process.waitForStarted(2000)) {
    m_platformUnavailable = true;
    return false;
  }
  process.write(value.toUtf8());
  process.closeWriteChannel();
  const bool stored = process.waitForFinished(2000) && process.exitCode() == 0;
  if (!stored)
    m_platformUnavailable = true;
  return stored;
#endif
  return false;
}

bool SecretStore::removePlatform(const QString &id) {
  if (m_testMode || m_platformUnavailable)
    return true;
#if defined(Q_OS_MACOS)
  QProcess process;
  process.start(QStringLiteral("/usr/bin/security"),
                {QStringLiteral("delete-generic-password"), QStringLiteral("-s"),
                 QStringLiteral("Templnote"), QStringLiteral("-a"), id});
  process.waitForFinished(2000);
  return true; // already absent is fine
#elif defined(Q_OS_LINUX)
  QProcess process;
  process.start(QStringLiteral("secret-tool"),
                {QStringLiteral("clear"), QStringLiteral("service"),
                 QStringLiteral("Templnote"), QStringLiteral("account"), id});
  process.waitForFinished(2000);
  return true;
#endif
  return true;
}

} // namespace Templnote::AI
