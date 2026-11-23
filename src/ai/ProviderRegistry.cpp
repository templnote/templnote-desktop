#include "ProviderRegistry.h"

#include "SecretStore.h"
#include "storage/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

namespace Templnote::AI {

namespace {
QString registryPath() {
  const QString dir = AppPaths::configDirectory();
  return dir.isEmpty() ? QString()
                       : QDir(dir).filePath(QStringLiteral("ai-providers.json"));
}

QString secretId(const QString &providerId) {
  return QStringLiteral("ai.provider.") + providerId +
         QStringLiteral(".apiKey");
}

QString kindName(ProviderKind kind) {
  switch (kind) {
  case ProviderKind::Local:
    return QStringLiteral("local");
  case ProviderKind::Anthropic:
    return QStringLiteral("anthropic");
  case ProviderKind::OpenAICompatible:
    return QStringLiteral("openai-compatible");
  }
  return {};
}

QString trimmedBase(QString base) {
  while (base.endsWith(QLatin1Char('/')))
    base.chop(1);
  return base;
}

// Never send a stored API key to an arbitrary host. Custom endpoints must be
// TLS or loopback; this blocks plaintext LAN leaks and SSRF-style probing.
bool safeBaseUrl(const QString &base) {
  const QUrl url(base);
  if (!url.isValid() || url.scheme() == QLatin1String("file"))
    return false;
  if (url.scheme() == QLatin1String("https"))
    return true;
  if (url.scheme() != QLatin1String("http"))
    return false;
  const QString host = url.host().toLower();
  return host == QStringLiteral("127.0.0.1") || host == QStringLiteral("localhost") ||
         host == QStringLiteral("::1");
}
} // namespace

const QHash<QString, ProviderDefinition> &ProviderRegistry::definitions() {
  static const QHash<QString, ProviderDefinition> table = [] {
    QHash<QString, ProviderDefinition> result;

    result.insert(QStringLiteral("ollama"),
                  {QStringLiteral("ollama"), QStringLiteral("Ollama (local)"),
                   ProviderKind::Local, QStringLiteral("http://127.0.0.1:11434"),
                   QStringLiteral("/api/tags"), QStringLiteral("/api/chat"),
                   QStringLiteral("none"), {}, false});

    result.insert(QStringLiteral("lm-studio"),
                  {QStringLiteral("lm-studio"), QStringLiteral("LM Studio (local)"),
                   ProviderKind::Local, QStringLiteral("http://127.0.0.1:1234/v1"),
                   QStringLiteral("/models"), QStringLiteral("/chat/completions"),
                   QStringLiteral("none"), {}, false});

    result.insert(QStringLiteral("openrouter"),
                  {QStringLiteral("openrouter"), QStringLiteral("OpenRouter"),
                   ProviderKind::OpenAICompatible,
                   QStringLiteral("https://openrouter.ai/api/v1"),
                   QStringLiteral("/models"), QStringLiteral("/chat/completions"),
                   QStringLiteral("bearer"),
                   {QStringLiteral("openai/gpt-4o-mini"),
                    QStringLiteral("anthropic/claude-3.5-sonnet")}});

    result.insert(QStringLiteral("openai"),
                  {QStringLiteral("openai"), QStringLiteral("OpenAI"),
                   ProviderKind::OpenAICompatible,
                   QStringLiteral("https://api.openai.com/v1"),
                   QStringLiteral("/models"), QStringLiteral("/chat/completions"),
                   QStringLiteral("bearer"),
                   {QStringLiteral("gpt-4o-mini"), QStringLiteral("gpt-4o")}});

    result.insert(QStringLiteral("groq"),
                  {QStringLiteral("groq"), QStringLiteral("Groq"),
                   ProviderKind::OpenAICompatible,
                   QStringLiteral("https://api.groq.com/openai/v1"),
                   QStringLiteral("/models"), QStringLiteral("/chat/completions"),
                   QStringLiteral("bearer"),
                   {QStringLiteral("llama-3.3-70b-versatile")}});

    result.insert(QStringLiteral("together"),
                  {QStringLiteral("together"), QStringLiteral("Together AI"),
                   ProviderKind::OpenAICompatible,
                   QStringLiteral("https://api.together.xyz/v1"),
                   QStringLiteral("/models"), QStringLiteral("/chat/completions"),
                   QStringLiteral("bearer"), {}});

    result.insert(QStringLiteral("anthropic"),
                  {QStringLiteral("anthropic"), QStringLiteral("Anthropic"),
                   ProviderKind::Anthropic,
                   QStringLiteral("https://api.anthropic.com/v1"),
                   QStringLiteral("/models"), QStringLiteral("/messages"),
                   QStringLiteral("anthropic"),
                   {QStringLiteral("claude-3-5-sonnet-latest"),
                    QStringLiteral("claude-3-5-haiku-latest")}});

    result.insert(QStringLiteral("custom"),
                  {QStringLiteral("custom"), QStringLiteral("Custom endpoint"),
                   ProviderKind::OpenAICompatible, QString(),
                   QStringLiteral("/models"), QStringLiteral("/chat/completions"),
                   QStringLiteral("bearer"), {}});

    return result;
  }();
  return table;
}

ProviderRegistry::ProviderRegistry(QObject *parent, SecretStore *secrets)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)),
      m_secrets(secrets) {
  if (!m_secrets) {
    m_secrets = new SecretStore(this);
    m_ownsSecrets = true;
  }
  load();
  // Local probing is explicit: the GUI and daemon call detectLocalProviders()
  // when they are ready. Constructing a registry must not start network I/O.
}

