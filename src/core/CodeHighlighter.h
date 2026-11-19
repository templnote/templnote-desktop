#pragma once

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QQuickTextDocument>
#include <QTextCharFormat>
#include <QColor>
#include <qqmlintegration.h>

namespace Templnote {

class CodeHighlighter : public QSyntaxHighlighter {
  Q_OBJECT
  QML_ELEMENT

  Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
  Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
  explicit CodeHighlighter(QObject *parent = nullptr);

  QQuickTextDocument *textDocument() const { return m_quickTextDocument; }
  void setTextDocument(QQuickTextDocument *doc);

  QString language() const { return m_language; }
  void setLanguage(const QString &lang);

signals:
  void textDocumentChanged();
  void languageChanged();

protected:
  void highlightBlock(const QString &text) override;

private:
  struct HighlightRule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };

  void buildRules();

  QQuickTextDocument *m_quickTextDocument = nullptr;
  QString m_language;
  QVector<HighlightRule> m_rules;

  QRegularExpression m_commentStartPattern;
  QRegularExpression m_commentEndPattern;
  QTextCharFormat m_multiLineCommentFormat;
};

} // namespace Templnote
