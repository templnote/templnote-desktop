#pragma once

// Core AI provider registry. Provider support is data-driven: a definition
// describes the endpoints and protocol, and this class owns connection state,
// discovered models, enable/disable flags and the user's default model.
//
// It is shared by the GUI (as `providerManager`) and the headless agent daemon.
// API keys live in SecretStore, never in the persisted registry JSON.

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkReply;

namespace Templnote::AI {

class SecretStore;

enum class ProviderKind { Local, OpenAICompatible, Anthropic };

struct ModelRef {
  QString providerId;
  QString modelId;

  bool isValid() const { return !providerId.isEmpty() && !modelId.isEmpty(); }
  QString key() const { return providerId + QStringLiteral("|") + modelId; }

  static ModelRef fromKey(const QString &key) {
    const int sep = key.indexOf(QLatin1Char('|'));
    if (sep < 0)
      return {};
    return {key.left(sep), key.mid(sep + 1)};
  }
};

struct ResolvedModel {
  bool ok = false;
  QString chatUrl;
  QString modelId;
  QString apiKey;
  ProviderKind kind = ProviderKind::OpenAICompatible;
  QString displayName;
};

struct ProviderDefinition {
  QString id;
  QString name;
  ProviderKind kind = ProviderKind::OpenAICompatible;
  QString baseUrl;
  QString modelsPath;
  QString chatPath;
  QString auth; // "bearer", "anthropic", "none"
  QStringList presetModels;
  bool requiresKey = true;
  // Whether this provider's chat endpoint accepts OpenAI/Anthropic-style tools.
  bool supportsToolCalls = true;
};

struct ModelState {
  QString label;
  bool enabled = true;
  bool discovered = true;
};

struct ProviderState {
  bool connected = false;
  QString baseUrl;
  QHash<QString, ModelState> models;
};

class ProviderRegistry : public QObject {
  Q_OBJECT

  Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
  Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
  // Bump on any provider/model state change so QML bindings that call
  // enabledModels() re-evaluate.
  Q_PROPERTY(int revision READ revision NOTIFY modelsChanged)
  Q_PROPERTY(bool anyConnected READ anyConnected NOTIFY connectivityChanged)
  Q_PROPERTY(QString defaultModelKey READ defaultModelKey WRITE
                 setDefaultModelKey NOTIFY defaultModelChanged)
  Q_PROPERTY(QString defaultProviderId READ defaultProviderId NOTIFY
                 defaultModelChanged)
  Q_PROPERTY(QString defaultModelId READ defaultModelId NOTIFY
                 defaultModelChanged)

public:
  explicit ProviderRegistry(QObject *parent = nullptr,
                            SecretStore *secrets = nullptr);
  ~ProviderRegistry() override;

  static const QHash<QString, ProviderDefinition> &definitions();

  int revision() const { return m_revision; }

  QVariantList providers() const;
  QVariantList models() const;
  Q_INVOKABLE QVariantList modelsFor(const QString &providerId) const;
  // Models that are toggled on AND belong to a connected provider — this is
  // exactly what the prompt panel's model list shows.
  Q_INVOKABLE QVariantList enabledModels() const;
  Q_INVOKABLE QVariantMap provider(const QString &id) const;
  Q_INVOKABLE QVariantMap modelInfo(const QString &modelKey) const;
  Q_INVOKABLE bool isConnected(const QString &id) const;
  Q_INVOKABLE bool supportsTools(const QString &providerId) const;

  bool anyConnected() const;
  QString defaultModelKey() const;
  void setDefaultModelKey(const QString &key);
  QString defaultProviderId() const;
  QString defaultModelId() const;
  ModelRef defaultModel() const { return ModelRef::fromKey(defaultModelKey()); }

  // Connect validates connectivity by listing models, stores the key only on
  // success, and defaults every newly discovered model to enabled.
  Q_INVOKABLE bool connectProvider(const QString &id, const QString &apiKey,
                                   const QString &baseUrl = {});
  Q_INVOKABLE void disconnectProvider(const QString &id);
  Q_INVOKABLE bool setModelEnabled(const QString &providerId,
                                   const QString &modelId, bool enabled);
  Q_INVOKABLE bool addModel(const QString &providerId, const QString &modelId);

  ResolvedModel resolve(const ModelRef &ref, QString *error = nullptr) const;

  Q_INVOKABLE void detectLocalProviders();
  void setLocalDetectionEnabled(bool enabled) { m_localDetection = enabled; }
  // Test hook: point the local probes at a mock server.
  void setLocalEndpoint(const QString &id, const QString &baseUrl) {
    m_localEndpoints.insert(id, baseUrl);
  }
  void setSecrets(SecretStore *secrets) { m_secrets = secrets; }
  SecretStore *secrets() const { return m_secrets; }

signals:
  void providersChanged();
  void modelsChanged();
  void connectivityChanged();
  void defaultModelChanged();
  void connected(const QString &providerId);
  void disconnected(const QString &providerId);
  void error(const QString &message);

private slots:
  void onModelsRequestFinished();

private:
  struct Pending {
    QString providerId;
    QString apiKey;
    QString baseUrl;
    bool connectOnSuccess = false;
    bool isLocalProbe = false;
  };

  void load();
  void save();
  void applyDiscoveredModels(const QString &providerId, const QStringList &ids,
                             bool pruneMissing);
  void markConnected(const QString &providerId, const QString &baseUrl,
                     const QString &apiKey);
  QNetworkRequest modelRequest(const QString &providerId,
                               const QString &baseUrl,
                               const QString &apiKey) const;
  QStringList parseModels(const QByteArray &data, const QString &providerId) const;

  QHash<QString, ProviderState> m_states;
  QHash<QString, QString> m_localEndpoints;
  QString m_defaultModelKey;
  QNetworkAccessManager *m_manager = nullptr;
  SecretStore *m_secrets = nullptr;
  QHash<QNetworkReply *, Pending> m_pending;
  int m_revision = 0;
  bool m_localDetection = true;
  bool m_ownsSecrets = false;
};

} // namespace Templnote::AI