ProviderRegistry::~ProviderRegistry() {
  for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
    if (it.key())
      it.key()->abort();
}

void ProviderRegistry::load() {
  const QString path = registryPath();
  if (path.isEmpty())
    return;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return;
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  const QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
  for (auto it = providers.constBegin(); it != providers.constEnd(); ++it) {
    const QString id = it.key();
    if (!definitions().contains(id))
      continue;
    const QJsonObject value = it.value().toObject();
    const ProviderDefinition &def = definitions().value(id);
    ProviderState state;
    state.baseUrl = value.value(QStringLiteral("baseUrl")).toString(def.baseUrl);
    const bool hasKey = !def.requiresKey || m_secrets->has(secretId(id));
    state.connected = value.value(QStringLiteral("connected")).toBool(false) && hasKey;
    const QJsonObject models = value.value(QStringLiteral("models")).toObject();
    for (auto modelIt = models.constBegin(); modelIt != models.constEnd();
         ++modelIt) {
      const QJsonObject model = modelIt.value().toObject();
      ModelState modelState;
      modelState.label = model.value(QStringLiteral("label")).toString(modelIt.key());
      modelState.enabled = model.value(QStringLiteral("enabled")).toBool(true);
      modelState.discovered = model.value(QStringLiteral("discovered")).toBool(true);
      state.models.insert(modelIt.key(), modelState);
    }
    m_states.insert(id, state);
  }
  m_defaultModelKey = root.value(QStringLiteral("defaultModel")).toString();
}

void ProviderRegistry::save() {
  const QString path = registryPath();
  if (path.isEmpty())
    return;
  QJsonObject providers;
  for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
    const ProviderState &state = it.value();
    if (!state.connected && state.models.isEmpty())
      continue;
    QJsonObject entry;
    entry.insert(QStringLiteral("connected"), state.connected);
    entry.insert(QStringLiteral("baseUrl"), state.baseUrl);
    QJsonObject models;
    for (auto modelIt = state.models.constBegin();
         modelIt != state.models.constEnd(); ++modelIt) {
      QJsonObject model;
      model.insert(QStringLiteral("label"), modelIt.value().label);
      model.insert(QStringLiteral("enabled"), modelIt.value().enabled);
      model.insert(QStringLiteral("discovered"), modelIt.value().discovered);
      models.insert(modelIt.key(), model);
    }
    entry.insert(QStringLiteral("models"), models);
    providers.insert(it.key(), entry);
  }
  QJsonObject root;
  root.insert(QStringLiteral("providers"), providers);
  root.insert(QStringLiteral("defaultModel"), m_defaultModelKey);

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return;
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

