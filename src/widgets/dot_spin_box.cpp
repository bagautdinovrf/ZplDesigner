#include "dot_spin_box.hpp"

#include <QDoubleValidator>
#include <QLineEdit>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

namespace Designer {
DotSpinBox::DotSpinBox(QWidget* parent) : QSpinBox(parent) {
    setKeyboardTracking(false);
    setMinimumHeight(30);
    setDisplayUnits(CanvasUnit::Dots, 203);
}

void DotSpinBox::setDisplayUnits(CanvasUnit units, int dpi) {
    const auto targetSuffix = property("hideUnitSuffix").toBool() && units == CanvasUnit::Dots ? QString{}
        : units == CanvasUnit::Millimeters ? tr(" мм") : QStringLiteral(" dots");
    if (units_ == units && dpi_ == std::max(1, dpi) && suffix() == targetSuffix) return;
    const QSignalBlocker blocker(this);
    units_ = units;
    dpi_ = std::max(1, dpi);
    setSuffix(targetSuffix);
    lineEdit()->setText(value() == minimum() && !specialValueText().isEmpty()
        ? specialValueText() : prefix() + textFromValue(value()) + suffix());
    setToolTip(tr("Хранится целое число точек принтера. Ввод в мм округляется до ближайшей точки; стрелки меняют значение на 1 dot."));
}

void DotSpinBox::setMillimeterDecimals(int decimals) {
    millimeterDecimals_ = std::clamp(decimals, 0, 3);
    const QSignalBlocker blocker(this);
    lineEdit()->setText(prefix() + textFromValue(value()) + suffix());
}

QString DotSpinBox::textFromValue(int value) const {
    return units_ == CanvasUnit::Millimeters
        ? locale().toString(value * 25.4 / dpi_, 'f', millimeterDecimals_) : QSpinBox::textFromValue(value);
}

QString DotSpinBox::numberText(QString text) const {
    if (text.startsWith(prefix())) text.remove(0, prefix().size());
    if (text.endsWith(suffix())) text.chop(suffix().size());
    return text.trimmed();
}

int DotSpinBox::valueFromText(const QString& text) const {
    if (units_ == CanvasUnit::Dots) return QSpinBox::valueFromText(text);
    if (!specialValueText().isEmpty() && text.trimmed() == specialValueText()) return minimum();
    bool ok = false;
    const double mm = locale().toDouble(numberText(text), &ok);
    if (!ok || !std::isfinite(mm)) return value();
    return qRound(std::clamp(mm * dpi_ / 25.4, double(minimum()), double(maximum())));
}

QValidator::State DotSpinBox::validate(QString& text, int& position) const {
    if (units_ == CanvasUnit::Dots) return QSpinBox::validate(text, position);
    if (!specialValueText().isEmpty() && text.trimmed() == specialValueText()) return QValidator::Acceptable;
    QString number = numberText(text);
    int cursor = std::min(position, int(number.size()));
    // Permit the displayed rounded boundary as well as its exact dot value.
    QDoubleValidator validator(minimum() * 25.4 / dpi_ - 0.0005,
                               maximum() * 25.4 / dpi_ + 0.0005, 3);
    validator.setLocale(locale());
    validator.setNotation(QDoubleValidator::StandardNotation);
    return validator.validate(number, cursor);
}
}
