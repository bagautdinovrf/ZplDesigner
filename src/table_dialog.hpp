#pragma once
#include "design_document.hpp"
#include "canvas_layout.hpp"
#include <QDialog>

class QTableWidget;
class QSpinBox;
class QLabel;
class QDialogButtonBox;

namespace Designer {
class TableDialog final : public QDialog {
    Q_OBJECT
public:
    TableDialog(const Element& element, CanvasUnit unit, int dpi, QWidget* parent = nullptr);
    Element editedElement() const;
private:
    void refreshGrid();
    void refreshSelection();
    void validateSize();
    Element element_;
    QTableWidget* cells_ = nullptr;
    QSpinBox* rows_ = nullptr;
    QSpinBox* columns_ = nullptr;
    QSpinBox* rowHeight_ = nullptr;
    QSpinBox* columnWidth_ = nullptr;
    QLabel* result_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    bool updating_ = false;
    int dpi_ = 203;
};
}