QVariantList ProviderRegistry::providers() const {
  QVariantList result;
  for (auto it = definitions().constBegin(); it != definitions().constEnd();
       ++it) {
    const ProviderDefinition &def = it.value();
    if (def.kind == ProviderKind::Local && !m_states.contains(def.id))
      continue; // show local providers only once they have been detected
    const ProviderState state = m_states.value(def.id);
    QVariantMap entry;
    entry.insert(QStringLiteral("id"), def.id);
    entry.insert(QStringLiteral("name"), def.name);
    entry.insert(QStringLiteral("kind"), kindName(def.kind));
    entry.insert(QStringLiteral("connected"), state.connected);
    entry.insert(QStringLiteral("requiresKey"), def.requiresKey);
    entry.insert(QStringLiteral("hasKey"),
                 !def.requiresKey || m_secrets->has(secretId(def.id)));
    entry.insert(QStringLiteral("baseUrl"), state.baseUrl.isEmpty()
                                                ? def.baseUrl : state.baseUrl);
    entry.insert(QStringLiteral("modelCount"), int(state.models.size()));
    result.append(entry);
  }
  return result;
}

QVariantList ProviderRegistry::models() const {
  QVariantList result;
  for (auto it = definitions().constBegin(); it != definitions().constEnd();
       ++it) {
    const ProviderState state = m_states.value(it.key());
    for (auto modelIt = state.models.constBegin();
         modelIt != state.models.constEnd(); ++modelIt) {
      QVariantMap entry;
      entry.insert(QStringLiteral("key"),
                   it.key() + QStringLiteral("|") + modelIt.key());
      entry.insert(QStringLiteral("providerId"), it.key());
      entry.insert(QStringLiteral("providerName"), it.value().name);
      entry.insert(QStringLiteral("modelId"), modelIt.key());
      entry.insert(QStringLiteral("label"), modelIt.value().label);
      entry.insert(QStringLiteral("enabled"), modelIt.value().enabled);
      entry.insert(QStringLiteral("connected"), state.connected);
      result.append(entry);
    }
  }
  return result;
}

QVariantList ProviderRegistry::modelsFor(const QString &providerId) const {
  QVariantList result;
  for (const QVariant &value : models()) {
    const QVariantMap entry = value.toMap();
    if (entry.value(QStringLiteral("providerId")).toString() == providerId)
      result.append(entry);
  }
  return result;
}

QVariantList ProviderRegistry::enabledModels() const {
  QVariantList result;
  for (const QVariant &value : models()) {
    const QVariantMap entry = value.toMap();
    if (entry.value(QStringLiteral("enabled")).toBool() &&
        entry.value(QStringLiteral("connected")).toBool())
      result.append(entry);
  }
  return result;
}

QVariantMap ProviderRegistry::provider(const QString &id) const {
  const ProviderDefinition def = definitions().value(id);
  if (def.id.isEmpty())
    return {};
  const ProviderState state = m_states.value(id);
  QVariantMap entry;
  entry.insert(QStringLiteral("id"), def.id);
  entry.insert(QStringLiteral("name"), def.name);
  entry.insert(QStringLiteral("kind"), kindName(def.kind));
  entry.insert(QStringLiteral("connected"), state.connected);
  entry.insert(QStringLiteral("requiresKey"), def.requiresKey);
  entry.insert(QStringLiteral("baseUrl"), state.baseUrl.isEmpty()
                                              ? def.baseUrl : state.baseUrl);
  entry.insert(QStringLiteral("modelsPath"), def.modelsPath);
  return entry;
}

