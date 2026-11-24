#pragma once

// Small QAbstractListModel for the AI panel. Replacing value lists with a real
// model keeps delegates alive while streaming/background events append rows.

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace Templnote::AI {

class PanelListModel : public QAbstractListModel {
  Q_OBJECT
  // QML consumers use count exactly like ListModel.count.
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
  explicit PanelListModel(const QStringList &roles, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setRows(const QVariantList &rows);
  void append(const QVariantMap &row);
  void updateRow(int row, const QVariantMap &changes);
  void clear();
  QVariantList rows() const { return m_rows; }
  Q_INVOKABLE QVariantMap row(int index) const {
    return index >= 0 && index < m_rows.size() ? m_rows.at(index).toMap()
                                               : QVariantMap{};
  }

signals:
  void countChanged();

private:
  QStringList m_roles;
  QVariantList m_rows;
};

} // namespace Templnote::AI
