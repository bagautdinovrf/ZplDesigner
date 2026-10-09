#pragma once

#include <QColor>

namespace Designer {

enum class DesignerTheme { Light, Dark };
enum class DesignerColor {
    Page, Panel, PanelAlternate, Ink, Muted, Border, Accent, Tint,
    Canvas, Grid, Shadow, Success, Warning, Control
};

// Tokens shared by the native widgets and the HTML workspace reference.
[[nodiscard]] QColor designerColor(DesignerColor color);

// Install one persistent style and initial font for the application lifetime.
void installDesignerStyle();
// A theme change updates only the palette, preserving widget metrics and fonts.
void setDesignerTheme(DesignerTheme theme);
[[nodiscard]] DesignerTheme designerTheme();

} // namespace Designer
