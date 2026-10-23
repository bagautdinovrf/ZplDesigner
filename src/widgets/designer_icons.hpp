#pragma once

#include "designer_theme.hpp"

#include <QIcon>

class QPainter;
class QRectF;

namespace Designer {

// Outline icons of the canvas-guides concept (design/canvas-guides-dialog,
// ICONS in bundle.py): a 16-unit grid stroked at 1.4 units with round caps.
enum class DesignerIcon { Grid, Magnet, More, Reset, Trash, Plus, Close, GuideHorizontal, GuideVertical };

void paintDesignerIcon(QPainter* painter, const QRectF& rect, DesignerIcon icon, const QColor& color);
// Colours follow the theme at paint time. QIcon::Selected and QIcon::On use the
// active colour, like the caption of a checked button.
[[nodiscard]] QIcon designerIcon(DesignerIcon icon, DesignerColor idle = DesignerColor::Ink,
                                 DesignerColor active = DesignerColor::Accent);

} // namespace Designer
