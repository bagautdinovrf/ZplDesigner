#pragma once
#include "design_document.hpp"
#include "document_operations.hpp"
#include "canvas_layout.hpp"
#include <QWidget>
#include <QSet>
class QFormLayout;
class QSpinBox;
class QPushButton;
namespace Designer {
class SelectionProperties final : public QWidget {
    Q_OBJECT
public:
    explicit SelectionProperties(QWidget* parent = nullptr);
    void setSelection(const DesignDocument& document, const QList<int>& indices);
    void setDisplayUnits(CanvasUnit unit, int dpi);
signals:
    void changesRequested(Designer::SelectionPropertyValues values);
private:
    QFormLayout* form_;
    QMap<QString, QSpinBox*> fields_;
    QPushButton* apply_;
    QString selectionState_;
    QSet<QString> changedFields_;
};
}
