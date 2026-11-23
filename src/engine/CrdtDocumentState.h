#pragma once

// CRDT-backed state for multi-user collaboration.
//
// Convergence model: every replica keeps the full set of causally-tagged ops
// it has seen. The document is rebuilt deterministically from that set, so two
// replicas that have seen the same ops always produce identical documents.
//
//  - Block ordering uses an RGA-style rule: insertion anchors to a stable
//    predecessor id; concurrent insertions at the same anchor are ordered by
//    (lamport, actor id).
//  - Block text and properties are LWW registers tagged with (lamport, actor).
//    Concurrent edits to the *same* block resolve deterministically (last
//    writer by logical clock wins, actor id breaks ties).
//
// This is deliberately simpler than a character-level CRDT: it converges, is
// easy to reason about, and preserves every existing block feature. Fine-grained
// character interleaving inside one block can be added later behind the same
// DocumentState interface.

#include "DocumentState.h"
#include "Types.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace Templnote::Engine {

class CrdtDocumentState : public DocumentState {
public:
  const Document &document() const override { return m_document; }
  Document &document() override { return m_document; }

  QString apply(const Command &cmd, QVector<Op> *ops) override;
  QString observe(const Op &op) override;

  // Merge a remote replica's op set. Returns the number of newly learned ops.
  int merge(const QVector<Op> &remoteOps);

  QJsonObject serialize() const override;
  bool load(const QJsonObject &json, QString *error = nullptr) override;
  quint64 lamport() const override { return m_lamport; }
  void setLamport(quint64 value) override { m_lamport = value; }

  // All ops known to this replica, for sync.
  QVector<Op> opSet() const { return m_opSet; }
  QString replicaId() const { return m_replicaId; }
  void setReplicaId(const QString &id) { m_replicaId = id; }

  void clear();

private:
  struct Field {
    QVariant value;
    quint64 clock = 0;
    QString actor;
  };
  struct Versioned {
    Field text;
    Field type;
    Field headingLevel;
    Field parentId;
    QHash<QString, Field> fields;
  };

  static bool wins(quint64 clock, const QString &actor, quint64 currentClock,
                   const QString &currentActor);
  void applyOpIncrementally(const Op &op);
  void rebuild();

  Document m_document;
  QVector<Op> m_opSet;
  QVector<QString> m_opKeys; // cache: opKey(op) for each entry in m_opSet
  QHash<QString, Versioned> m_versions;
  quint64 m_lamport = 0;
  QString m_replicaId;
};

} // namespace Templnote::Engine
