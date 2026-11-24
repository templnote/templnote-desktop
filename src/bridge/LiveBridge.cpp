#include "LiveBridge.h"

#include "engine/Types.h"
#include "model/BlockModel.h"

#include "storage/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QScopeGuard>

namespace Templnote {

namespace {
// LiveBridge is reachable from QML/plugin contexts, so never let it read an
// arbitrary file or block the GUI thread on a huge one.
constexpr qint64 kMaxDocumentBytes = 32 * 1024 * 1024;

bool isAllowedDocumentPath(const QString &path) {
  if (path.isEmpty())
    return false;
  const QString root =
      QFileInfo(AppPaths::documentsDirectory()).canonicalFilePath();
  const QString candidate = QFileInfo(path).canonicalFilePath();
  if (root.isEmpty() || candidate.isEmpty())
    return false;
  return candidate == root || candidate.startsWith(root + QLatin1Char('/'));
}
} // namespace

LiveBridge::LiveBridge(QObject *parent)
    : QObject(parent),
      m_session(new Engine::EditorSession(this)),
      m_feed(new Engine::ChangeFeed(m_session, this)) {
  connect(m_session, &Engine::EditorSession::patchApplied, this,
          [this](const Engine::Patch &patch) {
            m_lastPatchJson = patch.toJson();
            pushToView();
          });
  connect(m_session, &Engine::EditorSession::remoteApplied, this,
          [this](const Engine::Patch &patch) {
            m_lastPatchJson = patch.toJson();
            pushToView();
          });
}

void LiveBridge::setBlockModel(BlockModel *model) {
  if (m_blockModel == model)
    return;
  m_blockModel = model;
  emit blockModelChanged();
}

void LiveBridge::setDocumentPath(const QString &path) {
  if (m_documentPath == path)
    return;
  m_documentPath = path;
  emit documentPathChanged();
}

void LiveBridge::setLiveEnabled(bool enabled) {
  if (m_liveEnabled == enabled)
    return;
  m_liveEnabled = enabled;
  emit liveEnabledChanged();
}

bool LiveBridge::loadFromDisk() {
  if (m_documentPath.isEmpty()) {
    emit bridgeError(QStringLiteral("No document path set."));
    return false;
  }
  if (!isAllowedDocumentPath(m_documentPath)) {
    emit bridgeError(QStringLiteral("Document path is outside the documents directory."));
    return false;
  }
  QFile file(m_documentPath);
  if (!file.open(QIODevice::ReadOnly)) {
    emit bridgeError(file.errorString());
    return false;
  }
  if (file.size() > kMaxDocumentBytes) {
    emit bridgeError(QStringLiteral("Document is too large to load."));
    return false;
  }
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    emit bridgeError(error.errorString());
    return false;
  }
  QString loadError;
  if (!m_session->loadJson(doc.object(), &loadError)) {
    emit bridgeError(loadError);
    return false;
  }
  pushToView();
  return true;
}

void LiveBridge::pushToView() {
  if (!m_liveEnabled || !m_blockModel || m_syncing)
    return;
  m_syncing = true;
  const auto guard = qScopeGuard([this] { m_syncing = false; });
  const QJsonObject document = m_session->toJson();
  m_blockModel->applyRemoteDocument(document, m_lastPatchJson);
}

bool LiveBridge::applyCommand(const QJsonObject &commandJson) {
  QString error;
  Engine::Command cmd = Engine::Command::fromJson(commandJson, &error);
  if (!error.isEmpty()) {
    emit bridgeError(error);
    return false;
  }
  auto result = m_session->apply(cmd);
  if (!result.ok()) {
    emit bridgeError(result.error());
    return false;
  }
  return true;
}

bool LiveBridge::applyCommandJson(const QString &commandJson) {
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(commandJson.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError) {
    emit bridgeError(error.errorString());
    return false;
  }
  if (!doc.isObject()) {
    emit bridgeError(QStringLiteral("Command must be a JSON object."));
    return false;
  }
  return applyCommand(doc.object());
}

} // namespace Templnote
