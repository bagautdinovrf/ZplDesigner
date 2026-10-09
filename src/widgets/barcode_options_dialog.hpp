#pragma once
#include "design_document.hpp"
#include "canvas_layout.hpp"
#include <QDialog>

class QDialogButtonBox;
class QLabel;

namespace Designer {
class BarcodeOptionsDialog final : public QDialog {
    Q_OBJECT
public:
    BarcodeOptionsDialog(const Element& element, CanvasUnit unit, int dpi, QWidget* parent = nullptr);
    Element editedElement() const;
private:
    void validateOptions();
    Element element_;
    QLabel* status_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
}
