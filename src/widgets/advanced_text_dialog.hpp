#pragma once
#include "design_document.hpp"
#include "canvas_layout.hpp"
#include <QDialog>

namespace Designer {
class AdvancedTextDialog final : public QDialog {
    Q_OBJECT
public:
    AdvancedTextDialog(const Element& element, CanvasUnit unit, int dpi, QWidget* parent = nullptr);
    Element editedElement() const;
private:
    Element element_;
};
}
