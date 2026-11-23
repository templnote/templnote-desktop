#include "Commands.h"

#include "core/Block.h"

#include <QRegularExpression>
#include <algorithm>

namespace Templnote::Engine::Commands {

namespace {
Op makeOp(const Command &cmd, Op::Type type, const QString &blockId) {
  Op op;
  op.type = type;
  op.blockId = blockId;
  op.actor = cmd.actor;
  op.lamport = cmd.lamport;
  return op;
}

bool needsBlock(Command::Type type) {
  switch (type) {
  case Command::Type::InsertText:
  case Command::Type::DeleteRange:
  case Command::Type::ReplaceRange:
  case Command::Type::ToggleFormat:
  case Command::Type::SetBlockType:
  case Command::Type::SetAlignment:
  case Command::Type::SetMetadata:
  case Command::Type::SplitBlock:
  case Command::Type::MergeWithPrevious:
  case Command::Type::RemoveBlock:
  case Command::Type::MoveBlock:
  case Command::Type::SetText:
  case Command::Type::SetChecked:
  case Command::Type::SetCalloutType:
  case Command::Type::SetCodeLanguage:
  case Command::Type::SetImageSource:
  case Command::Type::SetImageWidthRatio:
    return true;
  default:
    return false;
  }
}
} // namespace

QString validate(const Document &doc, const Command &cmd) {
  if (needsBlock(cmd.type) && cmd.blockId.isEmpty())
    return QStringLiteral("Missing blockId.");

  const Block *block =
      cmd.blockId.isEmpty() ? nullptr : doc.findBlock(cmd.blockId);

  if (needsBlock(cmd.type) && !block)
    return QStringLiteral("Block not found.");

  const int len = block ? block->text.size() : 0;

  switch (cmd.type) {
  case Command::Type::InsertText:
    if (cmd.position < 0 || cmd.position > len)
      return QStringLiteral("Insert position out of range.");
    if (cmd.text.isEmpty())
      return QStringLiteral("Nothing to insert.");
    break;
  case Command::Type::DeleteRange:
  case Command::Type::ReplaceRange:
    if (cmd.start < 0 || cmd.end < cmd.start || cmd.end > len)
      return QStringLiteral("Invalid range.");
    break;
  case Command::Type::ToggleFormat:
    if (cmd.start < 0 || cmd.end < cmd.start || cmd.end > len)
      return QStringLiteral("Invalid format range.");
    if (cmd.format.isEmpty())
      return QStringLiteral("Missing format name.");
    break;
  case Command::Type::SplitBlock:
    if (cmd.position < 0 || cmd.position > len)
      return QStringLiteral("Split position out of range.");
    break;
  case Command::Type::SetBlockType:
    if (!isValidBlockType(cmd.blockType))
      return QStringLiteral("Invalid block type.");
    if (cmd.headingLevel < 0 || cmd.headingLevel > 6)
      return QStringLiteral("Heading level out of range.");
    break;
  case Command::Type::InsertBlock:
    if (!cmd.afterId.isEmpty() && !doc.findBlock(cmd.afterId))
      return QStringLiteral("Reference block not found.");
    if (!isValidBlockType(cmd.blockType))
      return QStringLiteral("Invalid block type.");
    break;
  case Command::Type::MergeWithPrevious:
    if (doc.indexOf(cmd.blockId) <= 0)
      return QStringLiteral("No previous block to merge with.");
    break;
  case Command::Type::MoveBlock:
    if (cmd.destination < 0 || cmd.destination >= doc.count())
      return QStringLiteral("Move destination out of range.");
    break;
  case Command::Type::ApplyJson:
    if (cmd.text.isEmpty())
      return QStringLiteral("Missing document JSON.");
    break;
  case Command::Type::SetImageWidthRatio:
    if (cmd.imageWidthRatio <= 0.0)
      return QStringLiteral("Width ratio must be positive.");
    break;
  default:
    break;
  }
  return {};
}

ApplyResult apply(Document &doc, const Command &cmd) {
  ApplyResult result;
  const QString error = validate(doc, cmd);
  if (!error.isEmpty()) {
    result.error = error;
    return result;
  }

  switch (cmd.type) {
  case Command::Type::InsertText: {
    const QString oldText = doc.findBlock(cmd.blockId)->text;
    QString newText = oldText;
    newText.insert(cmd.position, cmd.text);
    doc.updateBlockText(cmd.blockId, newText);
    Op op = makeOp(cmd, Op::Type::InsertText, cmd.blockId);
    op.position = cmd.position;
    op.text = cmd.text;
    op.fullText = newText;
    result.ops.append(op);
    break;
  }
  case Command::Type::DeleteRange: {
    const QString oldText = doc.findBlock(cmd.blockId)->text;
    const int length = cmd.end - cmd.start;
    const QString removed = oldText.mid(cmd.start, length);
    QString newText = oldText;
    newText.remove(cmd.start, length);
    doc.updateBlockText(cmd.blockId, newText);
    Op op = makeOp(cmd, Op::Type::DeleteRange, cmd.blockId);
    op.position = cmd.start;
    op.length = length;
    op.fullText = newText;
    op.payload = removed;
    result.ops.append(op);
    break;
  }
  case Command::Type::ReplaceRange: {
    const QString oldText = doc.findBlock(cmd.blockId)->text;
    const int length = cmd.end - cmd.start;
    const QString removed = oldText.mid(cmd.start, length);
    QString newText = oldText;
    newText.replace(cmd.start, length, cmd.text);
    doc.updateBlockText(cmd.blockId, newText);
    Op op = makeOp(cmd, Op::Type::ReplaceRange, cmd.blockId);
    op.position = cmd.start;
    op.length = length;
    op.text = cmd.text;
    op.fullText = newText;
    op.payload = removed;
    result.ops.append(op);
    break;
  }
  case Command::Type::SetText: {
    const QString oldText = doc.findBlock(cmd.blockId)->text;
    doc.updateBlockText(cmd.blockId, cmd.text);
    Op op = makeOp(cmd, Op::Type::ReplaceRange, cmd.blockId);
    op.position = 0;
    op.length = oldText.size();
    op.text = cmd.text;
    op.fullText = cmd.text;
    op.payload = oldText;
    result.ops.append(op);
    break;
  }
  case Command::Type::ToggleFormat: {
    const QString oldText = doc.findBlock(cmd.blockId)->text;
    const FormatResult formatted =
        toggleFormat(oldText, cmd.start, cmd.end, cmd.format);
    if (!formatted.ok()) {
      result.error = QStringLiteral("Unsupported format: ") + cmd.format;
      return result;
    }
    doc.updateBlockText(cmd.blockId, formatted.text);
    Op op = makeOp(cmd, Op::Type::ReplaceRange, cmd.blockId);
    op.position = 0;
    op.length = oldText.size();
    op.text = formatted.text;
    op.fullText = formatted.text;
    op.payload = oldText;
    op.key = cmd.format;
    result.ops.append(op);
    break;
  }
  case Command::Type::SetBlockType: {
    const Block *block = doc.findBlock(cmd.blockId);
    const int oldType = static_cast<int>(block->type);
    const int oldLevel = block->headingLevel;
    doc.updateBlockType(cmd.blockId, static_cast<BlockType>(cmd.blockType),
                        cmd.headingLevel);
    Op op = makeOp(cmd, Op::Type::SetBlockType, cmd.blockId);
    op.blockType = cmd.blockType;
    op.headingLevel = cmd.headingLevel;
    op.payload = QJsonValue(oldType);
    result.ops.append(op);
    if (oldLevel != cmd.headingLevel) {
      Op levelOp = makeOp(cmd, Op::Type::SetHeadingLevel, cmd.blockId);
      levelOp.headingLevel = cmd.headingLevel;
      levelOp.payload = QJsonValue(oldLevel);
      result.ops.append(levelOp);
    }
    break;
  }
  case Command::Type::SetAlignment: {
    const Block *block = doc.findBlock(cmd.blockId);
    const QString old = block->metadata.value(QStringLiteral("align"),
                                              QStringLiteral("left")).toString();
    QVariantMap metadata = block->metadata;
    metadata[QStringLiteral("align")] = cmd.alignment;
    doc.updateBlockMetadata(cmd.blockId, metadata);
    Op op = makeOp(cmd, Op::Type::SetAlignment, cmd.blockId);
    op.text = cmd.alignment;
    op.payload = old;
    result.ops.append(op);
    break;
  }
  case Command::Type::SetMetadata: {
    const Block *block = doc.findBlock(cmd.blockId);
    doc.updateBlockMetadata(cmd.blockId, cmd.metadata);
    Op op = makeOp(cmd, Op::Type::SetMetadata, cmd.blockId);
    op.payload = QJsonObject::fromVariantMap(block->metadata);
    result.ops.append(op);
    break;
  }
  case Command::Type::SetChecked: {
    const Block *block = doc.findBlock(cmd.blockId);
    QVariantMap metadata = block->metadata;
    const bool old = metadata.value(QStringLiteral("checked"), false).toBool();
    metadata[QStringLiteral("checked")] = cmd.checked;
    doc.updateBlockMetadata(cmd.blockId, metadata);
    Op op = makeOp(cmd, Op::Type::SetMetadata, cmd.blockId);
    op.key = QStringLiteral("checked");
    op.payload = old;
    result.ops.append(op);
    break;
  }
  case Command::Type::SetCalloutType: {
    const Block *block = doc.findBlock(cmd.blockId);
    QVariantMap metadata = block->metadata;
    const QString old = metadata.value(QStringLiteral("calloutType"),
                                       QStringLiteral("note")).toString();
    metadata[QStringLiteral("calloutType")] = cmd.calloutType;
    doc.updateBlockMetadata(cmd.blockId, metadata);
    Op op = makeOp(cmd, Op::Type::SetMetadata, cmd.blockId);
    op.key = QStringLiteral("calloutType");
    op.payload = old;
    result.ops.append(op);
    break;
  }
  case Command::Type::SetCodeLanguage: {
    const Block *block = doc.findBlock(cmd.blockId);
    QVariantMap metadata = block->metadata;
    const QString old = metadata.value(QStringLiteral("codeLanguage")).toString();
    metadata[QStringLiteral("codeLanguage")] = cmd.language;
    doc.updateBlockMetadata(cmd.blockId, metadata);
    Op op = makeOp(cmd, Op::Type::SetMetadata, cmd.blockId);
    op.key = QStringLiteral("codeLanguage");
    op.payload = old;
    result.ops.append(op);
    break;
  }
  case Command::Type::SetImageSource: {
    const Block *block = doc.findBlock(cmd.blockId);
    QVariantMap metadata = block->metadata;
    const QString old = metadata.value(QStringLiteral("imageSource")).toString();
    metadata[QStringLiteral("imageSource")] = cmd.imageSource;
    doc.updateBlockMetadata(cmd.blockId, metadata);
    Op op = makeOp(cmd, Op::Type::SetMetadata, cmd.blockId);
    op.key = QStringLiteral("imageSource");
    op.payload = old;
    result.ops.append(op);
    break;
  }
  case Command::Type::SetImageWidthRatio: {
    const Block *block = doc.findBlock(cmd.blockId);
    QVariantMap metadata = block->metadata;
    const double old = metadata.value(QStringLiteral("imageWidthRatio"), 1.0).toDouble();
    metadata[QStringLiteral("imageWidthRatio")] = cmd.imageWidthRatio;
    doc.updateBlockMetadata(cmd.blockId, metadata);
    Op op = makeOp(cmd, Op::Type::SetMetadata, cmd.blockId);
    op.key = QStringLiteral("imageWidthRatio");
    op.payload = old;
    result.ops.append(op);
    break;
  }
  case Command::Type::InsertBlock: {
    Block block = Block::create(static_cast<BlockType>(cmd.blockType), cmd.text);
    // Callers that already generated an id (e.g. BlockModel) can supply it so
    // delegates and history keep referring to the same block.
    if (!cmd.blockId.isEmpty())
      block.id = cmd.blockId;
    if (cmd.metadata.contains(QStringLiteral("parentId"))) {
      block.parentId = cmd.metadata.value(QStringLiteral("parentId")).toString();
      QVariantMap clean = cmd.metadata;
      clean.remove(QStringLiteral("parentId"));
      if (!clean.isEmpty())
        block.metadata = clean;
    }
    if (cmd.blockType == static_cast<int>(BlockType::Heading))
      block.headingLevel = cmd.headingLevel > 0 ? cmd.headingLevel : 1;
    const QString id = block.id;
    if (cmd.position >= 0 && cmd.position <= doc.count()) {
      doc.insertBlock(cmd.position, block);
    } else if (cmd.afterId.isEmpty()) {
      doc.appendBlock(block);
    } else {
      const int row = doc.indexOf(cmd.afterId);
      doc.insertBlock(row + 1, block);
    }
    Op op = makeOp(cmd, Op::Type::InsertBlock, id);
    op.afterId = cmd.afterId;
    op.blockType = cmd.blockType;
    op.headingLevel = block.headingLevel;
    op.text = cmd.text;
    op.fullText = cmd.text;
    result.ops.append(op);
    // Make the generated id discoverable to callers without mutating cmd.
    // (Stored in a side channel via payload for now; EditorSession copies it.)
    op.payload = id;
    result.ops.last() = op;
    break;
  }
  case Command::Type::RemoveBlock: {
    const Block block = *doc.findBlock(cmd.blockId);
    const int row = doc.indexOf(cmd.blockId);
    doc.removeBlock(cmd.blockId);
    Op op = makeOp(cmd, Op::Type::RemoveBlock, cmd.blockId);
    op.position = row;
    op.payload = block.toJson();
    result.ops.append(op);
    break;
  }
  case Command::Type::SplitBlock: {
    const QString newId = doc.splitBlock(cmd.blockId, cmd.position);
    if (newId.isEmpty()) {
      result.error = QStringLiteral("Split failed.");
      return result;
    }
    Op op = makeOp(cmd, Op::Type::InsertBlock, newId);
    op.afterId = cmd.blockId;
    op.position = cmd.position;
    op.fullText = doc.findBlock(newId)->text;
    op.payload = newId;
    result.ops.append(op);
    break;
  }
  case Command::Type::MergeWithPrevious: {
    const int row = doc.indexOf(cmd.blockId);
    const Block consumed = *doc.findBlock(cmd.blockId);
    if (!doc.mergeWithPrevious(cmd.blockId)) {
      result.error = QStringLiteral("Merge failed.");
      return result;
    }
    Op op = makeOp(cmd, Op::Type::RemoveBlock, cmd.blockId);
    op.position = row;
    op.payload = consumed.toJson();
    result.ops.append(op);
    break;
  }
  case Command::Type::MoveBlock: {
    doc.moveBlock(cmd.blockId, cmd.destination);
    Op op = makeOp(cmd, Op::Type::MoveBlock, cmd.blockId);
    op.destination = cmd.destination;
    result.ops.append(op);
    break;
  }
  case Command::Type::ApplyJson: {
    QJsonParseError parseError;
    const QJsonDocument parsed =
        QJsonDocument::fromJson(cmd.text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
      result.error = parseError.errorString();
      return result;
    }
    const QJsonObject json = parsed.object();
    Document replacement;
    QString docError;
    if (!replacement.fromJson(json, &docError)) {
      result.error = docError;
      return result;
    }
    // Swap contents by replacing every block (the Document has no move
    // assignment from an external doc, so restore() is the safe primitive).
    doc.restore(replacement.snapshot());
    Op op = makeOp(cmd, Op::Type::ApplySnapshot, {});
    op.fullText = cmd.text;
    op.payload = json;
    result.ops.append(op);
    break;
  }
  }

  return result;
}

FormatResult toggleFormat(const QString &raw, int start, int end,
                          const QString &format) {
  const int s = qMin(start, end);
  const int e = qMax(start, end);
  const QString sel = raw.mid(s, e - s);

  QString open, close;
  bool inlineTag = false;
  QRegularExpression spanRegex;
  bool scriptFormat = false;

  if (format == QLatin1String("bold")) {
    open = close = QStringLiteral("**");
    spanRegex = QRegularExpression(QStringLiteral("\\*\\*(.+?)\\*\\*"));
  } else if (format == QLatin1String("italic")) {
    open = close = QStringLiteral("*");
    spanRegex = QRegularExpression(QStringLiteral("\\*([^*\\n]+?)\\*"));
  } else if (format == QLatin1String("underline")) {
    open = QStringLiteral("<u>");
    close = QStringLiteral("</u>");
    inlineTag = true;
  } else if (format == QLatin1String("strike")) {
    open = close = QStringLiteral("~~");
    spanRegex = QRegularExpression(QStringLiteral("~~(.+?)~~"));
  } else if (format == QLatin1String("subscript")) {
    open = QStringLiteral("<sub>");
    close = QStringLiteral("</sub>");
    inlineTag = true;
    scriptFormat = true;
  } else if (format == QLatin1String("superscript")) {
    open = QStringLiteral("<sup>");
    close = QStringLiteral("</sup>");
    inlineTag = true;
    scriptFormat = true;
  } else {
    return {};
  }

  const int otLen = open.size();
  const int ctLen = close.size();

  // Collapsed caret: sub/sup insert an empty pair with the caret inside.
  if (s == e) {
    if (!scriptFormat)
      return FormatResult{raw, s, e};
    if (s >= otLen && s + ctLen <= raw.size() &&
        raw.mid(s - otLen, otLen).compare(open, Qt::CaseInsensitive) == 0 &&
        raw.mid(s, ctLen).compare(close, Qt::CaseInsensitive) == 0) {
      QString next = raw;
      next.remove(s, ctLen);
      next.remove(s - otLen, otLen);
      return FormatResult{next, s - otLen, s - otLen};
    }
    QString next = raw;
    next.insert(s, close);
    next.insert(s, open);
    return FormatResult{next, s + otLen, s + otLen};
  }

  // 1. Selection itself is wrapped.
  if (sel.size() >= otLen + ctLen &&
      sel.startsWith(open, Qt::CaseInsensitive) &&
      sel.endsWith(close, Qt::CaseInsensitive)) {
    const QString inner = sel.mid(otLen, sel.size() - otLen - ctLen);
    QString next = raw;
    next.replace(s, e - s, inner);
    return FormatResult{next, s, s + static_cast<int>(inner.size())};
  }

  // 2. Surrounding characters are the tags.
  if (s >= otLen && e + ctLen <= raw.size() &&
      raw.mid(s - otLen, otLen).compare(open, Qt::CaseInsensitive) == 0 &&
      raw.mid(e, ctLen).compare(close, Qt::CaseInsensitive) == 0) {
    QString next = raw;
    next.remove(e, ctLen);
    next.remove(s - otLen, otLen);
    return FormatResult{next, s - otLen, e - otLen};
  }

  // 3. Selection is inside an existing inline tag span.
  if (inlineTag) {
    const QString tagName = open.mid(1, open.size() - 2);
    QRegularExpression tagRe(QStringLiteral("<%1>(.*?)</%1>")
                                 .arg(tagName));
    tagRe.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
    auto it = tagRe.globalMatch(raw);
    while (it.hasNext()) {
      const auto match = it.next();
      const int contentStart = match.capturedStart(1);
      const int contentEnd = match.capturedEnd(1);
      if (s < contentStart || e > contentEnd)
        continue;
      if (s == contentStart && e == contentEnd) {
        QString next = raw;
        next.remove(match.capturedEnd(0), close.size());
        next.remove(match.capturedStart(0), open.size());
        return FormatResult{next, static_cast<int>(match.capturedStart(0)),
                            static_cast<int>(match.capturedStart(0)) + (e - s)};
      }
      const QString before = raw.mid(contentStart, s - contentStart);
      const QString after = raw.mid(e, contentEnd - e);
      QString rebuilt;
      int newStart = match.capturedStart(0);
      if (!before.isEmpty()) {
        rebuilt += open + before + close;
        newStart += otLen + before.size() + ctLen;
      }
      rebuilt += sel;
      const int newEnd = newStart + sel.size();
      if (!after.isEmpty())
        rebuilt += open + after + close;
      QString next = raw;
      next.replace(match.capturedStart(0), match.capturedLength(0), rebuilt);
      return FormatResult{next, newStart, newEnd};
    }
  } else {
    // 4. Selection is inside an existing markdown span.
    auto it = spanRegex.globalMatch(raw);
    while (it.hasNext()) {
      const auto match = it.next();
      const int mStart = match.capturedStart(0);
      const int mEnd = match.capturedEnd(0);
      int contentStart = match.capturedStart(1);
      int contentEnd = match.capturedEnd(1);
      if (format == QLatin1String("italic")) {
        contentStart = mStart + 1;
        contentEnd = mEnd - 1;
      }
      if (s < contentStart || e > contentEnd)
        continue;
      if (s == contentStart && e == contentEnd) {
        QString next = raw;
        next.remove(mEnd - ctLen, ctLen);
        next.remove(mStart, otLen);
        return FormatResult{next, mStart, mStart + (e - s)};
      }
      const QString before = raw.mid(contentStart, s - contentStart);
      const QString after = raw.mid(e, contentEnd - e);
      QString rebuilt;
      int newStart = mStart;
      if (!before.isEmpty()) {
        rebuilt += open + before + close;
        newStart += otLen + before.size() + ctLen;
      }
      rebuilt += sel;
      const int newEnd = newStart + sel.size();
      if (!after.isEmpty())
        rebuilt += open + after + close;
      QString next = raw;
      next.replace(mStart, mEnd - mStart, rebuilt);
      return FormatResult{next, newStart, newEnd};
    }
  }

  // 5. Wrap the selection.
  QString next = raw;
  next.replace(s, e - s, open + sel + close);
  return FormatResult{next, s, e + otLen + ctLen};
}

} // namespace Templnote::Engine::Commands
