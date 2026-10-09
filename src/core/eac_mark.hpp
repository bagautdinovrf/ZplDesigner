#pragma once

#include "design_document.hpp"
#include <QPainterPath>

namespace Designer {
// The official EAC letter geometry is a square of 9 x 9 units. The inverse
// white-on-black variant adds a one-unit black border (11 x 11 units total).
QPainterPath eacMarkPath();
// Smallest overall dot size for which the actual letters are at least 5 mm.
int eacMinimumSize(int dpi, EacStyle style);
int eacGridUnits(EacStyle style);
int eacSizeForRequest(int size, int dpi, EacStyle style);
QList<Element> eacElements(const Element& element);
} // namespace Designer
