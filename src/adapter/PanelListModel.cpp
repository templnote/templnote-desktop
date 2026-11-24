#include "PanelListModel.h"

namespace Templnote::AI {

PanelListModel::PanelListModel(const QStringList &roles, QObject *parent)
    : QAbstractListModel(parent), m_roles(roles) {}

int PanelListModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_rows.size();
}

QVariant PanelListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
    return {};
  const int column = role - Qt::UserRole;
  if (column < 0 || column >= m_roles.size())
    return {};
  return m_rows.at(index.row()).toMap().value(m_roles.at(column));
}

QHash<int, QByteArray> PanelListModel::roleNames() const {
  QHash<int, QByteArray> names;
  for (int i = 0; i < m_roles.size(); ++i)
    names.insert(Qt::UserRole + i, m_roles.at(i).toUtf8());
  return names;
}

void PanelListModel::setRows(const QVariantList &rows) {
  const int previous = m_rows.size();
  beginResetModel();
  m_rows = rows;
  endResetModel();
  if (previous != m_rows.size())
    emit countChanged();
}

void PanelListModel::append(const QVariantMap &row) {
  const int rowIndex = m_rows.size();
  beginInsertRows(QModelIndex(), rowIndex, rowIndex);
  m_rows.append(row);
  endInsertRows();
  emit countChanged();
}

void PanelListModel::updateRow(int row, const QVariantMap &changes) {
  if (row < 0 || row >= m_rows.size() || changes.isEmpty())
    return;
  QVariantMap updated = m_rows.at(row).toMap();
  for (auto it = changes.constBegin(); it != changes.constEnd(); ++it)
    updated.insert(it.key(), it.value());
  m_rows.replace(row, updated);
  emit dataChanged(index(row), index(row));
}

void PanelListModel::clear() { setRows({}); }

} // namespace Templnote::AI
