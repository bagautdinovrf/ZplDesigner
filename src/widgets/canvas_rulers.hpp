#pragma once

#include "canvas_layout.hpp"

#include <QList>
#include <QLocale>
#include <QString>

namespace Designer {

// Ruler geometry of the canvas-guides concept, in screen pixels.
inline constexpr int rulerHeight = 20;       // horizontal ruler above the label
inline constexpr int rulerMinimumWidth = 32; // vertical ruler left of the label
inline constexpr int rulerGap = 18;          // between a ruler and the paper
inline constexpr int rulerMinorTick = 3;
inline constexpr int rulerMiddleTick = 5;
inline constexpr int rulerMajorTick = 8;
inline constexpr qreal rulerMinimumTickSpacing = 3;
// Regular labels keep this far from the label end and its size caption.
inline constexpr qreal rulerHorizontalEndClearance = 36;
inline constexpr qreal rulerVerticalEndClearance = 22;

struct RulerTick {
    qreal pixel = 0;  // screen offset from the label origin
    int length = 0;   // 3, 5 or 8 px; the label end spans the whole ruler
    QString label;    // labelled ticks only
    bool end = false; // the label width or height, labelled with its size
    bool operator==(const RulerTick&) const = default;
};

// Label width or height for the ruler end: "75,07" mm or whole dots.
[[nodiscard]] QString rulerEndLabel(int dots, int dpi, CanvasUnit unit, const QLocale& locale);

// Ticks every 1 mm (3 px), 5 mm (5 px) and 10 mm (8 px, labelled); dots use
// 10/50/100. The scale grows tenfold until labelled ticks are labelSpacing px
// apart. A level is hidden when its ticks would be closer than 3 px, and no
// regular label is placed within endClearance px of the end, which follows last.
[[nodiscard]] QList<RulerTick> rulerTicks(int extentDots, int dpi, CanvasUnit unit, qreal pixelsPerDot,
                                          qreal labelSpacing, qreal endClearance, const QLocale& locale);

} // namespace Designer
