#include "DocumentStore.h"

#include "storage/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace Templnote::Store {
namespace {
constexpr auto kRevisionKey = "tnRevision";
constexpr auto kLamportKey = "tnLamport";
constexpr auto kLockSuffix = ".tnlock";
} // namespace

DocumentStore::DocumentStore(QObject *parent)
    : DocumentStore(
          QDir(AppPaths::cacheDirectory()).filePath(QStringLiteral("journal.sqlite")),
          parent) {}

DocumentStore::DocumentStore(const QString &journalPath, QObject *parent)
    : QObject(parent), m_journalPath(journalPath) {
  static int counter = 0;
  m_connectionName = QStringLiteral("tn_store_%1").arg(++counter);
  if (!m_journalPath.isEmpty())
    openDatabase();
}

DocumentStore::~DocumentStore() {
  if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName))
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DocumentStore::openDatabase() {
  if (QSqlDatabase::contains(m_connectionName))
    return true;
  QDir().mkpath(QFileInfo(m_journalPath).absolutePath());
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                              m_connectionName);
  db.setDatabaseName(m_journalPath);
  if (!db.open())
    return false;
  return ensureSchema();
}

bool DocumentStore::ensureSchema() {
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  const auto exec = [&](const QString &sql) {
    if (!q.exec(sql)) {
      qWarning() << "DocumentStore schema error:" << q.lastError().text();
      return false;
    }
    return true;
  };
  exec(QStringLiteral(
      "CREATE TABLE IF NOT EXISTS patches ("
      "sequence INTEGER PRIMARY KEY AUTOINCREMENT,"
      "path TEXT NOT NULL, revision INTEGER NOT NULL,"
      "patch_json TEXT NOT NULL, timestamp INTEGER NOT NULL)"));
  exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS patches_path ON patches(path, revision)"));
  exec(QStringLiteral(
      "CREATE TABLE IF NOT EXISTS snapshots ("
      "id TEXT PRIMARY KEY, path TEXT NOT NULL, revision INTEGER NOT NULL,"
      "timestamp INTEGER NOT NULL, actor_json TEXT NOT NULL,"
      "label TEXT NOT NULL, document_json TEXT NOT NULL)"));
  exec(QStringLiteral(
      "CREATE INDEX IF NOT EXISTS snapshots_path ON snapshots(path, revision)"));
  m_dbChecked = true;
  return true;
}

Engine::Result<StoredDocument> DocumentStore::load(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return Engine::Result<StoredDocument>(
        QStringLiteral("Cannot open document: ") + file.errorString());
  }
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    return Engine::Result<StoredDocument>(parseError.errorString());
  }
  StoredDocument stored;
  stored.document = doc.object();
  stored.path = path;
  stored.revision = stored.document.value(QLatin1String(kRevisionKey)).toInt(0);
  stored.lamport = static_cast<quint64>(
      stored.document.value(QLatin1String(kLamportKey)).toInteger());
  return Engine::Result<StoredDocument>(stored);
}

bool DocumentStore::readRevision(const QString &path, QString *error) const {
  Q_UNUSED(path);
  Q_UNUSED(error);
  return true; // keep declaration for older callers
}

bool DocumentStore::writeFile(const QString &path, const QJsonObject &document,
                              int revision, quint64 lamport,
                              QString *error) {
  QJsonObject out = document;
  out.insert(QLatin1String(kRevisionKey), revision);
  out.insert(QLatin1String(kLamportKey), static_cast<qint64>(lamport));

  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  file.setDirectWriteFallback(true);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error)
      *error = file.errorString();
    return false;
  }
  file.write(QJsonDocument(out).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    if (error)
      *error = file.errorString();
    return false;
  }
  return true;
}

Engine::Result<CommitResult> DocumentStore::initialize(
    const QString &path, const QJsonObject &document) {
  const auto loaded = load(path);
  Engine::Patch empty;
  empty.actor = Engine::Actor::system();
  empty.timestampMs = QDateTime::currentMSecsSinceEpoch();
  return commit(path, loaded.ok() ? loaded.value().revision : 0, document, empty,
                0, QStringLiteral("initial"));
}

