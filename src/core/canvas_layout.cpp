#include "canvas_layout.hpp"

#include <algorithm>

namespace Designer {

double canvasUnitValue(double dots, CanvasUnit unit, int dpi)
{
    return unit == CanvasUnit::Millimeters ? dots * 25.4 / std::max(1, dpi) : dots;
}

QString formatCanvasLength(double dots, CanvasUnit unit, int dpi, const QLocale& locale)
{
    if (unit == CanvasUnit::Dots) return QStringLiteral("%1 dots").arg(locale.toString(qRound(dots)));
    return QStringLiteral("%1 мм").arg(locale.toString(canvasUnitValue(dots, unit, dpi), 'f', 2));
}

} // namespace Designer
