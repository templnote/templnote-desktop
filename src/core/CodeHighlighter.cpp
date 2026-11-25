#include "CodeHighlighter.h"

namespace {
// buildRules() and highlightBlock() share these colours so the highlighter can
// recognise its own string/comment rules and give comments priority without
// losing string protection for identifiers and comment markers.
const QColor kStringColor("#98c379");
const QColor kCommentColor("#7f848e");

bool isStringFormat(const QTextCharFormat &format) {
  return format.foreground().color() == kStringColor;
}

bool isCommentFormat(const QTextCharFormat &format) {
  return format.foreground().color() == kCommentColor;
}
} // namespace

namespace Templnote {

CodeHighlighter::CodeHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent) {
  buildRules();
}

void CodeHighlighter::setTextDocument(QQuickTextDocument *doc) {
  if (m_quickTextDocument == doc)
    return;

  if (m_quickTextDocument) {
    disconnect(m_quickTextDocument, nullptr, this, nullptr);
  }

  m_quickTextDocument = doc;
  if (m_quickTextDocument) {
    connect(m_quickTextDocument, &QObject::destroyed, this, [this]() {
      m_quickTextDocument = nullptr;
      setDocument(nullptr);
    });
    setDocument(m_quickTextDocument->textDocument());
  } else {
    setDocument(nullptr);
  }
  emit textDocumentChanged();
}

void CodeHighlighter::setLanguage(const QString &lang) {
  const QString trimmed = lang.trimmed().toLower();
  if (m_language == trimmed)
    return;

  m_language = trimmed;
  buildRules();
  rehighlight();
  emit languageChanged();
}

