#include "CapabilityGate.h"

namespace Templnote::Engine {

CapabilityGate::CapabilityGate(QObject *parent) : QObject(parent) {}

void CapabilityGate::setRole(const QString &actorId, Role role) {
  if (m_roles.value(actorId, Role::Viewer) == role)
    return;
  m_roles.insert(actorId, role);
  emit rolesChanged();
}

CapabilityGate::Role CapabilityGate::roleOf(const QString &actorId) const {
  return m_roles.value(actorId, Role::Viewer);
}

bool CapabilityGate::hasCapability(const QString &actorId,
                                   Capability capability) const {
  const Role role = roleOf(actorId);
  switch (capability) {
  case Capability::EditDocument:
    return role == Role::Owner || role == Role::Editor;
  case Capability::ControlAi:
    return role == Role::Owner;
  case Capability::ManageVersions:
    return role == Role::Owner;
  case Capability::InvitePeers:
    return role == Role::Owner;
  }
  return false;
}

bool CapabilityGate::allows(const Command &cmd, QString *reason) const {
  const QString actorId = cmd.actor.id.isEmpty()
                              ? QStringLiteral("local-human")
                              : cmd.actor.id;
  if (cmd.actor.isAgent() && !hasCapability(actorId, Capability::ControlAi)) {
    if (reason)
      *reason = QStringLiteral("This user cannot control AI agents.");
    return false;
  }
  if (!hasCapability(actorId, Capability::EditDocument)) {
    if (reason)
      *reason = QStringLiteral("This user has read-only access.");
    return false;
  }
  if (reason)
    reason->clear();
  return true;
}

QString CapabilityGate::roleName(Role role) {
  switch (role) {
  case Role::Owner:
    return QStringLiteral("owner");
  case Role::Editor:
    return QStringLiteral("editor");
  case Role::Viewer:
    return QStringLiteral("viewer");
  }
  return QStringLiteral("viewer");
}

} // namespace Templnote::Engine
