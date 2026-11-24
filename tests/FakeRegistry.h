#pragma once

// Minimal ProviderRegistry stand-in for the prompt-panel model picker test.

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class FakeRegistry : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList models READ models CONSTANT)
  Q_PROPERTY(QString defaultModelKey READ defaultModelKey WRITE
                 setDefaultModelKey NOTIFY defaultModelChanged)
  Q_PROPERTY(bool anyConnected READ anyConnected CONSTANT)
  Q_PROPERTY(int revision READ revision CONSTANT)

public:
  explicit FakeRegistry(QObject *parent = nullptr) : QObject(parent) {
    QVariantMap model;
    model.insert(QStringLiteral("key"), QStringLiteral("google|gemini-4-hydrogen"));
    model.insert(QStringLiteral("providerId"), QStringLiteral("google"));
    model.insert(QStringLiteral("providerName"), QStringLiteral("Google"));
    model.insert(QStringLiteral("modelId"), QStringLiteral("gemini-4-hydrogen"));
    model.insert(QStringLiteral("label"), QStringLiteral("Gemini 4 Hydrogen"));
    model.insert(QStringLiteral("enabled"), true);
    model.insert(QStringLiteral("connected"), true);
    m_models.append(model);

    QVariantMap other = model;
    other.insert(QStringLiteral("key"), QStringLiteral("google|gemini-3.5-flash-lite"));
    other.insert(QStringLiteral("modelId"), QStringLiteral("gemini-3.5-flash-lite"));
    other.insert(QStringLiteral("label"), QStringLiteral("Gemini 3.5 Flash-Lite"));
    m_models.append(other);
  }

  QVariantList models() const { return m_models; }
  bool anyConnected() const { return true; }
  int revision() const { return 1; }

  QString defaultModelKey() const { return m_defaultModelKey; }
  void setDefaultModelKey(const QString &key) {
    if (m_defaultModelKey == key)
      return;
    m_defaultModelKey = key;
    emit defaultModelChanged();
  }

  Q_INVOKABLE QVariantList enabledModels() const { return m_models; }

  Q_INVOKABLE QVariantList thinkingLevels(const QString &key) const {
    QVariantList levels;
    const auto add = [&levels](const QString &value, const QString &label) {
      QVariantMap entry;
      entry.insert(QStringLiteral("value"), value);
      entry.insert(QStringLiteral("label"), label);
      levels.append(entry);
    };
    add(QString(), QStringLiteral("Default"));
    if (key.contains(QStringLiteral("hydrogen"))) {
      add(QStringLiteral("low"), QStringLiteral("Low"));
      add(QStringLiteral("medium"), QStringLiteral("Medium"));
      add(QStringLiteral("high"), QStringLiteral("High"));
      add(QStringLiteral("xhigh"), QStringLiteral("Xhigh"));
    } else {
      add(QStringLiteral("minimal"), QStringLiteral("Minimal"));
      add(QStringLiteral("low"), QStringLiteral("Low"));
      add(QStringLiteral("medium"), QStringLiteral("Medium"));
      add(QStringLiteral("high"), QStringLiteral("High"));
    }
    return levels;
  }

  Q_INVOKABLE QString modelThinkingLevel(const QString &provider,
                                         const QString &model) const {
    return m_levels.value(provider + QLatin1Char('|') + model);
  }

  Q_INVOKABLE bool setModelThinkingLevel(const QString &provider,
                                         const QString &model,
                                         const QString &level) {
    m_levels.insert(provider + QLatin1Char('|') + model, level);
    return true;
  }

  Q_INVOKABLE QVariantMap modelDetails(const QString &key) const {
    QVariantMap details;
    for (const QVariant &value : m_models) {
      const QVariantMap model = value.toMap();
      if (model.value(QStringLiteral("key")).toString() == key) {
        details = model;
        break;
      }
    }
    details.insert(QStringLiteral("context"), QStringLiteral("1M unified context"));
    details.insert(QStringLiteral("modalities"),
                   QStringLiteral("Text, Image, PDF, Audio, Video, Tools"));
    return details;
  }

signals:
  void defaultModelChanged();

private:
  QVariantList m_models;
  QString m_defaultModelKey = QStringLiteral("google|gemini-4-hydrogen");
  QHash<QString, QString> m_levels;
};
