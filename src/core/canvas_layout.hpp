#pragma once

#include <QList>
#include <QLocale>
#include <QString>

namespace Designer {

enum class CanvasUnit { Millimeters, Dots };

// Editor-only layout aids. Never consumed by the ZPL codec or renderer.
struct CanvasLayoutSettings {
    bool gridVisible = true;
    bool snapEnabled = true;
    bool rulersVisible = true;
    bool guidesVisible = true;
    bool marginsVisible = false;
    int gridStep = 8;
    int margin = 16;
    CanvasUnit unit = CanvasUnit::Millimeters;
    QList<int> horizontalGuides;
    QList<int> verticalGuides;

    bool operator==(const CanvasLayoutSettings&) const = default;
};

// Display conversions for editor annotations. Stored geometry stays in dots.
[[nodiscard]] double canvasUnitValue(double dots, CanvasUnit unit, int dpi);
// Millimetres keep two decimals; dots round to whole printer dots.
[[nodiscard]] QString formatCanvasLength(double dots, CanvasUnit unit, int dpi, const QLocale& locale);

} // namespace Designer