QVariantMap ProviderRegistry::modelInfo(const QString &modelKey) const {
  const ModelRef ref = ModelRef::fromKey(modelKey);
  const ProviderState state = m_states.value(ref.providerId);
  const ModelState model = state.models.value(ref.modelId);
  if (model.label.isEmpty())
    return {};
  QVariantMap entry;
  entry.insert(QStringLiteral("key"), modelKey);
  entry.insert(QStringLiteral("providerId"), ref.providerId);
  entry.insert(QStringLiteral("modelId"), ref.modelId);
  entry.insert(QStringLiteral("label"), model.label);
  entry.insert(QStringLiteral("enabled"), model.enabled);
  return entry;
}

bool ProviderRegistry::isConnected(const QString &id) const {
  return m_states.value(id).connected;
}

bool ProviderRegistry::supportsTools(const QString &providerId) const {
  const ProviderDefinition def = definitions().value(providerId);
  return !def.id.isEmpty() && def.supportsToolCalls;
}

bool ProviderRegistry::anyConnected() const {
  for (auto it = m_states.cbegin(); it != m_states.cend(); ++it)
    if (it.value().connected)
      return true;
  return false;
}

QString ProviderRegistry::defaultModelKey() const {
  return m_defaultModelKey;
}

void ProviderRegistry::setDefaultModelKey(const QString &key) {
  if (key == m_defaultModelKey)
    return;
  const ModelRef ref = ModelRef::fromKey(key);
  const ProviderState state = m_states.value(ref.providerId);
  if (!state.connected || !state.models.contains(ref.modelId) ||
      !state.models.value(ref.modelId).enabled) {
    emit error(QStringLiteral("Model is not available."));
    return;
  }
  m_defaultModelKey = key;
  save();
  emit defaultModelChanged();
}

QString ProviderRegistry::defaultProviderId() const {
  return ModelRef::fromKey(m_defaultModelKey).providerId;
}

QString ProviderRegistry::defaultModelId() const {
  return ModelRef::fromKey(m_defaultModelKey).modelId;
}

QNetworkRequest ProviderRegistry::modelRequest(const QString &providerId,
                                               const QString &baseUrl,
                                               const QString &apiKey) const {
  const ProviderDefinition def = definitions().value(providerId);
  QString url = trimmedBase(baseUrl.isEmpty() ? def.baseUrl : baseUrl) +
                def.modelsPath;
  QNetworkRequest request{QUrl(url)};
  request.setTransferTimeout(8000);
  if (def.auth == QLatin1String("bearer"))
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + apiKey.toUtf8());
  else if (def.auth == QLatin1String("anthropic")) {
    request.setRawHeader("x-api-key", apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
  }
  return request;
}

bool ProviderRegistry::connectProvider(const QString &id, const QString &apiKey,
                                       const QString &baseUrl) {
  const ProviderDefinition def = definitions().value(id);
  if (def.id.isEmpty()) {
    emit error(QStringLiteral("Unknown provider."));
    return false;
  }
  if (def.requiresKey && apiKey.trimmed().isEmpty()) {
    emit error(QStringLiteral("An API key is required."));
    return false;
  }
  const QString base = trimmedBase(baseUrl.isEmpty() ? def.baseUrl : baseUrl);
  if (base.isEmpty()) {
    emit error(QStringLiteral("A base URL is required."));
    return false;
  }
  if (!safeBaseUrl(base)) {
    emit error(QStringLiteral("Endpoint must use HTTPS or a loopback address."));
    return false;
  }
  QNetworkReply *reply = m_manager->get(modelRequest(id, base, apiKey));
  Pending pending;
  pending.providerId = id;
  pending.apiKey = apiKey;
  pending.baseUrl = base;
  pending.connectOnSuccess = true;
  m_pending.insert(reply, pending);
  connect(reply, &QNetworkReply::finished, this,
          &ProviderRegistry::onModelsRequestFinished);
  return true;
}