Engine::Result<CommitResult> DocumentStore::commit(
    const QString &path, int expectedRevision, const QJsonObject &document,
    const Engine::Patch &patch, quint64 lamport, const QString &label) {
  QLockFile lock(path + QLatin1String(kLockSuffix));
  lock.setStaleLockTime(30000);
  if (!lock.tryLock(5000)) {
    return Engine::Result<CommitResult>(
        QStringLiteral("Document is locked by another writer."));
  }

  const auto current = load(path);
  const int diskRevision = current.ok() ? current.value().revision : 0;
  if (diskRevision != expectedRevision) {
    return Engine::Result<CommitResult>(QStringLiteral("conflict"));
  }

  const int newRevision = diskRevision + 1;
  QString error;
  if (!writeFile(path, document, newRevision, lamport, &error))
    return Engine::Result<CommitResult>(error);

  // Journal writes are best-effort: the file is already committed atomically.
  if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO patches(path, revision, patch_json, timestamp) "
        "VALUES(?, ?, ?, ?)"));
    q.addBindValue(path);
    q.addBindValue(newRevision);
    q.addBindValue(QString::fromUtf8(
        QJsonDocument(patch.toJson()).toJson(QJsonDocument::Compact)));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.exec();

    Engine::SnapshotInfo snap;
    snap.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snap.revision = newRevision;
    snap.timestampMs = QDateTime::currentMSecsSinceEpoch();
    snap.actor = patch.actor;
    snap.label = label;
    QSqlQuery sq(db);
    sq.prepare(QStringLiteral(
        "INSERT INTO snapshots(id, path, revision, timestamp, actor_json, "
        "label, document_json) VALUES(?, ?, ?, ?, ?, ?, ?)"));
    sq.addBindValue(snap.id);
    sq.addBindValue(path);
    sq.addBindValue(newRevision);
    sq.addBindValue(snap.timestampMs);
    sq.addBindValue(QString::fromUtf8(
        QJsonDocument(snap.actor.toJson()).toJson(QJsonDocument::Compact)));
    sq.addBindValue(label);
    sq.addBindValue(QString::fromUtf8(
        QJsonDocument(document).toJson(QJsonDocument::Compact)));
    sq.exec();
  }

  CommitResult result;
  result.ok = true;
  result.revision = newRevision;
  emit committed(path, newRevision, patch);
  return Engine::Result<CommitResult>(result);
}

QVector<Engine::SnapshotInfo> DocumentStore::versions(const QString &path) const {
  QVector<Engine::SnapshotInfo> result;
  if (!QSqlDatabase::contains(m_connectionName))
    return result;
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "SELECT id, revision, timestamp, actor_json, label FROM snapshots "
      "WHERE path = ? ORDER BY revision ASC"));
  q.addBindValue(path);
  if (!q.exec())
    return result;
  while (q.next()) {
    Engine::SnapshotInfo s;
    s.id = q.value(0).toString();
    s.revision = q.value(1).toInt();
    s.timestampMs = q.value(2).toLongLong();
    QJsonParseError e;
    const QJsonDocument actorDoc = QJsonDocument::fromJson(
        q.value(3).toByteArray(), &e);
    if (e.error == QJsonParseError::NoError)
      s.actor = Engine::Actor::fromJson(actorDoc.object());
    s.label = q.value(4).toString();
    result.append(s);
  }
  return result;
}

Engine::Result<QJsonObject> DocumentStore::snapshotDocument(
    const QString &path, const QString &snapshotId) const {
  if (!QSqlDatabase::contains(m_connectionName))
    return Engine::Result<QJsonObject>(QStringLiteral("No journal."));
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "SELECT document_json FROM snapshots WHERE path = ? AND id = ?"));
  q.addBindValue(path);
  q.addBindValue(snapshotId);
  if (!q.exec() || !q.next())
    return Engine::Result<QJsonObject>(QStringLiteral("Snapshot not found."));
  QJsonParseError e;
  const QJsonDocument doc = QJsonDocument::fromJson(q.value(0).toByteArray(), &e);
  if (e.error != QJsonParseError::NoError)
    return Engine::Result<QJsonObject>(e.errorString());
  return Engine::Result<QJsonObject>(doc.object());
}

Engine::Result<CommitResult> DocumentStore::restore(const QString &path,
                                                    const QString &snapshotId) {
  const auto snap = snapshotDocument(path, snapshotId);
  if (!snap.ok())
    return Engine::Result<CommitResult>(snap.error());
  const auto current = load(path);
  if (!current.ok())
    return Engine::Result<CommitResult>(current.error());
  Engine::Patch patch;
  patch.actor = Engine::Actor::system();
  patch.timestampMs = QDateTime::currentMSecsSinceEpoch();
  patch.ops.append([] {
    Engine::Op op;
    op.type = Engine::Op::Type::ReplaceRange;
    return op;
  }());
  auto result = commit(path, current.value().revision, snap.value(), patch, 0,
                       QStringLiteral("restore:") + snapshotId);
  if (result.ok())
    emit restored(path, result.value().revision);
  return result;
}

QVector<DocumentStore::PatchEntry> DocumentStore::patches(
    const QString &path) const {
  QVector<PatchEntry> result;
  if (!QSqlDatabase::contains(m_connectionName))
    return result;
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "SELECT sequence, revision, patch_json, timestamp FROM patches "
      "WHERE path = ? ORDER BY sequence ASC"));
  q.addBindValue(path);
  if (!q.exec())
    return result;
  while (q.next()) {
    PatchEntry e;
    e.sequence = q.value(0).toInt();
    e.revision = q.value(1).toInt();
    e.timestampMs = q.value(3).toLongLong();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(q.value(2).toByteArray(),
                                                      &parseError);
    if (parseError.error == QJsonParseError::NoError)
      e.patch = Engine::Patch::fromJson(doc.object());
    result.append(e);
  }
  return result;
}

int DocumentStore::currentRevision(const QString &path) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return -1;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  return doc.object().value(QLatin1String(kRevisionKey)).toInt(0);
}

} // namespace Templnote::Store
