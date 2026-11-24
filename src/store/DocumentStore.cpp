#include "DocumentStore.h"

#include "storage/AppPaths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
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
constexpr qint64 kMaxDocumentBytes = 64 * 1024 * 1024;

// Revision metadata lives in the journal as the authoritative source. The
// copy in the JSON file is informational (and user-editable), so compare it
// against the journal and also track a content hash to detect external edits
// that leave the revision untouched.
QByteArray hashableDocumentJson(const QJsonObject &document) {
  QJsonObject content = document;
  content.remove(QLatin1String(kRevisionKey));
  content.remove(QLatin1String(kLamportKey));
  return QJsonDocument(content).toJson(QJsonDocument::Compact);
}

QString hashJson(const QByteArray &bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString documentHash(const QJsonObject &document) {
  return hashJson(hashableDocumentJson(document));
}

struct JournalState {
  int revision = -1;
  QString hash;
};

JournalState readJournalState(const QString &connection, const QString &path) {
  JournalState state;
  if (connection.isEmpty() || !QSqlDatabase::contains(connection))
    return state;
  QSqlDatabase db = QSqlDatabase::database(connection);
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "SELECT revision, doc_hash FROM document_state WHERE path = ?"));
  q.addBindValue(path);
  if (q.exec() && q.next()) {
    state.revision = q.value(0).toInt();
    state.hash = q.value(1).toString();
  }
  return state;
}

void writeJournalState(const QString &connection, const QString &path,
                       int revision, const QString &hash) {
  if (connection.isEmpty() || !QSqlDatabase::contains(connection))
    return;
  QSqlDatabase db = QSqlDatabase::database(connection);
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "INSERT INTO document_state(path, revision, doc_hash) VALUES(?, ?, ?) "
      "ON CONFLICT(path) DO UPDATE SET revision = excluded.revision, "
      "doc_hash = excluded.doc_hash"));
  q.addBindValue(path);
  q.addBindValue(revision);
  q.addBindValue(hash);
  q.exec();
}
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
  exec(QStringLiteral(
      "CREATE TABLE IF NOT EXISTS document_state ("
      "path TEXT PRIMARY KEY, revision INTEGER NOT NULL, doc_hash TEXT NOT NULL)"));
  m_dbChecked = true;
  return true;
}

Engine::Result<StoredDocument> DocumentStore::load(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return Engine::Result<StoredDocument>(
        QStringLiteral("Cannot open document: ") + file.errorString());
  }
  if (file.size() > kMaxDocumentBytes)
    return Engine::Result<StoredDocument>(QStringLiteral("Document is too large."));
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    return Engine::Result<StoredDocument>(QStringLiteral("Invalid document JSON"));
  }
  const QJsonObject object = doc.object();
  if (!object.contains(QLatin1String("blocks")) ||
      !object.value(QLatin1String("blocks")).isArray()) {
    return Engine::Result<StoredDocument>(
        QStringLiteral("Invalid document: missing 'blocks' array"));
  }
  StoredDocument stored;
  stored.document = object;
  stored.path = path;
  const int fileRevision =
      object.value(QLatin1String(kRevisionKey)).toInt(0);
  const JournalState journal = readJournalState(m_connectionName, path);
  stored.revision = qMax(fileRevision, journal.revision);
  stored.lamport = static_cast<quint64>(
      object.value(QLatin1String(kLamportKey)).toInteger());
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
  lock.setStaleLockTime(5000);
  if (!lock.tryLock(5000)) {
    return Engine::Result<CommitResult>(
        QStringLiteral("Document is locked by another writer."));
  }

  const auto current = load(path);
  const int diskRevision = current.ok() ? current.value().revision : 0;
  if (diskRevision != expectedRevision) {
    return Engine::Result<CommitResult>(QStringLiteral("conflict"));
  }
  // Detect edits made directly to the JSON file: the journal remembers the
  // hash of the last content we wrote.
  const JournalState journal = readJournalState(m_connectionName, path);
  if (journal.revision >= 0 && current.ok() &&
      documentHash(current.value().document) != journal.hash) {
    return Engine::Result<CommitResult>(QStringLiteral("conflict"));
  }

  const int newRevision = diskRevision + 1;
  QString error;
  if (!writeFile(path, document, newRevision, lamport, &error))
    return Engine::Result<CommitResult>(error);

  // One serialization feeds both the content hash and the snapshot row. The
  // old code serialized the whole document twice per commit.
  const QByteArray snapshotJson = hashableDocumentJson(document);
  const QString hash = hashJson(snapshotJson);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  // Journal writes are best-effort (the document file is already committed
  // atomically), but the rows of one commit are inserted and pruned inside a
  // single transaction so readers never see a partial commit.
  if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    const bool transaction = db.transaction();

    writeJournalState(m_connectionName, path, newRevision, hash);

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO patches(path, revision, patch_json, timestamp) "
        "VALUES(?, ?, ?, ?)"));
    q.addBindValue(path);
    q.addBindValue(newRevision);
    q.addBindValue(QString::fromUtf8(
        QJsonDocument(patch.toJson()).toJson(QJsonDocument::Compact)));
    q.addBindValue(now);
    q.exec();

    Engine::SnapshotInfo snap;
    snap.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snap.revision = newRevision;
    snap.timestampMs = now;
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
    sq.addBindValue(snapshotJson);
    sq.exec();

    if (transaction) {
      pruneSnapshots(db, path);
      if (!db.commit())
        db.rollback();
    }
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
  const int fileRevision =
      doc.object().value(QLatin1String(kRevisionKey)).toInt(0);
  return qMax(fileRevision, readJournalState(m_connectionName, path).revision);
}

