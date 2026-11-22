#include "PluginsModel.h"

namespace Templnote {

PluginsModel::PluginsModel(QObject *parent) : QAbstractListModel(parent) {}

int PluginsModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_plugins.size();
}

QVariant PluginsModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_plugins.size())
    return {};

  const PluginItem &item = m_plugins.at(index.row());
  const PluginManifest &m = item.manifest;

  switch (role) {
  case IdRole:
    return m.id();
  case NameRole:
    return m.name();
  case DescriptionRole:
    return m.description();
  case VersionRole:
    return m.version();
  case AuthorRole:
    return m.author();
  case EnabledRole:
    return item.enabled;
  case BundledRole:
    return m.isBundled();
  case CategoryRole:
    return m.category();
  case HasSettingsRole:
    return m.hasSettings();
  case IconRole:
    return m.iconUrl();
  default:
    return {};
  }
}

QHash<int, QByteArray> PluginsModel::roleNames() const {
  static const QHash<int, QByteArray> roles = {
    {IdRole, "id"},
    {NameRole, "name"},
    {DescriptionRole, "description"},
    {VersionRole, "version"},
    {AuthorRole, "author"},
    {EnabledRole, "enabled"},
    {BundledRole, "bundled"},
    {CategoryRole, "category"},
    {HasSettingsRole, "hasSettings"},
    {IconRole, "icon"}
  };
  return roles;
}

void PluginsModel::setPlugins(const QVector<PluginItem> &plugins) {
  beginResetModel();
  m_plugins = plugins;
  endResetModel();
  emit countChanged();
}

void PluginsModel::updatePluginEnabled(const QString &id, bool enabled) {
  for (int i = 0; i < m_plugins.size(); ++i) {
    if (m_plugins[i].manifest.id() == id) {
      if (m_plugins[i].enabled != enabled) {
        m_plugins[i].enabled = enabled;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx, {EnabledRole});
      }
      return;
    }
  }
}

const PluginItem *PluginsModel::findPlugin(const QString &id) const {
  for (const auto &item : m_plugins) {
    if (item.manifest.id() == id)
      return &item;
  }
  return nullptr;
}

} // namespace Templnote
