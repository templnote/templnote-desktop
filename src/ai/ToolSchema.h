#pragma once

// Converts engine commands into JSON tool schemas that LLMs can call, and
// parses model tool calls back into validated Commands. This is the single
// bridge between "AI speaks tools" and "the engine applies operations".

#include "engine/Types.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace Templnote::AI {

class ToolSchema {
public:
  // OpenAI-compatible tool definitions for every edit command.
  static QJsonArray definitions();

  // Parse a model tool call ({name, arguments}) into a Command.
  static Engine::Result<Engine::Command> fromToolCall(
      const QJsonObject &call, QString *error = nullptr);

  // Serialize a command as a tool call (useful for tests / debugging).
  static QJsonObject toToolCall(const Engine::Command &cmd);

  // Tool definitions for authoring plugins (handled by PluginManager, not the
  // document engine). Returned separately so clients can opt in.
  static QJsonArray pluginDefinitions();
};

} // namespace Templnote::AI
