#pragma once

// Persistent, revisioned document storage for headless agents and multi-user
// sync. Documents remain plain .templnote JSON files (the user can still open
// them in a text editor); revision/actor metadata is stored in a SQLite
// journal next to the cache so the file format stays backward compatible.

#include "engine/Types.h"

#include <QJsonObject>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace Templnote::Store {

struct StoredDocument {
  QJsonObject document;
  int revision = 0;
  quint64 lamport = 0;
  QString path;
};

struct CommitResult {
  bool ok = false;
  int revision = 0;
  QString error;
};

class DocumentStore : public QObject {
  Q_OBJECT
public:
  explicit DocumentStore(QObject *parent = nullptr);
  // Explicit paths for tests/embedding. Empty journalPath disables journaling.
  DocumentStore(const QString &journalPath, QObject *parent = nullptr);
  ~DocumentStore() override;

  // Read a document and its current revision. A missing revision field is
  // treated as 0 (legacy files created before the headless engine existed).
  Engine::Result<StoredDocument> load(const QString &path);

  // Atomically write `document` only if the on-disk revision still equals
  // `expectedRevision`. Returns the new revision on success. A conflict leaves
  // the file untouched and reports "conflict" in the error string.
  Engine::Result<CommitResult> commit(const QString &path, int expectedRevision,
                                      const QJsonObject &document,
                                      const Engine::Patch &patch,
                                      quint64 lamport,
                                      const QString &label = {});

  // Version history.
  QVector<Engine::SnapshotInfo> versions(const QString &path) const;
  Engine::Result<QJsonObject> snapshotDocument(const QString &path,
                                               const QString &snapshotId) const;
  Engine::Result<CommitResult> restore(const QString &path,
                                       const QString &snapshotId);

  // Full patch journal (for provenance / "what did the AI change").
  struct PatchEntry {
    int sequence = 0;
    QString path;
    int revision = 0;
    Engine::Patch patch;
    qint64 timestampMs = 0;
  };
  QVector<PatchEntry> patches(const QString &path) const;

  int currentRevision(const QString &path) const;
  QString journalPath() const { return m_journalPath; }

  // Maximum number of recent snapshots kept per document. Every Nth revision
  // is additionally retained as a checkpoint. Values below 1 are clamped to 1
  // so the newest snapshot is never dropped. Default: 50.
  void setSnapshotRetention(int count);
  int snapshotRetention() const { return m_snapshotRetention; }

  // Drop snapshots beyond the retention window and delete patches older than
  // the oldest retained snapshot. Commits only apply the snapshot policy;
  // patch rows are append-only provenance and are removed only here, when the
  // caller explicitly asks for a journal prune. Returns deleted row count.
  int pruneJournal(const QString &path);

  // Compact the SQLite journal. Never called from commit(); only when the
  // caller explicitly requests it.
  bool vacuum();

  // Creates an initial revision for a document that has no revision yet.
  Engine::Result<CommitResult> initialize(const QString &path,
                                          const QJsonObject &document);

signals:
  void committed(const QString &path, int revision, const Engine::Patch &patch);
  void restored(const QString &path, int revision);

private:
  bool openDatabase();
  bool ensureSchema();
  bool writeFile(const QString &path, const QJsonObject &document,
                 int revision, quint64 lamport, QString *error);
  bool readRevision(const QString &path, QString *error) const;
  // Prunes snapshots outside the retention window; invoked inside the commit
  // transaction (and by the explicit pruneJournal()).
  int pruneSnapshots(QSqlDatabase &db, const QString &path);

  static constexpr int kCheckpointEvery = 10;
  QString m_journalPath;
  QString m_connectionName = QStringLiteral("tn_store_main");
  mutable bool m_dbChecked = false;
  int m_snapshotRetention = 50;
};

} // namespace Templnote::Store
