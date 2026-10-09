#pragma once

#include "design_document.hpp"
#include "canvas_layout.hpp"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMargins>
#include <optional>

class QLabel;

namespace Designer {

// Scene coordinates are printer dots. The image is always rendered by QtZpl;
// the view paints only editor decorations over that raster.
class LabelCanvas final : public QGraphicsView {
    Q_OBJECT

public:
    explicit LabelCanvas(QWidget* parent = nullptr);

    // Update editing state synchronously while an expensive raster is pending.
    void syncDocument(const DesignDocument& document);
    void setDocument(const DesignDocument& document, const Preview& preview);
    void setSelectedIndex(int index);
    [[nodiscard]] int selectedIndex() const;
    void setSelectedIndices(const QList<int>& indices);
    [[nodiscard]] QList<int> selectedIndices() const;
    void setGridVisible(bool visible);
    void setSnapEnabled(bool enabled);
    void setLayoutSettings(const CanvasLayoutSettings& settings);
    [[nodiscard]] const CanvasLayoutSettings& layoutSettings() const;
    void zoomIn();
    void zoomOut();
    void fitLabel();
    void actualSize();
    void setZoomPercent(int percent);
    void centerLabel();
    [[nodiscard]] bool isFitMode() const;
    // Reserve space for workspace chrome without changing printer geometry.
    void setWorkspaceInsets(const QMargins& insets);
    [[nodiscard]] QString dimensionsFooterText() const;

signals:
    // Flush a pending raster before capturing hit targets and gesture geometry.
    void editingStarted();
    void selectionChanged(int index);
    void selectionSetChanged(QList<int> indices);
    void elementMoved(int index, QPoint position);
    // Emitted instead of elementMoved when several elements move together.
    void elementsMoved(QList<int> indices, QPoint delta);
    // Shapes/images use model dots; text and barcodes use rendered bounds.
    void elementResized(int index, QSize size);
    void elementEditRequested(int index);
    void elementDropped(Designer::ElementType type, QPoint position);
    void imageDropped(QString path, QPoint position);
    void deleteRequested();
    void duplicateRequested();
    void zoomChanged(int percent);
    void layoutSettingsEdited(Designer::CanvasLayoutSettings settings);
    void dimensionsFooterChanged(QString text);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    enum class Interaction { None, Move, Resize, Pan, RubberBand, Guide };
    enum class ResizeHandle { None, Right, Bottom, Corner };

    [[nodiscard]] QRectF labelRect() const;
    [[nodiscard]] QRectF elementBounds(int index) const;
    [[nodiscard]] QRectF selectionRect() const;
    [[nodiscard]] QRectF selectedBounds() const;
    [[nodiscard]] QList<int> groupMembers(int index) const;
    [[nodiscard]] bool canResize() const;
    [[nodiscard]] bool selectionLocked() const;
    [[nodiscard]] bool keepsResizeAspectRatio() const;
    [[nodiscard]] bool resizeGrowsNegative(bool horizontal) const;
    [[nodiscard]] ResizeHandle resizeHandleAt(QPoint viewportPosition) const;
    [[nodiscard]] QPointF resizeHandlePosition(ResizeHandle handle) const;
    [[nodiscard]] Qt::CursorShape resizeCursor(ResizeHandle handle) const;
    [[nodiscard]] int hitElement(QPointF scenePosition) const;
    [[nodiscard]] QPoint snappedPosition(QPointF position, bool bypass = false) const;
    [[nodiscard]] int snappedAxis(qreal position, bool horizontal, bool bypass = false) const;
    [[nodiscard]] std::optional<int> nearbyAid(qreal position, bool horizontal) const;
    [[nodiscard]] std::optional<qreal> nearbyObjectOffset(const QRectF& bounds, bool horizontal,
                                                        bool resize) const;
    void paintSmartGuides(QPainter* painter, const QRectF& bounds) const;
    [[nodiscard]] QPoint boundedSelectionDelta(QPoint delta) const;
    [[nodiscard]] QPoint dragPosition(QPointF delta, bool bypass) const;
    [[nodiscard]] QSize resizedDimensions(QPointF delta, bool bypass) const;
    void applyZoom(qreal scale, std::optional<QPointF> viewportAnchor = std::nullopt);
    [[nodiscard]] QRectF availableViewportRect(QSizeF viewportSize) const;
    QSizeF updateScrollBarPolicies();
    void positionView(QPointF center);
    void updateDimensionsCaption();
    void positionDimensionsCaption();
    void updatePointer(QPoint viewportPosition);
    void cancelInteraction();
    void updateRulers();
    void paintRuler(QWidget* ruler, bool horizontal);
    [[nodiscard]] int guideAt(QPoint viewportPosition, bool& horizontal) const;
    void beginGuide(bool horizontal, int index, QPoint viewportPosition);
    void updateGuide(QPoint viewportPosition);
    void finishGuide(QPoint viewportPosition);

    QGraphicsScene m_scene;
    DesignDocument m_document;
    Preview m_preview;
    int m_selectedIndex = -1;
    QList<int> m_selectedIndices;
    CanvasLayoutSettings m_layout;
    QWidget* m_horizontalRuler = nullptr;
    QWidget* m_verticalRuler = nullptr;
    QLabel* m_dimensionsCaption = nullptr;
    QString m_dimensionsFooterText;
    QMargins m_workspaceInsets{35, 50, 35, 45};
    bool m_guideHorizontal = false;
    bool m_guideInside = false;
    int m_guideIndex = -1;
    int m_guidePosition = 0;
    bool m_fitMode = true;
    bool m_fitting = false;
    bool m_positioningView = false;
    QPointF m_viewCenter;
    bool m_spaceDown = false;
    bool m_dropActive = false;
    bool m_hasDocument = false;
    bool m_snapBypassed = false;
    Interaction m_interaction = Interaction::None;
    ResizeHandle m_resizeHandle = ResizeHandle::None;
    QPointF m_pressScene;
    QPointF m_rubberEnd;
    QList<int> m_rubberInitialSelection;
    QPoint m_originPosition;
    QPoint m_dragPosition;
    QRectF m_originBounds;
    QSize m_originSize;
    QSize m_dragSize;
    QPoint m_lastPan;
    QPoint m_dropPosition;
};

} // namespace Designer
