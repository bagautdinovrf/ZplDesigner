#pragma once

#include "canvas_layout.hpp"
#include "label_canvas.hpp"

#include <QDialog>
#include <QSize>

#include <optional>

class QPushButton;
class QTableWidget;

namespace Designer {

class PillBadge;

// Guide table opened by "Список…" in the canvas quick panel (the table of
// design/canvas-guides-dialog, variant 01). One row per guide with its axis,
// an editable position in the canvas units, the stored dots and a delete
// button. Edits apply together as one undo step when accepted.
class GuideListDialog final : public QDialog {
    Q_OBJECT

public:
    GuideListDialog(CanvasLayoutSettings settings, QSize page, int dpi, QWidget* parent = nullptr);
    [[nodiscard]] CanvasLayoutSettings settings() const { return settings_; }
    // Adds a guide on the given axis at the label centre or the nearest free
    // grid step, then selects it. Returns false at the 128-guide limit.
    bool addGuide(bool horizontal);
    // Moves a guide; positions are clamped to the label and kept unique.
    void moveGuide(CanvasGuide guide, int coordinate);
    void removeGuide(CanvasGuide guide);
    [[nodiscard]] std::optional<CanvasGuide> currentGuide() const;

signals:
    void currentGuideChanged(std::optional<Designer::CanvasGuide> guide);

private:
    friend class GuideDelegate;
    void refresh(std::optional<CanvasGuide> select = std::nullopt);
    [[nodiscard]] QList<int>& guides(bool horizontal);
    [[nodiscard]] int extent(bool horizontal) const;

    CanvasLayoutSettings settings_;
    QSize page_;
    int dpi_;
    QTableWidget* table_ = nullptr;
    PillBadge* count_ = nullptr;
    QPushButton* addHorizontal_ = nullptr;
    QPushButton* addVertical_ = nullptr;
    QPushButton* removeAll_ = nullptr;
};

} // namespace Designer