QStringList ProviderRegistry::parseModels(const QByteArray &data,
                                          const QString &providerId) const {
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    return {};
  const QJsonObject root = doc.object();
  QStringList ids;
  if (providerId == QLatin1String("ollama")) {
    for (const QJsonValue &value : root.value(QStringLiteral("models")).toArray()) {
      const QString name = value.toObject().value(QStringLiteral("name")).toString();
      if (!name.isEmpty())
        ids.append(name);
    }
  } else {
    for (const QJsonValue &value : root.value(QStringLiteral("data")).toArray()) {
      const QString id = value.toObject().value(QStringLiteral("id")).toString();
      if (!id.isEmpty())
        ids.append(id);
    }
  }
  return ids;
}

void ProviderRegistry::onModelsRequestFinished() {
  auto *reply = qobject_cast<QNetworkReply *>(sender());
  if (!reply || !m_pending.contains(reply))
    return;
  const Pending pending = m_pending.take(reply);
  const QByteArray data = reply->readAll();
  const bool networkError = reply->error() != QNetworkReply::NoError;
  const QString errorText = reply->errorString();
  reply->deleteLater();

  const ProviderDefinition def = definitions().value(pending.providerId);
  if (networkError) {
    // A failed probe only means "no local provider here" — it is not a user
    // error. A failed explicit connection must be reported.
    if (!pending.isLocalProbe)
      emit error(QStringLiteral("Cannot connect to %1: %2")
                     .arg(def.name, errorText));
    return;
  }
  QStringList ids = parseModels(data, pending.providerId);
  // Presets cover providers whose /models endpoint is unreliable. They are a
  // fallback for a *successful* response, never proof of a working connection.
  const bool usedPresets = ids.isEmpty();
  if (usedPresets)
    ids = def.presetModels;
  if (ids.isEmpty()) {
    if (!pending.isLocalProbe)
      emit error(QStringLiteral("No models were returned by %1.").arg(def.name));
    return;
  }

  if (pending.connectOnSuccess) {
    if (def.requiresKey && !m_secrets->set(secretId(pending.providerId),
                                           pending.apiKey)) {
      emit error(QStringLiteral("Could not store the API key."));
      return;
    }
    markConnected(pending.providerId, pending.baseUrl, pending.apiKey);
  }
  // Never prune against a preset list: an unreliable endpoint should not make
  // real models disappear.
  applyDiscoveredModels(pending.providerId, ids, !usedPresets);
  save();
  ++m_revision;
  emit providersChanged();
  emit modelsChanged();
}

void ProviderRegistry::markConnected(const QString &providerId,
                                     const QString &baseUrl,
                                     const QString &apiKey) {
  Q_UNUSED(apiKey);
  ProviderState state = m_states.value(providerId);
  const bool wasConnected = state.connected;
  state.connected = true;
  state.baseUrl = baseUrl;
  m_states.insert(providerId, state);
  if (!wasConnected) {
    emit connectivityChanged();
    emit connected(providerId);
  }
}

void ProviderRegistry::applyDiscoveredModels(const QString &providerId,
                                             const QStringList &ids,
                                             bool pruneMissing) {
  ProviderState state = m_states.value(providerId);
  for (const QString &id : ids) {
    if (id.isEmpty())
      continue;
    if (!state.models.contains(id)) {
      // Default: on. Keep the user's choice when a model reappears.
      ModelState model;
      model.label = id;
      model.enabled = true;
      state.models.insert(id, model);
    } else if (state.models.value(id).label == id) {
      // Preserve a friendly label if one was added manually.
      ModelState model = state.models.value(id);
      model.discovered = true;
      state.models.insert(id, model);
    }
  }
  if (pruneMissing) {
    QStringList stale;
    for (auto it = state.models.constBegin(); it != state.models.constEnd(); ++it) {
      if (it.value().discovered && !ids.contains(it.key()))
        stale.append(it.key());
    }
    for (const QString &id : stale) {
      state.models.remove(id);
      if (m_defaultModelKey == providerId + QStringLiteral("|") + id) {
        m_defaultModelKey.clear();
        emit defaultModelChanged();
      }
    }
  }
  m_states.insert(providerId, state);
}

