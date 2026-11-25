#pragma once

// LiveBridge connects the headless engine to the QML editor. An EditorSession
// (owned here) is the authoritative document; every patch it produces is
// pushed into the BlockModel so an open editor sees changes live. Remote users
// apply patches through the session, never through the QML view.

#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "model/BlockModel.h"
#include "store/DocumentStore.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace Templnote {

class LiveBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(Engine::EditorSession *session READ session CONSTANT)
  Q_PROPERTY(Engine::ChangeFeed *feed READ feed CONSTANT)
  Q_PROPERTY(BlockModel *blockModel READ blockModel WRITE setBlockModel NOTIFY
                 blockModelChanged)
  Q_PROPERTY(QString documentPath READ documentPath WRITE setDocumentPath NOTIFY
                 documentPathChanged)
  Q_PROPERTY(bool liveEnabled READ liveEnabled WRITE setLiveEnabled NOTIFY
                 liveEnabledChanged)
  Q_PROPERTY(bool autoCommit READ autoCommit WRITE setAutoCommit NOTIFY
                 autoCommitChanged)
  Q_PROPERTY(int loadedRevision READ loadedRevision NOTIFY revisionCommitted)

public:
  explicit LiveBridge(QObject *parent = nullptr);

  Engine::EditorSession *session() const { return m_session; }
  Engine::ChangeFeed *feed() const { return m_feed; }
  BlockModel *blockModel() const { return m_blockModel; }
  void setBlockModel(BlockModel *model);

  QString documentPath() const { return m_documentPath; }
  void setDocumentPath(const QString &path);

  bool liveEnabled() const { return m_liveEnabled; }
  void setLiveEnabled(bool enabled);

  bool autoCommit() const { return m_autoCommit; }
  void setAutoCommit(bool enabled);

  int loadedRevision() const { return m_loadedRevision; }

  // The store owns persistence. Without one, applyCommand stays in memory.
  void setStore(Store::DocumentStore *store) { m_store = store; }

  // Test seam: allow documents outside the user's documents directory.
  void setAllowedRoot(const QString &root) { m_allowedRoot = root; }

  // Load the current documentPath into the session (from disk) and push it to
  // the view. Call once after both path and model are set.
  Q_INVOKABLE bool loadFromDisk();

  // Apply a command through the session. Exposed to QML.
  Q_INVOKABLE bool applyCommand(const QJsonObject &commandJson);
  Q_INVOKABLE bool applyCommandJson(const QString &commandJson);

  // Commit all accumulated ops as one transaction. Returns false on a CAS
  // conflict or store error. Cancel reloads the last committed state.
  Q_INVOKABLE bool commitPending(const QString &label = QStringLiteral("live-bridge"));
  Q_INVOKABLE bool cancelPending();

signals:
  void blockModelChanged();
  void documentPathChanged();
  void liveEnabledChanged();
  void autoCommitChanged();
  void revisionCommitted(int revision);
  void bridgeError(const QString &message);

private:
  void pushToView();
  bool isAllowedDocumentPath(const QString &path) const;

  Engine::EditorSession *m_session = nullptr;
  Engine::ChangeFeed *m_feed = nullptr;
  BlockModel *m_blockModel = nullptr;
  Store::DocumentStore *m_store = nullptr;
  QString m_documentPath;
  QString m_allowedRoot;
  bool m_liveEnabled = true;
  bool m_autoCommit = true;
  bool m_syncing = false;
  int m_loadedRevision = 0;
  Engine::Patch m_pendingPatch;
  QJsonObject m_lastPatchJson;
};

} // namespace Templnote