void CodeHighlighter::buildRules() {
  m_rules.clear();
  m_commentStartPattern = QRegularExpression();
  m_commentEndPattern = QRegularExpression();

  // Color palette (OneDark / Dracula style vibrant tones)
  const QColor keywordColor("#e06c75");       // Red/Coral
  const QColor typeColor("#e5c07b");          // Yellow/Gold
  const QColor numberColor("#d19a66");        // Orange
  const QColor functionColor("#61afef");      // Cyan/Blue
  const QColor fenceColor("#5c6370");         // Dimmed Gray for markdown fences

  QTextCharFormat keywordFormat;
  keywordFormat.setForeground(keywordColor);
  keywordFormat.setFontWeight(QFont::Bold);

  QTextCharFormat typeFormat;
  typeFormat.setForeground(typeColor);

  QTextCharFormat functionFormat;
  functionFormat.setForeground(functionColor);

  QTextCharFormat stringFormat;
  stringFormat.setForeground(kStringColor);

  QTextCharFormat commentFormat;
  commentFormat.setForeground(kCommentColor);
  commentFormat.setFontItalic(true);

  QTextCharFormat numberFormat;
  numberFormat.setForeground(numberColor);

  QTextCharFormat fenceFormat;
  fenceFormat.setForeground(fenceColor);
  fenceFormat.setFontItalic(true);

  // Markdown fence lines: ^```.*$
  m_rules.append({QRegularExpression(QStringLiteral("^```.*$")), fenceFormat});

  // Numbers (int, float, hex)
  m_rules.append({QRegularExpression(QStringLiteral("\\b0[xX][0-9a-fA-F]+\\b")), numberFormat});
  m_rules.append({QRegularExpression(QStringLiteral("\\b[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?\\b")), numberFormat});

  const QString lang = m_language;

  // Collected per language and appended after the keyword/function rules so
  // its format wins over keyword-like text inside a single-line comment.
  QString lineCommentPattern;

  // Language specific or generalized keywords
  QStringList keywords;
  QStringList types;

  if (lang == QLatin1String("python") || lang == QLatin1String("py")) {
    keywords = {"def", "class", "import", "from", "as", "return", "if", "elif", "else",
                "for", "while", "break", "continue", "try", "except", "finally", "with",
                "lambda", "yield", "raise", "assert", "pass", "global", "nonlocal", "async",
                "await", "in", "is", "not", "and", "or", "True", "False", "None"};
    types = {"int", "float", "str", "list", "dict", "set", "tuple", "bool", "bytes", "object"};
    lineCommentPattern = QStringLiteral("#[^\n]*");
  } else if (lang == QLatin1String("javascript") || lang == QLatin1String("js") ||
             lang == QLatin1String("typescript") || lang == QLatin1String("ts")) {
    keywords = {"function", "return", "var", "let", "const", "if", "else", "for", "while",
                "do", "switch", "case", "break", "continue", "new", "delete", "typeof",
                "instanceof", "try", "catch", "finally", "throw", "class", "extends", "super",
                "import", "export", "default", "from", "as", "async", "await", "yield",
                "true", "false", "null", "undefined", "this"};
    types = {"string", "number", "boolean", "any", "void", "never", "unknown", "Promise", "Array", "Record"};
    lineCommentPattern = QStringLiteral("//[^\n]*");
    m_commentStartPattern = QRegularExpression(QStringLiteral("/\\*"));
    m_commentEndPattern = QRegularExpression(QStringLiteral("\\*/"));
  } else if (lang == QLatin1String("cpp") || lang == QLatin1String("c++") ||
             lang == QLatin1String("c") || lang == QLatin1String("h") || lang == QLatin1String("hpp")) {
    keywords = {"auto", "break", "case", "class", "const", "constexpr", "continue", "default",
                "delete", "do", "else", "enum", "explicit", "export", "extern", "false", "for",
                "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept",
                "nullptr", "operator", "private", "protected", "public", "register",
                "reinterpret_cast", "return", "sizeof", "static", "static_assert", "static_cast",
                "struct", "switch", "template", "this", "throw", "true", "try", "typedef",
                "typeid", "typename", "union", "using", "virtual", "volatile", "while"};
    types = {"bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int",
             "long", "short", "signed", "unsigned", "void", "wchar_t", "size_t", "int8_t",
             "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
             "QString", "QByteArray", "QVector", "QList", "std"};
    lineCommentPattern = QStringLiteral("//[^\n]*");
    m_commentStartPattern = QRegularExpression(QStringLiteral("/\\*"));
    m_commentEndPattern = QRegularExpression(QStringLiteral("\\*/"));
  } else if (lang == QLatin1String("rust") || lang == QLatin1String("rs")) {
    keywords = {"as", "break", "const", "continue", "crate", "else", "enum", "extern", "false",
                "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut",
                "pub", "ref", "return", "self", "Self", "static", "struct", "super", "trait",
                "true", "type", "unsafe", "use", "where", "while", "async", "await", "dyn"};
    types = {"bool", "char", "i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32",
             "u64", "u128", "usize", "f32", "f64", "str", "String", "Vec", "Option", "Result"};
    lineCommentPattern = QStringLiteral("//[^\n]*");
    m_commentStartPattern = QRegularExpression(QStringLiteral("/\\*"));
    m_commentEndPattern = QRegularExpression(QStringLiteral("\\*/"));
  } else if (lang == QLatin1String("bash") || lang == QLatin1String("sh") || lang == QLatin1String("zsh")) {
    keywords = {"if", "then", "else", "elif", "fi", "case", "esac", "for", "while", "until",
                "do", "done", "in", "function", "select", "time", "return", "exit", "echo", "local"};
    lineCommentPattern = QStringLiteral("#[^\n]*");
  } else if (lang == QLatin1String("json")) {
    keywords = {"true", "false", "null"};
  } else if (lang == QLatin1String("sql")) {
    keywords = {"select", "from", "where", "insert", "into", "update", "delete", "create",
                "table", "drop", "alter", "join", "inner", "left", "right", "outer", "group",
                "by", "order", "asc", "desc", "having", "limit", "and", "or", "not", "null"};
    lineCommentPattern = QStringLiteral("--[^\n]*");
  } else {
    // Generic fallback for any other language
    keywords = {"if", "else", "for", "while", "return", "function", "def", "class", "import",
                "true", "false", "null", "nil", "None", "let", "const", "var"};
    lineCommentPattern = QStringLiteral("(//|#)[^\n]*");
  }

  for (const QString &kw : keywords) {
    // Escape keywords so regex metacharacters (e.g. C++ operator names) are
    // matched literally instead of altering the pattern.
    const QString pattern =
        QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(kw));
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (lang == QLatin1String("sql"))
      options |= QRegularExpression::CaseInsensitiveOption;
    m_rules.append({QRegularExpression(pattern, options), keywordFormat});
  }

  for (const QString &t : types) {
    const QString pattern =
        QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(t));
    m_rules.append({QRegularExpression(pattern), typeFormat});
  }

  // Function calls identifier(...)
  m_rules.append({QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_]+(?=\\s*\\()")), functionFormat});

  // Comments and strings are applied after the keyword/type/function rules:
  // setFormat() overwrites earlier formats, so applying these last keeps
  // keywords inside comments and identifiers inside strings from being
  // recolored. highlightBlock() filters their matches against computed
  // comment/string ranges so a string inside a comment stays a comment while
  // a comment marker inside a string still resolves to the string format.
  if (!lineCommentPattern.isEmpty())
    m_rules.append({QRegularExpression(lineCommentPattern), commentFormat});

  // Strings: double quoted, single quoted, backtick
  m_rules.append({QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"])*\"")), stringFormat});
  m_rules.append({QRegularExpression(QStringLiteral("'(?:\\\\.|[^'])*'")), stringFormat});
  m_rules.append({QRegularExpression(QStringLiteral("`(?:\\\\.|[^`])*`")), stringFormat});

  m_multiLineCommentFormat = commentFormat;
}