void ProviderRegistry::disconnectProvider(const QString &id) {
  ProviderState state = m_states.value(id);
  if (!state.connected)
    return;
  m_secrets->remove(secretId(id));
  state.connected = false;
  m_states.insert(id, state);
  if (!m_defaultModelKey.isEmpty() &&
      ModelRef::fromKey(m_defaultModelKey).providerId == id) {
    m_defaultModelKey.clear();
    emit defaultModelChanged();
  }
  save();
  ++m_revision;
  emit connectivityChanged();
  emit providersChanged();
  emit modelsChanged();
  emit disconnected(id);
}

bool ProviderRegistry::setModelEnabled(const QString &providerId,
                                       const QString &modelId, bool enabled) {
  ProviderState state = m_states.value(providerId);
  if (!state.models.contains(modelId))
    return false;
  ModelState model = state.models.value(modelId);
  if (model.enabled == enabled)
    return true;
  model.enabled = enabled;
  state.models.insert(modelId, model);
  m_states.insert(providerId, state);
  if (!enabled && m_defaultModelKey == ModelRef(providerId, modelId).key()) {
    m_defaultModelKey.clear();
    emit defaultModelChanged();
  }
  save();
  ++m_revision;
  emit modelsChanged();
  return true;
}

bool ProviderRegistry::addModel(const QString &providerId,
                                const QString &modelId) {
  ProviderState state = m_states.value(providerId);
  if (modelId.trimmed().isEmpty())
    return false;
  if (!state.models.contains(modelId)) {
    ModelState model;
    model.label = modelId;
    model.enabled = true;
    model.discovered = false;
    state.models.insert(modelId, model);
    m_states.insert(providerId, state);
    save();
    ++m_revision;
    emit modelsChanged();
  }
  return true;
}

ResolvedModel ProviderRegistry::resolve(const ModelRef &ref,
                                        QString *error) const {
  ResolvedModel resolved;
  if (!ref.isValid()) {
    if (error)
      *error = QStringLiteral("No model selected.");
    return resolved;
  }
  const ProviderDefinition def = definitions().value(ref.providerId);
  if (def.id.isEmpty()) {
    if (error)
      *error = QStringLiteral("Unknown provider.");
    return resolved;
  }
  const ProviderState state = m_states.value(ref.providerId);
  if (!state.connected) {
    if (error)
      *error = QStringLiteral("Connect %1 before using AI.").arg(def.name);
    return resolved;
  }
  const ModelState model = state.models.value(ref.modelId);
  if (model.label.isEmpty()) {
    if (error)
      *error = QStringLiteral("Unknown model.");
    return resolved;
  }
  if (!model.enabled) {
    if (error)
      *error = QStringLiteral("That model is disabled.");
    return resolved;
  }
  resolved.ok = true;
  resolved.kind = def.kind;
  resolved.displayName = def.name;
  resolved.modelId = ref.modelId;
  resolved.chatUrl = trimmedBase(state.baseUrl.isEmpty() ? def.baseUrl
                                                         : state.baseUrl) +
                     def.chatPath;
  resolved.apiKey = m_secrets->get(secretId(ref.providerId));
  return resolved;
}

void ProviderRegistry::detectLocalProviders() {
  struct Probe {
    QString id;
    QString base;
  };
  const QVector<Probe> probes{
      {QStringLiteral("ollama"), QStringLiteral("http://127.0.0.1:11434")},
      {QStringLiteral("lm-studio"),
       QStringLiteral("http://127.0.0.1:1234/v1")}};
  for (const Probe &probe : probes) {
    const QString base = m_localEndpoints.value(probe.id, probe.base);
    QNetworkReply *reply = m_manager->get(modelRequest(probe.id, base, {}));
    Pending pending;
    pending.providerId = probe.id;
    pending.baseUrl = base;
    pending.isLocalProbe = true;
    pending.connectOnSuccess = true;
    m_pending.insert(reply, pending);
    connect(reply, &QNetworkReply::finished, this,
            &ProviderRegistry::onModelsRequestFinished);
  }
}

} // namespace Templnote::AI
