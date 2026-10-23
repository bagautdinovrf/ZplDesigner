#include "canvas_layout.hpp"

#include <algorithm>

namespace Designer {

CanvasLayoutSettings scaledLayoutSettings(CanvasLayoutSettings settings, double scaleX, double scaleY)
{
    const auto scale = [](QList<int>& guides, double factor) {
        for (int& guide : guides) guide = qRound(std::clamp(guide * factor, 0.0, 4096.0));
    };
    scale(settings.horizontalGuides, scaleY);
    scale(settings.verticalGuides, scaleX);
    settings.margin = qRound(std::clamp(settings.margin * std::min(scaleX, scaleY), 0.0, 2048.0));
    return settings;
}

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
