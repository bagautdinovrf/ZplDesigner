#pragma once

#include "canvas_layout.hpp"
#include <QSpinBox>

namespace Designer {
// The stored value and valueChanged signal always use integral printer dots.
class DotSpinBox final : public QSpinBox {
    Q_OBJECT
public:
    explicit DotSpinBox(QWidget* parent = nullptr);
    void setDisplayUnits(CanvasUnit units, int dpi);
    // Millimetre text precision; input still accepts three decimals.
    void setMillimeterDecimals(int decimals);
protected:
    QString textFromValue(int value) const override;
    int valueFromText(const QString& text) const override;
    QValidator::State validate(QString& text, int& position) const override;
private:
    QString numberText(QString text) const;
    CanvasUnit units_ = CanvasUnit::Dots;
    int dpi_ = 203;
    int millimeterDecimals_ = 3;
};
}
