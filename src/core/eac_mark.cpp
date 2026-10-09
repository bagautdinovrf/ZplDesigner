#include "eac_mark.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Designer {
namespace {
// EEC Decision 711, figures 3 and 4:
// https://eec.eaeunion.org/upload/medialibrary/986/PologenieEAC.pdf
// Disjoint rectangles preserve the exact 9 x 9 construction without relying
// on a font, antialiasing, or an additional preview renderer.
const std::array<QRect, 11> bars{{
    {0, 0, 1, 9}, {1, 0, 1, 1}, {1, 4, 1, 1}, {1, 8, 1, 1},
    {3, 0, 1, 9}, {5, 0, 1, 9}, {4, 0, 1, 1}, {4, 4, 1, 1},
    {7, 0, 1, 9}, {8, 0, 1, 1}, {8, 8, 1, 1}
}};

int boundary(int unit, int size, int units)
{
    return qRound(double(unit) * size / units);
}
} // namespace

QPainterPath eacMarkPath()
{
    QPainterPath result;
    result.setFillRule(Qt::WindingFill);
    for (const auto& bar : bars) result.addRect(QRectF(bar));
    return result;
}

int eacMinimumSize(int dpi, EacStyle style)
{
    const int module = static_cast<int>(std::ceil(5.0 * std::max(1, dpi) / (25.4 * 9)));
    return module * eacGridUnits(style);
}

int eacGridUnits(EacStyle style)
{
    return style == EacStyle::WhiteOnBlack ? 11 : 9;
}

int eacSizeForRequest(int size, int dpi, EacStyle style)
{
    const int units = eacGridUnits(style);
    const int minimum = eacMinimumSize(dpi, style);
    size = std::clamp(size, minimum, 4096);
    return std::min((size + units - 1) / units, 4096 / units) * units;
}

QList<Element> eacElements(const Element& element)
{
    QList<Element> result;
    const bool inverse = element.eacStyle == EacStyle::WhiteOnBlack;
    const bool white = inverse || element.eacStyle == EacStyle::WhiteTransparent;
    const int units = inverse ? 11 : 9;
    const int inset = inverse ? 1 : 0;
    const auto rectangle = [&](int x, int y, int width, int height, bool graphicWhite) {
        auto child = makeElement(ElementType::Box, {element.x + x, element.y + y});
        child.width = width;
        child.height = height;
        child.thickness = std::min(width, height);
        child.graphicWhite = graphicWhite;
        child.originJustification = 0;
        return child;
    };
    if (element.eacStyle == EacStyle::BlackOnWhite || inverse)
        result.append(rectangle(0, 0, element.width, element.width, !inverse));
    for (const auto& bar : bars) {
        const int left = boundary(inset + bar.x(), element.width, units);
        const int top = boundary(inset + bar.y(), element.width, units);
        const int right = boundary(inset + bar.x() + bar.width(), element.width, units);
        const int bottom = boundary(inset + bar.y() + bar.height(), element.width, units);
        result.append(rectangle(left, top, right - left, bottom - top, white));
    }
    return result;
}
} // namespace Designer