void CodeHighlighter::highlightBlock(const QString &text) {
  struct Range {
    int start = 0;
    int end = 0;
  };

  // Recover the string and single-line-comment rules from the built rule list;
  // they are applied below with range filtering instead of in the plain loop.
  QVector<const QRegularExpression *> stringPatterns;
  const QRegularExpression *lineCommentPattern = nullptr;
  QTextCharFormat stringFormat;
  QTextCharFormat commentFormat;

  for (const HighlightRule &rule : m_rules) {
    if (isStringFormat(rule.format)) {
      stringPatterns.append(&rule.pattern);
      stringFormat = rule.format;
    } else if (isCommentFormat(rule.format)) {
      lineCommentPattern = &rule.pattern;
      commentFormat = rule.format;
    }
  }

  // Strings are independent of comment ranges, so collect them first: both the
  // block-comment scan and the line-comment walk need them to avoid treating
  // delimiters inside quotes as real markers.
  QVector<Range> strings;
  QVector<Range> comments;

  // When a block comment started on an earlier line, a `//` here is ordinary
  // comment text, not a single-line comment. Detecting it as one would create a
  // range covering the rest of the line and mask the closing `*/`, so the
  // comment would never close and every later line stayed grey.
  const bool continuingMultiLineComment = previousBlockState() == 1;

  int scan = 0;

  while (scan < text.size()) {
    int nextString = -1;
    int stringEnd = -1;

    for (const QRegularExpression *pattern : stringPatterns) {
      const QRegularExpressionMatch match = pattern->match(text, scan);

      if (match.hasMatch() &&
          (nextString < 0 || match.capturedStart() < nextString)) {
        nextString = match.capturedStart();
        stringEnd = match.capturedEnd();
      }
    }

    if (nextString < 0)
      break;

    strings.append({nextString, stringEnd});
    scan = stringEnd;
  }

  auto startsInside = [](int position, const QVector<Range> &ranges) {
    for (const Range &range : ranges) {
      if (range.start > position)
        break;
      if (position < range.end)
        return true;
    }
    return false;
  };

  // Finds the next match whose start is not masked by either range list. Keep
  // the lists separate: startsInside() relies on each one being ordered by
  // start, and strings/comments can interleave on the same line.
  auto nextMatchOutside = [&](const QRegularExpression &pattern, int from,
                              const QVector<Range> &first,
                              const QVector<Range> &second) {
    int at = from;

    while (at >= 0 && at <= text.size()) {
      const QRegularExpressionMatch match = pattern.match(text, at);

      if (!match.hasMatch())
        break;

      const int start = match.capturedStart();

      if (startsInside(start, first) || startsInside(start, second)) {
        at = match.capturedEnd();

        if (at <= start)
          break;

        continue;
      }

      return match;
    }

    return QRegularExpressionMatch();
  };

  // On a continuation line, a `//` before the closing `*/` is ordinary comment
  // text, but a `//` AFTER the close is a real line comment. Find the closing
  // delimiter first (strings are already known) so the walk below can enable
  // line-comment detection only for the suffix.
  int continuationCloseEnd = -1;

  if (continuingMultiLineComment && !m_commentEndPattern.pattern().isEmpty()) {
    const QRegularExpressionMatch endMatch =
        nextMatchOutside(m_commentEndPattern, 0, strings, {});

    if (endMatch.hasMatch())
      continuationCloseEnd = endMatch.capturedEnd();
  }

  // On the line where `/*` opens, a `//` inside that block comment must not
  // mask the closing `*/`. Discover the block-comment spans using only string
  // masking (line-comment ranges do not exist yet) and record where each span
  // closes. A line-comment range starting inside a span stops before the `*/`.
  QVector<Range> blockCommentSpans; // end = index of `*/`, or end of line

  if (!continuingMultiLineComment &&
      !m_commentStartPattern.pattern().isEmpty()) {
    int from = 0;

    while (from < text.size()) {
      const QRegularExpressionMatch startMatch =
          nextMatchOutside(m_commentStartPattern, from, strings, {});

      if (!startMatch.hasMatch())
        break;

      const QRegularExpressionMatch endMatch = nextMatchOutside(
          m_commentEndPattern, startMatch.capturedEnd(), strings, {});
      const int closeStart = endMatch.hasMatch()
                                 ? static_cast<int>(endMatch.capturedStart())
                                 : static_cast<int>(text.size());

      blockCommentSpans.append(
          {static_cast<int>(startMatch.capturedStart()), closeStart});

      if (!endMatch.hasMatch())
        break;

      from = endMatch.capturedEnd();
    }
  }

  // Walk the line left to right so string and comment ranges never overlap:
  // a quote after // belongs to the comment, but // inside a quote is text.
  // On a continuation line, only the suffix after the closing */ can contain
  // a real line comment; starting the walk there also avoids a `//` before
  // the close being mistaken for one.
  int pos = (continuingMultiLineComment && continuationCloseEnd >= 0)
                ? continuationCloseEnd
                : 0;

  while (pos < text.size()) {
    const bool lineCommentsActive =
        lineCommentPattern &&
        (!continuingMultiLineComment ||
         (continuationCloseEnd >= 0 && pos >= continuationCloseEnd));
    const int nextComment =
        lineCommentsActive ? text.indexOf(*lineCommentPattern, pos) : -1;

    if (nextComment < 0)
      break;

    // A `//` inside a string is string text, not a comment marker.
    bool insideString = false;

    for (const Range &stringRange : strings) {
      if (nextComment >= stringRange.start &&
          nextComment < stringRange.end) {
        pos = stringRange.end;
        insideString = true;
        break;
      }
    }

    if (insideString)
      continue;

    int commentEnd = text.size();

    for (const Range &span : blockCommentSpans) {
      if (nextComment > span.start && nextComment < span.end) {
        // Stop before the closing `*/` so it stays visible to the state
        // machine below. Text after it is ordinary code again.
        commentEnd = span.end;
        break;
      }
    }

    comments.append({nextComment, commentEnd});

    if (commentEnd >= text.size())
      break;

    pos = commentEnd;
  }

  for (const HighlightRule &rule : m_rules) {
    if (isStringFormat(rule.format) || isCommentFormat(rule.format))
      continue; // applied below, after range filtering

    QRegularExpressionMatchIterator matchIterator =
        rule.pattern.globalMatch(text);

    while (matchIterator.hasNext()) {
      const QRegularExpressionMatch match = matchIterator.next();

      if (startsInside(match.capturedStart(), strings) ||
          startsInside(match.capturedStart(), comments))
        continue;

      setFormat(match.capturedStart(), match.capturedLength(), rule.format);
    }
  }

  // Strings first: identifiers inside them keep the string colour. Then the
  // single-line comment range wins, including any quotes it contains.
  for (const Range &range : strings)
    setFormat(range.start, range.end - range.start, stringFormat);

  for (const Range &range : comments)
    setFormat(range.start, range.end - range.start, commentFormat);

  // A raw indexOf()/match() would treat delimiters inside strings and single
  // line comments as real comment markers: `auto s = "/*";` would open a
  // multi-line comment and a `*/` inside a string would close one. Exclude both
  // sets of ranges from the state-machine scan below.

  // Handle multi-line comments if configured
  if (!m_commentStartPattern.pattern().isEmpty()) {
    // Continuing from the previous block starts at the beginning of this one;
    // strings detected here are still masked so a quoted `*/` cannot end it.
    int startIndex = 0;

    if (previousBlockState() != 1) {
      const QRegularExpressionMatch startMatch =
          nextMatchOutside(m_commentStartPattern, 0, strings, comments);
      startIndex = startMatch.hasMatch() ? startMatch.capturedStart() : -1;
    }

    int state = 0;
    while (startIndex >= 0) {
      const QRegularExpressionMatch endMatch =
          nextMatchOutside(m_commentEndPattern, startIndex, strings, comments);
      const int endIndex = endMatch.hasMatch() ? endMatch.capturedStart() : -1;
      int commentLength = 0;

      if (endIndex < 0) {
        state = 1;
        commentLength = text.length() - startIndex;
      } else {
        commentLength = endIndex - startIndex + endMatch.capturedLength();
      }

      setFormat(startIndex, commentLength, m_multiLineCommentFormat);

      if (endIndex < 0)
        break;

      const QRegularExpressionMatch nextStart =
          nextMatchOutside(m_commentStartPattern, startIndex + commentLength,
                           strings, comments);
      startIndex = nextStart.hasMatch() ? nextStart.capturedStart() : -1;
    }
    setCurrentBlockState(state);
  } else {
    setCurrentBlockState(0);
  }
}

} // namespace Templnote
