#pragma once

// LiveBridge connects the headless engine to the QML editor. An EditorSession
// (owned here) is the authoritative document; every patch it produces is
// pushed into the BlockModel so an open editor sees changes live. AI agents and
// remote users apply patches through the session, never through the QML view.

#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "model/BlockModel.h"

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

  // Load the current documentPath into the session (from disk) and push it to
  // the view. Call once after both path and model are set.
  Q_INVOKABLE bool loadFromDisk();

  // Apply a command through the session. Exposed to QML and agents.
  Q_INVOKABLE bool applyCommand(const QJsonObject &commandJson);
  Q_INVOKABLE bool applyCommandJson(const QString &commandJson);

signals:
  void blockModelChanged();
  void documentPathChanged();
  void liveEnabledChanged();
  void bridgeError(const QString &message);

private:
  void pushToView();

  Engine::EditorSession *m_session = nullptr;
  Engine::ChangeFeed *m_feed = nullptr;
  BlockModel *m_blockModel = nullptr;
  QString m_documentPath;
  bool m_liveEnabled = true;
  bool m_syncing = false;
  QJsonObject m_lastPatchJson;
};

} // namespace Templnote
