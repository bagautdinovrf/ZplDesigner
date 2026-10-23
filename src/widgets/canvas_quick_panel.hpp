#pragma once

#include "canvas_layout.hpp"
#include "designer_theme.hpp"

#include <QImage>
#include <QPointer>
#include <QWidget>

#include <functional>

class QAbstractButton;
class QCheckBox;
class QLabel;
class QPainterPath;
class QPushButton;

namespace Designer {

class DocumentSession;
class DotSpinBox;
class PillBadge;

// Quick canvas panel (design/canvas-guides-dialog, variant 05): a popup below
// the "···" button that applies each change at once. All changes made during one
// opening merge into one undo step. Selection labels are workspace preferences:
// the panel reports them and never puts them into the label history.
class CanvasQuickPanel final : public QWidget {
    Q_OBJECT

public:
    explicit CanvasQuickPanel(DocumentSession* session, QWidget* parent = nullptr);

    void setGeometryLabelsVisible(bool sizes, bool coordinates);
    // Editor-wide like selection labels: reported, never part of label history.
    void setRulersDocked(bool docked);
    [[nodiscard]] bool rulersDocked() const;
    [[nodiscard]] bool geometrySizesVisible() const;
    [[nodiscard]] bool geometryCoordinatesVisible() const;
    // Shows the panel below the anchor, its arrow pointing at the anchor, and
    // starts a new history group for this opening.
    void popup(QWidget* anchor);
    // The visible card in panel coordinates; the rest of the window is shadow.
    [[nodiscard]] QRect cardRect() const;

signals:
    void geometryLabelsChanged(bool sizes, bool coordinates);
    void rulersDockedChanged(bool docked);
    void guideListRequested();
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void apply(const std::function<void(CanvasLayoutSettings&)>& change);
    [[nodiscard]] QPainterPath cardPath() const;
    [[nodiscard]] const QImage& shadow();

    DocumentSession* session_;
    QPointer<QWidget> anchor_;
    quint64 group_ = 0;
    QAbstractButton* millimeters_ = nullptr;
    QAbstractButton* dots_ = nullptr;
    QAbstractButton* rulersAtLabel_ = nullptr;
    QAbstractButton* rulersDocked_ = nullptr;
    QCheckBox* grid_ = nullptr;
    QCheckBox* snap_ = nullptr;
    QCheckBox* margins_ = nullptr;
    QCheckBox* rulers_ = nullptr;
    QCheckBox* guides_ = nullptr;
    QCheckBox* geometrySizes_ = nullptr;
    QCheckBox* geometryCoordinates_ = nullptr;
    DotSpinBox* gridStep_ = nullptr;
    DotSpinBox* margin_ = nullptr;
    PillBadge* guideCount_ = nullptr;
    QPushButton* removeGuides_ = nullptr;
    QWidget* footer_ = nullptr;
    QImage shadow_;
    QSize shadowSize_;
    DesignerTheme shadowTheme_ = DesignerTheme::Light;
};

} // namespace Designer
