#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace Templnote {

// Local Emojibase dataset used by the colon-triggered picker and by inline
// :shortcode: rendering. The JSON files are vendored as Qt resources, so the
// editor never needs network access.
class EmojiDatabase : public QObject {
  Q_OBJECT
public:
  struct Entry {
    QString label;
    QString emoji;
    QString hexcode;
    QString shortcode;
    int group = 0;
    QStringList tags;
  };

  explicit EmojiDatabase(QObject *parent = nullptr);

  Q_INVOKABLE int count() const;
  Q_INVOKABLE QVariantList groups() const;
  Q_INVOKABLE QVariantList search(const QString &query, int limit = 60) const;
  Q_INVOKABLE QVariantList forGroup(int group, int limit = 240) const;
  Q_INVOKABLE QString shortcodeToEmoji(const QString &shortcode) const;

private:
  void load();
  QVariantMap toMap(const Entry &entry) const;

  QVector<Entry> m_entries;
  QVector<QString> m_groups;
  QHash<QString, QString> m_shortcodeToEmoji;
  QHash<QString, QString> m_hexToShortcode;
};

} // namespace Templnote