void DocumentStore::setSnapshotRetention(int count) {
  // Always keep at least the newest snapshot so restore/versions stay usable.
  m_snapshotRetention = qMax(1, count);
}

int DocumentStore::pruneSnapshots(QSqlDatabase &db, const QString &path) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "DELETE FROM snapshots WHERE path = ? AND revision % ? != 0 "
      "AND id NOT IN (SELECT id FROM snapshots WHERE path = ? "
      "ORDER BY revision DESC LIMIT ?)"));
  q.addBindValue(path);
  q.addBindValue(kCheckpointEvery);
  q.addBindValue(path);
  q.addBindValue(m_snapshotRetention);
  if (!q.exec()) {
    qWarning() << "DocumentStore snapshot prune failed:" << q.lastError().text();
    return 0;
  }
  return qMax(0, q.numRowsAffected());
}

int DocumentStore::pruneJournal(const QString &path) {
  if (m_connectionName.isEmpty() || !QSqlDatabase::contains(m_connectionName))
    return 0;

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  const bool transaction = db.transaction();
  const int removedSnapshots = pruneSnapshots(db, path);

  // Patch rows are provenance: only an explicit prune may drop them, and only
  // those older than every retained snapshot.
  int removedPatches = 0;
  {
    QSqlQuery oldest(db);
    oldest.prepare(QStringLiteral(
        "SELECT MIN(revision) FROM snapshots WHERE path = ?"));
    oldest.addBindValue(path);
    if (oldest.exec() && oldest.next() && !oldest.value(0).isNull()) {
      QSqlQuery del(db);
      del.prepare(QStringLiteral(
          "DELETE FROM patches WHERE path = ? AND revision < ?"));
      del.addBindValue(path);
      del.addBindValue(oldest.value(0).toInt());
      if (del.exec())
        removedPatches = qMax(0, del.numRowsAffected());
    }
  }

  if (transaction && !db.commit())
    db.rollback();

  return removedSnapshots + removedPatches;
}

bool DocumentStore::vacuum() {
  if (m_connectionName.isEmpty() || !QSqlDatabase::contains(m_connectionName))
    return false;

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  // VACUUM must run outside a transaction; commits never call it.
  if (!q.exec(QStringLiteral("VACUUM"))) {
    qWarning() << "DocumentStore VACUUM failed:" << q.lastError().text();
    return false;
  }
  return true;
}

} // namespace Templnote::Store
