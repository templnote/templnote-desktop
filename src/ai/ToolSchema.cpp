#include "ToolSchema.h"

#include <QJsonDocument>

namespace Templnote::AI {
namespace {

QJsonObject tool(const QString &name, const QString &description,
                 const QJsonObject &properties,
                 const QJsonArray &required = {}) {
  QJsonObject schema;
  schema.insert(QStringLiteral("type"), QStringLiteral("object"));
  schema.insert(QStringLiteral("properties"), properties);
  if (!required.isEmpty())
    schema.insert(QStringLiteral("required"), required);
  QJsonObject function;
  function.insert(QStringLiteral("name"), name);
  function.insert(QStringLiteral("description"), description);
  function.insert(QStringLiteral("parameters"), schema);
  QJsonObject wrapper;
  wrapper.insert(QStringLiteral("type"), QStringLiteral("function"));
  wrapper.insert(QStringLiteral("function"), function);
  return wrapper;
}

QJsonObject stringProp(const QString &description) {
  QJsonObject p;
  p.insert(QStringLiteral("type"), QStringLiteral("string"));
  p.insert(QStringLiteral("description"), description);
  return p;
}
QJsonObject intProp(const QString &description) {
  QJsonObject p;
  p.insert(QStringLiteral("type"), QStringLiteral("integer"));
  p.insert(QStringLiteral("description"), description);
  return p;
}

} // namespace

QJsonArray ToolSchema::definitions() {
  QJsonArray tools;

  QJsonObject props;
  props.insert(QStringLiteral("blockId"), stringProp("Target block id"));
  props.insert(QStringLiteral("start"), intProp("Start offset"));
  props.insert(QStringLiteral("end"), intProp("End offset"));
  props.insert(QStringLiteral("text"), stringProp("Replacement text"));
  tools.append(tool("replaceRange",
                    "Replace a range of text in a block with new text.", props,
                    QJsonArray{QStringLiteral("blockId"),
                               QStringLiteral("start"), QStringLiteral("end"),
                               QStringLiteral("text")}));

  QJsonObject ins;
  ins.insert(QStringLiteral("blockId"), stringProp("Target block id"));
  ins.insert(QStringLiteral("position"), intProp("Insert offset"));
  ins.insert(QStringLiteral("text"), stringProp("Text to insert"));
  tools.append(tool("insertText", "Insert text at a position in a block.", ins,
                    QJsonArray{QStringLiteral("blockId"),
                               QStringLiteral("position"),
                               QStringLiteral("text")}));

  QJsonObject setText;
  setText.insert(QStringLiteral("blockId"), stringProp("Target block id"));
  setText.insert(QStringLiteral("text"), stringProp("New complete text"));
  tools.append(tool("setText", "Replace the entire text of a block.", setText,
                    QJsonArray{QStringLiteral("blockId"),
                               QStringLiteral("text")}));

  QJsonObject fmt;
  fmt.insert(QStringLiteral("blockId"), stringProp("Target block id"));
  fmt.insert(QStringLiteral("start"), intProp("Start offset"));
  fmt.insert(QStringLiteral("end"), intProp("End offset"));
  fmt.insert(QStringLiteral("format"),
             stringProp("One of: bold, italic, underline, strike, code, link, "
                        "subscript, superscript"));
  tools.append(tool("toggleFormat",
                    "Toggle inline markdown formatting over a range.", fmt,
                    QJsonArray{QStringLiteral("blockId"),
                               QStringLiteral("start"), QStringLiteral("end"),
                               QStringLiteral("format")}));

  QJsonObject type;
  type.insert(QStringLiteral("blockId"), stringProp("Target block id"));
  type.insert(QStringLiteral("blockType"),
              intProp("0=paragraph 1=heading 2=bullet 3=numbered 4=todo "
                      "5=quote 6=code 10=callout"));
  type.insert(QStringLiteral("headingLevel"), intProp("1-6 for headings"));
  tools.append(tool("setBlockType", "Change a block's type.", type,
                    QJsonArray{QStringLiteral("blockId"),
                               QStringLiteral("blockType")}));

  QJsonObject insBlock;
  insBlock.insert(QStringLiteral("afterId"),
                  stringProp("Insert after this block id (empty = append)"));
  insBlock.insert(QStringLiteral("blockType"), intProp("Block type"));
  insBlock.insert(QStringLiteral("text"), stringProp("Initial text"));
  tools.append(tool("insertBlock", "Insert a new block.", insBlock,
                    QJsonArray{QStringLiteral("blockType")}));

  QJsonObject del;
  del.insert(QStringLiteral("blockId"), stringProp("Block to remove"));
  tools.append(tool("removeBlock", "Remove a block.", del,
                    QJsonArray{QStringLiteral("blockId")}));

  return tools;
}

Engine::Result<Engine::Command> ToolSchema::fromToolCall(
    const QJsonObject &call, QString *error) {
  const QJsonObject function =
      call.value(QStringLiteral("function")).toObject();
  const QString name = call.value(QStringLiteral("name")).toString(
      function.value(QStringLiteral("name")).toString());
  // OpenAI/Anthropic wrap arguments inside "function"; Ollama and our own
  // toToolCall() emit them at the top level. Accept either shape, object or
  // JSON string.
  QJsonObject args = call.value(QStringLiteral("arguments")).toObject();
  if (args.isEmpty())
    args = function.value(QStringLiteral("arguments")).toObject();
  if (args.isEmpty()) {
    const QJsonValue raw = call.value(QStringLiteral("arguments"));
    const QJsonValue functionRaw = function.value(QStringLiteral("arguments"));
    const QJsonValue source = raw.isString() ? raw : functionRaw;
    if (source.isString()) {
      QJsonParseError parseError;
      const QJsonDocument doc =
          QJsonDocument::fromJson(source.toString().toUtf8(), &parseError);
      if (parseError.error == QJsonParseError::NoError && doc.isObject())
        args = doc.object();
    }
  }

  bool typeOk = false;
  const Engine::Command::Type type =
      Engine::Command::typeFromName(name, &typeOk);
  if (!typeOk) {
    if (error)
      *error = QStringLiteral("Unknown tool: ") + name;
    return Engine::Result<Engine::Command>(error ? *error : QStringLiteral("Unknown tool"));
  }

  Engine::Command cmd;
  cmd.type = type;
  cmd.blockId = args.value(QStringLiteral("blockId")).toString();
  cmd.afterId = args.value(QStringLiteral("afterId")).toString();
  cmd.position = args.value(QStringLiteral("position")).toInt(-1);
  cmd.start = args.value(QStringLiteral("start")).toInt(-1);
  cmd.end = args.value(QStringLiteral("end")).toInt(-1);
  cmd.text = args.value(QStringLiteral("text")).toString();
  cmd.format = args.value(QStringLiteral("format")).toString();
  cmd.blockType = args.value(QStringLiteral("blockType")).toInt(0);
  cmd.headingLevel = args.value(QStringLiteral("headingLevel")).toInt(0);
  cmd.actor = Engine::Actor::agent(QStringLiteral("ai-agent"));
  return Engine::Result<Engine::Command>(cmd);
}

QJsonArray ToolSchema::pluginDefinitions() {
  QJsonArray tools;

  QJsonObject spec;
  spec.insert(QStringLiteral("type"), QStringLiteral("object"));
  spec.insert(QStringLiteral("description"),
              QStringLiteral("Plugin specification: id, name, description, "
                             "category, decorations[{slot,component,zOrder}], "
                             "settingsFields[{key,type,label,description,default}], "
                             "permissions[]"));
  spec.insert(QStringLiteral("additionalProperties"), true);
  QJsonObject createFn;
  createFn.insert(QStringLiteral("name"), QStringLiteral("createPlugin"));
  createFn.insert(QStringLiteral("description"),
                  QStringLiteral("Create a new plugin with a generated manifest. "
                                 "Requires user approval before it can be enabled."));
  createFn.insert(QStringLiteral("parameters"), spec);
  QJsonObject createTool;
  createTool.insert(QStringLiteral("type"), QStringLiteral("function"));
  createTool.insert(QStringLiteral("function"), createFn);
  tools.append(createTool);

  QJsonObject writeProps;
  writeProps.insert(QStringLiteral("pluginId"), stringProp("Plugin id"));
  writeProps.insert(QStringLiteral("path"), stringProp("Relative file path (.qml/.json/.js)"));
  writeProps.insert(QStringLiteral("contents"), stringProp("File contents"));
  QJsonObject writeFn;
  writeFn.insert(QStringLiteral("name"), QStringLiteral("writePluginFile"));
  writeFn.insert(QStringLiteral("description"),
                 QStringLiteral("Write a QML/JSON file inside an existing plugin."));
  QJsonObject writeSchema;
  writeSchema.insert(QStringLiteral("type"), QStringLiteral("object"));
  writeSchema.insert(QStringLiteral("properties"), writeProps);
  writeSchema.insert(QStringLiteral("required"),
                     QJsonArray{QStringLiteral("pluginId"),
                                QStringLiteral("path"), QStringLiteral("contents")});
  writeFn.insert(QStringLiteral("parameters"), writeSchema);
  QJsonObject writeTool;
  writeTool.insert(QStringLiteral("type"), QStringLiteral("function"));
  writeTool.insert(QStringLiteral("function"), writeFn);
  tools.append(writeTool);

  // approvePlugin is deliberately NOT advertised. Enabling agent-authored QML
  // is a user consent decision made in Settings; no model or remote caller can
  // grant it to itself.

  return tools;
}

QJsonObject ToolSchema::toToolCall(const Engine::Command &cmd) {
  QJsonObject call;
  call.insert(QStringLiteral("name"), cmd.typeName());
  call.insert(QStringLiteral("arguments"), cmd.toJson());
  return call;
}

} // namespace Templnote::AI
