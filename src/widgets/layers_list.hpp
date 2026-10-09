#pragma once

#include "design_document.hpp"

#include <QListWidget>

namespace Designer {

// Positional rows are private to this view; cross-panel selection uses UUIDs.
class LayersList final : public QListWidget {
    Q_OBJECT
public:
    explicit LayersList(QWidget* parent = nullptr);

    void refresh(const DesignDocument& document, const QList<QUuid>& selectedIds);
    void setSelectedIds(const QList<QUuid>& ids);
    [[nodiscard]] QList<QUuid> selectedIds() const;
    [[nodiscard]] QUuid primaryId() const;
};

} // namespace Designer
