#pragma once

// Role-based capability gating. "Other users can see changes but not control
// the AI" is enforced here: AI-originated commands are rejected unless the
// actor has the AgentControl capability. Human edits are gated separately.

#include "Types.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace Templnote::Engine {

class CapabilityGate : public QObject {
  Q_OBJECT
public:
  enum class Role : uint8_t { Owner, Editor, Viewer };
  Q_ENUM(Role)

  enum class Capability : uint8_t {
    EditDocument,
    ControlAi,
    ManageVersions,
    InvitePeers
  };
  Q_ENUM(Capability)

  explicit CapabilityGate(QObject *parent = nullptr);

  void setRole(const QString &actorId, Role role);
  Role roleOf(const QString &actorId) const;

  bool hasCapability(const QString &actorId, Capability capability) const;
  // Full check used by EditorSession/agents before applying a command.
  bool allows(const Command &cmd, QString *reason = nullptr) const;

  static QString roleName(Role role);

signals:
  void rolesChanged();

private:
  QHash<QString, Role> m_roles;
};

} // namespace Templnote::Engine
