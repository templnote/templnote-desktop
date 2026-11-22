#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "PluginManifest.h"

namespace Templnote {

struct PluginItem {
  PluginManifest manifest;
  bool enabled = false;
};

class PluginsModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    NameRole,
    DescriptionRole,
    VersionRole,
    AuthorRole,
    EnabledRole,
    BundledRole,
    CategoryRole,
    HasSettingsRole,
    IconRole
  };
  Q_ENUM(Roles)

  explicit PluginsModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  int count() const { return m_plugins.size(); }

  void setPlugins(const QVector<PluginItem> &plugins);
  void updatePluginEnabled(const QString &id, bool enabled);
  const PluginItem *findPlugin(const QString &id) const;

signals:
  void countChanged();

private:
  QVector<PluginItem> m_plugins;
};

} // namespace Templnote
