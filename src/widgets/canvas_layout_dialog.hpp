#pragma once

#include "canvas_layout.hpp"

#include <QDialog>
#include <QSize>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace Designer {

class CanvasLayoutDialog final : public QDialog {
    Q_OBJECT
public:
    CanvasLayoutDialog(CanvasLayoutSettings settings, QSize page, int dpi, QWidget* parent = nullptr);
    [[nodiscard]] CanvasLayoutSettings settings() const;
    // Selection annotations are editor-wide workspace preferences, not label layout.
    void setGeometryLabelsVisible(bool sizes, bool coordinates);
    [[nodiscard]] bool geometrySizesVisible() const;
    [[nodiscard]] bool geometryCoordinatesVisible() const;

private:
    void refreshUnits();
    void refreshGuides(int axis);
    void updateHint();
    [[nodiscard]] double unitsPerDot() const;

    CanvasLayoutSettings settings_;
    QSize page_;
    int dpi_;
    QCheckBox* geometrySizes_ = nullptr;
    QCheckBox* geometryCoordinates_ = nullptr;
    QDoubleSpinBox* step_ = nullptr;
    QDoubleSpinBox* margin_ = nullptr;
    QDoubleSpinBox* position_[2]{};
    QListWidget* guides_[2]{};
    QLabel* hint_ = nullptr;
    QPushButton* apply_ = nullptr;
};

} // namespace Designer
