#pragma once

// A DocumentState backend that operates on a Document owned by someone else.
// Used by BlockModel so the engine and the view share one Document instance —
// there is no second copy to drift out of sync.

#include "DocumentState.h"

namespace Templnote::Engine {

class ExternalDocumentState : public DocumentState {
public:
  explicit ExternalDocumentState(Document *document)
      : m_document(document) {}

  const Document &document() const override { return *m_document; }
  Document &document() override { return *m_document; }

  QString apply(const Command &cmd, QVector<Op> *ops) override;
  QString observe(const Op &op) override;
  QJsonObject serialize() const override;
  bool load(const QJsonObject &json, QString *error = nullptr) override;
  quint64 lamport() const override { return m_lamport; }
  void setLamport(quint64 value) override { m_lamport = value; }

  void setDocument(Document *document) { m_document = document; }

private:
  Document *m_document = nullptr;
  quint64 m_lamport = 0;
};

} // namespace Templnote::Engine
