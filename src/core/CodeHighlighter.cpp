#include "CodeHighlighter.h"

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
  const QColor stringColor("#98c379");        // Sage Green
  const QColor commentColor("#7f848e");       // Muted Gray
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
  stringFormat.setForeground(stringColor);

  QTextCharFormat commentFormat;
  commentFormat.setForeground(commentColor);
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

  // Language specific or generalized keywords
  QStringList keywords;
  QStringList types;

  if (lang == QLatin1String("python") || lang == QLatin1String("py")) {
    keywords = {"def", "class", "import", "from", "as", "return", "if", "elif", "else",
                "for", "while", "break", "continue", "try", "except", "finally", "with",
                "lambda", "yield", "raise", "assert", "pass", "global", "nonlocal", "async",
                "await", "in", "is", "not", "and", "or", "True", "False", "None"};
    types = {"int", "float", "str", "list", "dict", "set", "tuple", "bool", "bytes", "object"};
    m_rules.append({QRegularExpression(QStringLiteral("#[^\n]*")), commentFormat});
  } else if (lang == QLatin1String("javascript") || lang == QLatin1String("js") ||
             lang == QLatin1String("typescript") || lang == QLatin1String("ts")) {
    keywords = {"function", "return", "var", "let", "const", "if", "else", "for", "while",
                "do", "switch", "case", "break", "continue", "new", "delete", "typeof",
                "instanceof", "try", "catch", "finally", "throw", "class", "extends", "super",
                "import", "export", "default", "from", "as", "async", "await", "yield",
                "true", "false", "null", "undefined", "this"};
    types = {"string", "number", "boolean", "any", "void", "never", "unknown", "Promise", "Array", "Record"};
    m_rules.append({QRegularExpression(QStringLiteral("//[^\n]*")), commentFormat});
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
    m_rules.append({QRegularExpression(QStringLiteral("//[^\n]*")), commentFormat});
    m_commentStartPattern = QRegularExpression(QStringLiteral("/\\*"));
    m_commentEndPattern = QRegularExpression(QStringLiteral("\\*/"));
  } else if (lang == QLatin1String("rust") || lang == QLatin1String("rs")) {
    keywords = {"as", "break", "const", "continue", "crate", "else", "enum", "extern", "false",
                "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut",
                "pub", "ref", "return", "self", "Self", "static", "struct", "super", "trait",
                "true", "type", "unsafe", "use", "where", "while", "async", "await", "dyn"};
    types = {"bool", "char", "i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32",
             "u64", "u128", "usize", "f32", "f64", "str", "String", "Vec", "Option", "Result"};
    m_rules.append({QRegularExpression(QStringLiteral("//[^\n]*")), commentFormat});
    m_commentStartPattern = QRegularExpression(QStringLiteral("/\\*"));
    m_commentEndPattern = QRegularExpression(QStringLiteral("\\*/"));
  } else if (lang == QLatin1String("bash") || lang == QLatin1String("sh") || lang == QLatin1String("zsh")) {
    keywords = {"if", "then", "else", "elif", "fi", "case", "esac", "for", "while", "until",
                "do", "done", "in", "function", "select", "time", "return", "exit", "echo", "local"};
    m_rules.append({QRegularExpression(QStringLiteral("#[^\n]*")), commentFormat});
  } else if (lang == QLatin1String("json")) {
    keywords = {"true", "false", "null"};
  } else if (lang == QLatin1String("sql")) {
    keywords = {"select", "from", "where", "insert", "into", "update", "delete", "create",
                "table", "drop", "alter", "join", "inner", "left", "right", "outer", "group",
                "by", "order", "asc", "desc", "having", "limit", "and", "or", "not", "null"};
    m_rules.append({QRegularExpression(QStringLiteral("--[^\n]*")), commentFormat});
  } else {
    // Generic fallback for any other language
    keywords = {"if", "else", "for", "while", "return", "function", "def", "class", "import",
                "true", "false", "null", "nil", "None", "let", "const", "var"};
    m_rules.append({QRegularExpression(QStringLiteral("(//|#)[^\n]*")), commentFormat});
  }

  for (const QString &kw : keywords) {
    const QString pattern = QStringLiteral("\\b%1\\b").arg(kw);
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (lang == QLatin1String("sql"))
      options |= QRegularExpression::CaseInsensitiveOption;
    m_rules.append({QRegularExpression(pattern, options), keywordFormat});
  }

  for (const QString &t : types) {
    const QString pattern = QStringLiteral("\\b%1\\b").arg(t);
    m_rules.append({QRegularExpression(pattern), typeFormat});
  }

  // Function calls identifier(...)
  m_rules.append({QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_]+(?=\\s*\\()")), functionFormat});

  // Strings: double quoted, single quoted, backtick
  m_rules.append({QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"])*\"")), stringFormat});
  m_rules.append({QRegularExpression(QStringLiteral("'(?:\\\\.|[^'])*'")), stringFormat});
  m_rules.append({QRegularExpression(QStringLiteral("`(?:\\\\.|[^`])*`")), stringFormat});

  m_multiLineCommentFormat = commentFormat;
}

void CodeHighlighter::highlightBlock(const QString &text) {
  for (const HighlightRule &rule : m_rules) {
    QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
    while (matchIterator.hasNext()) {
      QRegularExpressionMatch match = matchIterator.next();
      setFormat(match.capturedStart(), match.capturedLength(), rule.format);
    }
  }

  // Handle multi-line comments if configured
  if (!m_commentStartPattern.pattern().isEmpty()) {
    int startIndex = 0;
    if (previousBlockState() == 1) {
      startIndex = 0;
    } else {
      startIndex = text.indexOf(m_commentStartPattern);
    }

    int state = 0;
    while (startIndex >= 0) {
      QRegularExpressionMatch match = m_commentEndPattern.match(text, startIndex);
      int endIndex = match.capturedStart();
      int commentLength = 0;
      if (endIndex == -1) {
        state = 1;
        commentLength = text.length() - startIndex;
      } else {
        commentLength = endIndex - startIndex + match.capturedLength();
      }
      setFormat(startIndex, commentLength, m_multiLineCommentFormat);
      if (endIndex == -1) {
        break;
      }
      startIndex = text.indexOf(m_commentStartPattern, startIndex + commentLength);
    }
    setCurrentBlockState(state);
  } else {
    setCurrentBlockState(0);
  }
}

} // namespace Templnote
