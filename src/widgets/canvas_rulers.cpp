#include "canvas_rulers.hpp"

#include <algorithm>
#include <cmath>

namespace Designer {

QString rulerEndLabel(int dots, int dpi, CanvasUnit unit, const QLocale& locale)
{
    if (unit == CanvasUnit::Dots) return locale.toString(dots);
    return locale.toString(canvasUnitValue(dots, unit, dpi), 'f', 2);
}

QList<RulerTick> rulerTicks(int extentDots, int dpi, CanvasUnit unit, qreal pixelsPerDot,
                            qreal labelSpacing, qreal endClearance, const QLocale& locale)
{
    QList<RulerTick> ticks;
    if (extentDots <= 0 || pixelsPerDot <= 0) return ticks;
    const qreal dotsPerUnit = unit == CanvasUnit::Millimeters ? std::max(1, dpi) / 25.4 : 1.0;
    // One minor division in display units: 1 mm or 10 dots, then tenfold.
    qint64 base = unit == CanvasUnit::Millimeters ? 1 : 10;
    while (10 * base * dotsPerUnit * pixelsPerDot < labelSpacing && base < 100'000'000) base *= 10;
    const qreal step = base * dotsPerUnit * pixelsPerDot;
    const bool minor = step >= rulerMinimumTickSpacing;
    const bool middle = 5 * step >= rulerMinimumTickSpacing;
    const qreal end = extentDots * pixelsPerDot;
    const auto last = static_cast<qint64>(std::floor(extentDots / (base * dotsPerUnit) + 1e-9));
    for (qint64 index = 0; index <= last; ++index) {
        const int length = index % 10 == 0 ? rulerMajorTick : index % 5 == 0 ? rulerMiddleTick : rulerMinorTick;
        if ((length == rulerMinorTick && !minor) || (length == rulerMiddleTick && !middle)) continue;
        const qreal pixel = index * step;
        RulerTick tick{pixel, length};
        if (length == rulerMajorTick && end - pixel > endClearance)
            tick.label = locale.toString(index * base);
        ticks.append(tick);
    }
    ticks.append({end, 0, rulerEndLabel(extentDots, dpi, unit, locale), true});
    return ticks;
}

} // namespace Designer
