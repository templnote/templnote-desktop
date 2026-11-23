#pragma once

// Pluggable state backend. JsonDocumentState is the P0 implementation backed
// by the existing Document. A CRDT implementation can be dropped in later
// without changing EditorSession or the command/patch API.

#include "Types.h"
#include "core/Document.h"

#include <QJsonObject>
#include <QVector>

namespace Templnote::Engine {

class DocumentState {
public:
  virtual ~DocumentState() = default;

  virtual const Document &document() const = 0;
  virtual Document &document() = 0;

  // Apply a command. Returns an empty string on success; otherwise an error.
  // On success, `*ops` receives the atomic changes that were performed.
  virtual QString apply(const Command &cmd, QVector<Op> *ops) = 0;

  // Apply a single remote op (used by CRDT sync and patch replay).
  virtual QString observe(const Op &op) = 0;

  virtual QJsonObject serialize() const = 0;
  virtual bool load(const QJsonObject &json, QString *error = nullptr) = 0;

  virtual quint64 lamport() const = 0;
  virtual void setLamport(quint64 value) = 0;
};

class JsonDocumentState : public DocumentState {
public:
  const Document &document() const override { return m_document; }
  Document &document() override { return m_document; }

  QString apply(const Command &cmd, QVector<Op> *ops) override;
  QString observe(const Op &op) override;
  QJsonObject serialize() const override;
  bool load(const QJsonObject &json, QString *error = nullptr) override;
  quint64 lamport() const override { return m_lamport; }
  void setLamport(quint64 value) override { m_lamport = value; }

private:
  Document m_document;
  quint64 m_lamport = 0;
};

} // namespace Templnote::Engine
