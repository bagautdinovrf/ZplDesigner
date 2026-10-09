#pragma once

#include "design_document.hpp"

#include <QObject>

namespace Designer {

// Selection owns stable element identities, never a document or its history.
// The last requested ID is primary, matching the canvas selection convention.
class ElementSelection final : public QObject {
    Q_OBJECT
public:
    explicit ElementSelection(QObject* parent = nullptr);

    [[nodiscard]] const QList<QUuid>& ids() const;
    [[nodiscard]] QUuid primaryId() const;
    [[nodiscard]] QList<int> indices(const DesignDocument& document) const;
    void setIds(const QList<QUuid>& ids, const DesignDocument& document);
    void setIndices(const QList<int>& indices, const DesignDocument& document);
    // Call after replacing a document snapshot. Reordering keeps selection;
    // deleted objects disappear, while surviving groups remain selected together.
    void reconcile(const DesignDocument& document);

signals:
    void selectionChanged();

private:
    QList<QUuid> ids_;
};

} // namespace Designer
