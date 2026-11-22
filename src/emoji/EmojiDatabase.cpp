#include "EmojiDatabase.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace Templnote {
namespace {
QByteArray readResource(const QStringList &candidates) {
  for (const QString &path : candidates) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
      return file.readAll();
  }
  return {};
}

QJsonDocument parseJson(const QByteArray &data) {
  QJsonParseError error;
  QJsonDocument document = QJsonDocument::fromJson(data, &error);
  return error.error == QJsonParseError::NoError ? document : QJsonDocument{};
}

QString normalizeShortcode(const QString &value) {
  QString result = value.trimmed().toLower();
  result.remove(QLatin1Char(':'));
  return result;
}
} // namespace

EmojiDatabase::EmojiDatabase(QObject *parent) : QObject(parent) { load(); }

void EmojiDatabase::load() {
  const QJsonArray emojis = parseJson(readResource({
      QStringLiteral(":/qt/qml/Templnote/assets/emoji/data.json"),
      QStringLiteral(":/assets/emoji/data.json"),
  })).array();

  const QJsonObject messages = parseJson(readResource({
      QStringLiteral(":/qt/qml/Templnote/assets/emoji/messages.json"),
      QStringLiteral(":/assets/emoji/messages.json"),
  })).object();

  const QJsonObject shortcodes = parseJson(readResource({
      QStringLiteral(":/qt/qml/Templnote/assets/emoji/shortcodes-github.json"),
      QStringLiteral(":/assets/emoji/shortcodes-github.json"),
  })).object();

  for (auto it = shortcodes.constBegin(); it != shortcodes.constEnd(); ++it)
    m_hexToShortcode.insert(it.key(), it.value().toString());

  const QJsonArray groupMessages = messages.value(QStringLiteral("groups")).toArray();
  for (const QJsonValue &value : groupMessages) {
    const QJsonObject group = value.toObject();
    const int order = group.value(QStringLiteral("order")).toInt(m_groups.size());
    while (m_groups.size() <= order)
      m_groups.append(QString());
    m_groups[order] = group.value(QStringLiteral("message")).toString();
  }

  for (const QJsonValue &value : emojis) {
    const QJsonObject object = value.toObject();
    Entry entry;
    entry.label = object.value(QStringLiteral("label")).toString();
    entry.emoji = object.value(QStringLiteral("emoji")).toString();
    entry.hexcode = object.value(QStringLiteral("hexcode")).toString();
    entry.group = object.value(QStringLiteral("group")).toInt();

    const QJsonArray tags = object.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue &tag : tags)
      entry.tags.append(tag.toString());

    entry.shortcode = m_hexToShortcode.value(entry.hexcode);
    if (entry.shortcode.isEmpty()) {
      entry.shortcode = entry.label.trimmed().toLower().replace(
          QRegularExpression(QStringLiteral("[^a-z0-9]+")),
          QStringLiteral("_"));
    }

    m_entries.append(entry);

    if (!entry.shortcode.isEmpty()) {
      // The canonical shortcode is authoritative. Label aliases are added
      // only when they do not already resolve to something (typically a
      // canonical shortcode), so equal labels cannot hijack :shortcode:.
      const QString shortcodeKey = normalizeShortcode(entry.shortcode);
      m_shortcodeToEmoji.insert(shortcodeKey, entry.emoji);

      const QString labelKey = normalizeShortcode(entry.label);
      if (!labelKey.isEmpty() && !m_shortcodeToEmoji.contains(labelKey))
        m_shortcodeToEmoji.insert(labelKey, entry.emoji);
    }
  }
}

int EmojiDatabase::count() const { return m_entries.size(); }

QVariantMap EmojiDatabase::toMap(const Entry &entry) const {
  QVariantMap map;
  map.insert(QStringLiteral("label"), entry.label);
  map.insert(QStringLiteral("emoji"), entry.emoji);
  map.insert(QStringLiteral("hexcode"), entry.hexcode);
  map.insert(QStringLiteral("shortcode"), entry.shortcode);
  map.insert(QStringLiteral("group"), entry.group);
  map.insert(QStringLiteral("groupName"),
             entry.group >= 0 && entry.group < m_groups.size()
                 ? m_groups.at(entry.group) : QString());
  return map;
}

QVariantList EmojiDatabase::search(const QString &query, int limit) const {
  const QString needle = query.trimmed().toLower();
  QVariantList result;
  if (limit <= 0)
    return result;

  for (const Entry &entry : m_entries) {
    if (result.size() >= limit)
      break;

    if (needle.isEmpty()) {
      result.append(toMap(entry));
      continue;
    }

    if (entry.label.contains(needle, Qt::CaseInsensitive) ||
        entry.shortcode.contains(needle, Qt::CaseInsensitive)) {
      result.append(toMap(entry));
      continue;
    }

    for (const QString &tag : entry.tags) {
      if (tag.contains(needle, Qt::CaseInsensitive)) {
        result.append(toMap(entry));
        break;
      }
    }
  }
  return result;
}

QVariantList EmojiDatabase::forGroup(int group, int limit) const {
  QVariantList result;
  for (const Entry &entry : m_entries) {
    if (entry.group != group)
      continue;
    if (result.size() >= limit)
      break;
    result.append(toMap(entry));
  }
  return result;
}

QVariantList EmojiDatabase::groups() const {
  QVariantList result;
  for (int i = 0; i < m_groups.size(); ++i) {
    QVariantMap group;
    group.insert(QStringLiteral("id"), i);
    group.insert(QStringLiteral("name"), m_groups.at(i));
    result.append(group);
  }
  return result;
}

QString EmojiDatabase::shortcodeToEmoji(const QString &shortcode) const {
  return m_shortcodeToEmoji.value(normalizeShortcode(shortcode));
}

} // namespace Templnote
