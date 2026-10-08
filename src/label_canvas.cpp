#include "label_canvas.hpp"
#include "designer_theme.hpp"

#include <QCursor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QHash>
#include <QImageReader>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <limits>

namespace Designer {
namespace {
constexpr auto elementMime = "application/x-qtzpl-element";
constexpr int rulerThickness = 24;
constexpr int maximumGuides = 128;
constexpr qreal minimumZoom = 0.1;
constexpr qreal maximumZoom = 8.0;

// Editor annotations are drawn on white paper in both themes. Keep their ink
// dark enough for that surface while deriving its hue from the active palette.
QColor paperInk(const QPalette& palette, QPalette::ColorRole role)
{
    const QColor color = palette.color(role);
    return color.lightness() > 128 ? color.darker(170) : color;
}

QColor translucent(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

void paintWorkspaceBackdrop(QPainter* painter, const QRect& area, bool grid, QPoint viewportOrigin = {})
{
    painter->save();
    painter->fillRect(area, designerColor(DesignerColor::Canvas));
    if (grid) {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(designerColor(DesignerColor::Grid), 1.6, Qt::SolidLine, Qt::RoundCap));
        const int firstX = ((5 - viewportOrigin.x()) % 10 + 10) % 10;
        const int firstY = ((5 - viewportOrigin.y()) % 10 + 10) % 10;
        QPolygonF dots;
        for (int y = firstY; y < area.height(); y += 10)
            for (int x = firstX; x < area.width(); x += 10)
                dots.append(QPointF(x, y));
        painter->drawPoints(dots);
    }
    painter->restore();
}

bool flipsHorizontal(const DesignDocument& document) { return document.printInverted != document.printMirror; }

// Scene coordinates describe final output; model origins remain the untransformed
// ZPL coordinates. These map geometric boundaries, rather than raster pixel indices.
QPointF displayPoint(const DesignDocument& document, QPointF point)
{
    return {flipsHorizontal(document) ? document.width - point.x() : point.x(),
            document.printInverted ? document.height - point.y() : point.y()};
}

QPointF logicalDelta(const DesignDocument& document, QPointF delta)
{
    return {flipsHorizontal(document) ? -delta.x() : delta.x(),
            document.printInverted ? -delta.y() : delta.y()};
}

QRectF displayRect(const DesignDocument& document, const QRectF& rect)
{
    return {flipsHorizontal(document) ? document.width - rect.x() - rect.width() : rect.x(),
            document.printInverted ? document.height - rect.y() - rect.height() : rect.y(), rect.width(), rect.height()};
}

QRect logicalFallbackBounds(const DesignDocument& document, const Element& element)
{
    const bool table = element.type == ElementType::Table;
    const int width = std::max(1, element.width);
    const int height = std::max(1, element.type == ElementType::Circle ? element.width : element.height);
    const int justification = element.originJustification < 0 ? document.defaultJustification : element.originJustification;
    return {element.x - (!table && justification == 1 ? width : 0),
            element.y - (!table && element.baselineOrigin ? height : 0), width, height};
}

std::optional<ElementType> droppedElement(const QMimeData* mime)
{
    if (!mime->hasFormat(elementMime))
        return std::nullopt;
    bool valid = false;
    const int value = QString::fromUtf8(mime->data(elementMime)).toInt(&valid);
    if (!valid || value < static_cast<int>(ElementType::Text)
        || value > static_cast<int>(ElementType::Diagonal))
        return std::nullopt;
    return static_cast<ElementType>(value);
}

QString droppedImage(const QMimeData* mime)
{
    for (const auto& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (QImageReader::supportedImageFormats().contains(QFileInfo(path).suffix().toLower().toLatin1()))
            return path;
    }
    return {};
}

QPen cosmeticPen(const QColor& color, qreal width = 1.0, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, width, style);
    pen.setCosmetic(true);
    return pen;
}

bool sameGestureElement(const Element& before, const Element& after)
{
    return before.id == after.id && before.type == after.type
        && before.locked == after.locked && before.editorVisible == after.editorVisible
        && before.groupId == after.groupId && before.imageKeepAspectRatio == after.imageKeepAspectRatio
        && before.x == after.x && before.y == after.y
        && before.width == after.width && before.height == after.height
        && before.fontHeight == after.fontHeight && before.fontWidth == after.fontWidth
        && before.moduleWidth == after.moduleWidth && before.thickness == after.thickness
        && before.orientation == after.orientation && before.text == after.text && before.literalText == after.literalText
        && before.useDefaultOrientation == after.useDefaultOrientation
        && before.baselineOrigin == after.baselineOrigin && before.originJustification == after.originJustification
        && before.textDirection == after.textDirection && before.characterSpacing == after.characterSpacing
        && before.humanReadable == after.humanReadable && before.reverse == after.reverse
        && before.gs1 == after.gs1
        && before.blockWidth == after.blockWidth && before.blockMaxLines == after.blockMaxLines
        && before.blockLineSpacing == after.blockLineSpacing && before.blockHangingIndent == after.blockHangingIndent
        && before.blockJustification == after.blockJustification && before.code128Mode == after.code128Mode
        && before.interpretationAbove == after.interpretationAbove
        && before.code128CheckDigit == after.code128CheckDigit && before.barcodeRatio == after.barcodeRatio
        && before.threshold == after.threshold && before.dither == after.dither
        && before.image == after.image;
}

QRect pendingRenderedBounds(const Element& before, const Element& after, QRect bounds)
{
    qreal horizontal = 1.0;
    qreal vertical = 1.0;
    if (after.type == ElementType::Text) {
        const int beforeWidth = before.fontWidth > 0 ? before.fontWidth : before.fontHeight;
        const int afterWidth = after.fontWidth > 0 ? after.fontWidth : after.fontHeight;
        horizontal = qreal(afterWidth) / std::max(1, beforeWidth);
        vertical = qreal(after.fontHeight) / std::max(1, before.fontHeight);
    } else if (isLinearBarcodeType(after.type)) {
        horizontal = qreal(after.moduleWidth) / std::max(1, before.moduleWidth);
        const int logicalHeight = before.orientation == u'R' || before.orientation == u'B'
            ? bounds.width() : bounds.height();
        const int caption = before.humanReadable ? std::max(0, logicalHeight - before.height) : 0;
        const int gap = std::min(5, caption);
        const int nextCaption = gap + qRound((caption - gap) * horizontal);
        vertical = qreal(after.height + nextCaption) / std::max(1, logicalHeight);
    } else if (after.type == ElementType::QrCode || after.type == ElementType::DataMatrix
               || after.type == ElementType::Aztec || after.type == ElementType::Gs1DataBar) {
        horizontal = vertical = qreal(after.moduleWidth) / std::max(1, before.moduleWidth);
    }
    if (after.orientation == u'R' || after.orientation == u'B')
        std::swap(horizontal, vertical);
    return {after.x + qRound((bounds.x() - before.x) * horizontal),
            after.y + qRound((bounds.y() - before.y) * vertical),
            std::max(1, qRound(bounds.width() * horizontal)),
            std::max(1, qRound(bounds.height() * vertical))};
}
} // namespace

LabelCanvas::LabelCanvas(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(&m_scene);
    setObjectName(QStringLiteral("labelCanvas"));
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter);
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setMinimumSize(320, 260);
    setAccessibleName(tr("Холст этикетки"));
    setAccessibleDescription(tr("Перетаскивайте элементы для перемещения. Ctrl/Shift + щелчок или рамка на пустом месте — выделение нескольких элементов.\n"
                  "Потяните маркер на стороне или в углу, чтобы изменить размер элемента. Ctrl + A — выделить всё.\n"
                  "Ctrl + колесо — масштаб · Пробел + перетаскивание — перемещение вида · Alt — без привязки\n"
                  "Стрелки — перемещение на 1 точку принтера · Shift + стрелки — на 10 точек"));
    m_horizontalRuler = new QWidget(viewport());
    m_verticalRuler = new QWidget(viewport());
    m_horizontalRuler->setObjectName(QStringLiteral("horizontalRuler"));
    m_verticalRuler->setObjectName(QStringLiteral("verticalRuler"));
    m_horizontalRuler->setToolTip(tr("Потяните на этикетку, чтобы добавить горизонтальную направляющую"));
    m_verticalRuler->setToolTip(tr("Потяните на этикетку, чтобы добавить вертикальную направляющую"));
    m_horizontalRuler->setAccessibleName(tr("Горизонтальная линейка"));
    m_verticalRuler->setAccessibleName(tr("Вертикальная линейка"));
    for (auto* ruler : {m_horizontalRuler, m_verticalRuler}) {
        ruler->installEventFilter(this);
        ruler->setMouseTracking(true);
    }
    m_horizontalRuler->setCursor(Qt::SplitVCursor);
    m_verticalRuler->setCursor(Qt::SplitHCursor);
    updateRulers();
}

void LabelCanvas::syncDocument(const DesignDocument& document)
{
    QHash<QUuid, qsizetype> previousIndexes;
    for (qsizetype index = 0; index < m_document.elements.size(); ++index)
        previousIndexes.insert(m_document.elements[index].id, index);
    QList<QUuid> selectedIds;
    for (const int index : m_selectedIndices)
        selectedIds.append(m_document.elements[index].id);
    QHash<QUuid, int> nextIndexes;
    QList<QRect> pendingBounds;
    QMap<int, QString> pendingErrors;
    pendingBounds.reserve(document.elements.size());
    for (qsizetype index = 0; index < document.elements.size(); ++index) {
        const auto& next = document.elements[index];
        nextIndexes.insert(next.id, static_cast<int>(index));
        QRect bounds = logicalFallbackBounds(document, next);
        const qsizetype previousIndex = previousIndexes.value(next.id, -1);
        if (const auto error = m_preview.elementErrors.constFind(static_cast<int>(previousIndex));
            previousIndex >= 0 && error != m_preview.elementErrors.cend())
            pendingErrors.insert(static_cast<int>(index), error.value());
        if (previousIndex >= 0 && previousIndex < m_preview.bounds.size()
            && !m_preview.bounds[previousIndex].isEmpty()) {
            auto previous = m_document.elements[previousIndex];
            auto effectiveNext = next;
            if (previous.useDefaultOrientation) previous.orientation = m_document.defaultOrientation;
            if (effectiveNext.useDefaultOrientation) effectiveNext.orientation = document.defaultOrientation;
            const bool renderedSize = next.type == ElementType::Text || isBarcodeType(next.type);
            if (next.type == previous.type && next.text == previous.text
                && effectiveNext.orientation == previous.orientation && next.humanReadable == previous.humanReadable
                && next.baselineOrigin == previous.baselineOrigin && next.originJustification == previous.originJustification
                && (next.originJustification >= 0 || document.defaultJustification == m_document.defaultJustification)
                && next.gs1 == previous.gs1
                && (renderedSize || (next.width == previous.width && next.height == previous.height)))
                bounds = pendingRenderedBounds(previous, effectiveNext, displayRect(m_document, m_preview.bounds[previousIndex]).toRect());
        }
        pendingBounds.append(displayRect(document, bounds).toRect());
    }
    const bool pageChanged = !m_hasDocument || document.width != m_document.width
        || document.height != m_document.height;
    m_document = document;
    m_preview.bounds = std::move(pendingBounds);
    m_preview.elementErrors = std::move(pendingErrors);
    m_hasDocument = true;
    m_interaction = Interaction::None;
    QList<int> nextSelection;
    for (const auto& id : selectedIds) {
        if (nextIndexes.contains(id)) nextSelection.append(nextIndexes.value(id));
    }
    setSelectedIndices(nextSelection);
    if (pageChanged) {
        m_scene.setSceneRect(labelRect().adjusted(-120, -100, 120, 100));
        if (m_fitMode) fitLabel();
    }
    viewport()->update();
    updateRulers();
}

void LabelCanvas::setDocument(const DesignDocument& document, const Preview& preview)
{
    const bool pageChanged = !m_hasDocument || document.width != m_document.width
        || document.height != m_document.height;
    QList<int> nextSelection;
    bool preserveGesture = !pageChanged && document.visualEditable
        && document.printInverted == m_document.printInverted && document.printMirror == m_document.printMirror
        && document.defaultOrientation == m_document.defaultOrientation
        && document.defaultJustification == m_document.defaultJustification;
    for (const int index : m_selectedIndices) {
        const auto& previous = m_document.elements[index];
        const auto it = std::ranges::find(document.elements, previous.id, &Element::id);
        if (it != document.elements.end()) {
            const int nextIndex = static_cast<int>(it - document.elements.begin());
            nextSelection.append(nextIndex);
            preserveGesture &= index == nextIndex && sameGestureElement(previous, *it);
        } else {
            preserveGesture = false;
        }
    }
    m_document = document;
    m_preview = preview;
    m_hasDocument = true;
    if (!preserveGesture)
        m_interaction = Interaction::None;
    setSelectedIndices(nextSelection);
    if (pageChanged) {
        m_scene.setSceneRect(labelRect().adjusted(-120, -100, 120, 100));
        if (m_fitMode) fitLabel();
    }
    viewport()->update();
    updateRulers();
}

void LabelCanvas::setSelectedIndex(int index)
{
    setSelectedIndices(index < 0 ? QList<int>{} : QList<int>{index});
}

void LabelCanvas::setSelectedIndices(const QList<int>& indices)
{
    QList<int> selected;
    if (m_document.visualEditable) {
        for (const int index : indices) {
            if (index >= 0 && index < m_document.elements.size() && !selected.contains(index))
                selected.append(index);
        }
        // Keep explicitly requested indices in order, including the primary
        // (last) element, while expanding every persistent group atomically.
        QList<int> additional;
        for (const int index : selected) {
            for (const int member : groupMembers(index)) {
                if (!selected.contains(member) && !additional.contains(member))
                    additional.append(member);
            }
        }
        selected = additional + selected;
    }
    if (selected == m_selectedIndices) return;
    const int previous = m_selectedIndex;
    m_interaction = Interaction::None;
    m_selectedIndices = std::move(selected);
    m_selectedIndex = m_selectedIndices.isEmpty() ? -1 : m_selectedIndices.back();
    viewport()->update();
    if (m_selectedIndex != previous) emit selectionChanged(m_selectedIndex);
    emit selectionSetChanged(m_selectedIndices);
}

int LabelCanvas::selectedIndex() const
{
    return m_selectedIndex;
}

QList<int> LabelCanvas::selectedIndices() const
{
    return m_selectedIndices;
}

void LabelCanvas::setGridVisible(bool visible)
{
    if (m_layout.gridVisible == visible) return;
    m_layout.gridVisible = visible;
    updateRulers();
    viewport()->update();
}

void LabelCanvas::setSnapEnabled(bool enabled)
{
    m_layout.snapEnabled = enabled;
}

void LabelCanvas::setLayoutSettings(const CanvasLayoutSettings& settings)
{
    CanvasLayoutSettings bounded = settings;
    bounded.gridStep = std::clamp(bounded.gridStep, 1, 4096);
    bounded.margin = std::clamp(bounded.margin, 0, 2048);
    const auto boundGuides = [](QList<int>& guides) {
        QList<int> result;
        for (const int value : guides) {
            if (value >= 0 && value <= 4096 && !result.contains(value)) result.append(value);
            if (result.size() == maximumGuides) break;
        }
        std::ranges::sort(result);
        guides = std::move(result);
    };
    boundGuides(bounded.horizontalGuides);
    boundGuides(bounded.verticalGuides);
    if (bounded == m_layout) return;
    if (m_interaction == Interaction::Guide) cancelInteraction();
    m_layout = std::move(bounded);
    updateRulers();
    viewport()->update();
}

const CanvasLayoutSettings& LabelCanvas::layoutSettings() const
{
    return m_layout;
}

void LabelCanvas::zoomIn()
{
    applyZoom(transform().m11() * 1.2, false);
}

void LabelCanvas::zoomOut()
{
    applyZoom(transform().m11() / 1.2, false);
}

void LabelCanvas::fitLabel()
{
    if (m_fitting || !m_hasDocument || m_document.width <= 0 || m_document.height <= 0)
        return;
    const QScopedValueRollback fitting(m_fitting, true);
    m_fitMode = true;
    // The scene includes extra workspace around the paper. AsNeeded bars can
    // oscillate when fitting to the changing viewport near their thresholds.
    // Fit mode keeps a stable viewport; manual zoom restores scrollable bars.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QRectF available(m_workspaceInsets.left(), m_workspaceInsets.top(),
        std::max(1, viewport()->width() - m_workspaceInsets.left() - m_workspaceInsets.right()),
        std::max(1, viewport()->height() - m_workspaceInsets.top() - m_workspaceInsets.bottom()));
    const qreal scale = std::clamp(std::min(available.width() / m_document.width,
                                          available.height() / m_document.height),
                                   minimumZoom, maximumZoom);
    setTransform(QTransform::fromScale(scale, scale));
    const QPointF viewCenter = labelRect().center()
        + (QRectF(viewport()->rect()).center() - available.center()) / scale;
    // Keep enough navigable scene around an asymmetric workspace. Otherwise
    // QGraphicsView's alignment would override centerOn when the scene fits.
    const QSizeF visibleSize(viewport()->width() / scale, viewport()->height() / scale);
    const QRectF visibleScene(viewCenter - QPointF(visibleSize.width() / 2, visibleSize.height() / 2), visibleSize);
    m_scene.setSceneRect(visibleScene.united(labelRect()).adjusted(-120, -100, 120, 100));
    centerOn(viewCenter);
    updateRulers();
    emit zoomChanged(qRound(scale * 100));
}

void LabelCanvas::actualSize()
{
    applyZoom(1.0, false);
}

void LabelCanvas::setZoomPercent(int percent)
{
    applyZoom(percent / 100.0, false);
}

bool LabelCanvas::isFitMode() const
{
    return m_fitMode;
}

void LabelCanvas::setWorkspaceInsets(const QMargins& insets)
{
    const QMargins bounded(std::max(0, insets.left()), std::max(0, insets.top()),
                           std::max(0, insets.right()), std::max(0, insets.bottom()));
    if (m_workspaceInsets == bounded) return;
    m_workspaceInsets = bounded;
    if (m_fitMode) fitLabel();
}

void LabelCanvas::applyZoom(qreal scale, bool keepMouseAnchor)
{
    scale = std::clamp(scale, minimumZoom, maximumZoom);
    const QPoint anchor = keepMouseAnchor ? viewport()->mapFromGlobal(QCursor::pos())
                                        : viewport()->rect().center();
    const QPointF previousPosition = mapToScene(anchor);
    m_fitMode = false;
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setTransform(QTransform::fromScale(scale, scale));
    const QPoint nextAnchor = keepMouseAnchor ? viewport()->mapFromGlobal(QCursor::pos())
                                            : viewport()->rect().center();
    const QPointF nextPosition = mapToScene(nextAnchor);
    const QPointF center = mapToScene(viewport()->rect().center());
    centerOn(center + previousPosition - nextPosition);
    updateRulers();
    emit zoomChanged(qRound(scale * 100));
}

QRectF LabelCanvas::labelRect() const
{
    return {0, 0, static_cast<qreal>(m_document.width), static_cast<qreal>(m_document.height)};
}

QRectF LabelCanvas::elementBounds(int index) const
{
    if (index < 0 || index >= m_document.elements.size())
        return {};
    if (index < m_preview.bounds.size() && !m_preview.bounds[index].isEmpty())
        return m_preview.bounds[index];
    const auto& element = m_document.elements[index];
    return displayRect(m_document, logicalFallbackBounds(m_document, element));
}

QRectF LabelCanvas::selectionRect() const
{
    QRectF rect = m_interaction == Interaction::Move || m_interaction == Interaction::Resize
        ? m_originBounds : selectedBounds();
    if (m_interaction == Interaction::Move)
        rect.translate(logicalDelta(m_document, m_dragPosition - m_originPosition));
    if (m_interaction == Interaction::Resize) {
        const qreal width = std::max(1.0, rect.width() + m_dragSize.width() - m_originSize.width());
        const qreal height = std::max(1.0, rect.height() + m_dragSize.height() - m_originSize.height());
        rect = {resizeGrowsNegative(true) ? rect.right() - width : rect.left(),
                resizeGrowsNegative(false) ? rect.bottom() - height : rect.top(), width, height};
    }
    return rect;
}

QRectF LabelCanvas::selectedBounds() const
{
    QRectF result;
    for (const int index : m_selectedIndices)
        if (m_document.elements[index].editorVisible) result = result.united(elementBounds(index));
    return result;
}

QList<int> LabelCanvas::groupMembers(int index) const
{
    if (index < 0 || index >= m_document.elements.size()) return {};
    const auto& group = m_document.elements[index].groupId;
    if (group.isNull()) return {index};
    QList<int> members;
    for (int candidate = 0; candidate < m_document.elements.size(); ++candidate) {
        if (m_document.elements[candidate].groupId == group) members.append(candidate);
    }
    return members;
}

bool LabelCanvas::canResize() const
{
    return m_document.visualEditable && m_selectedIndices.size() == 1 && m_selectedIndex >= 0
        && m_selectedIndex < m_document.elements.size() && !selectionLocked()
        && m_document.elements[m_selectedIndex].editorVisible
        && m_document.elements[m_selectedIndex].type != ElementType::MaxiCode;
}

bool LabelCanvas::selectionLocked() const {
    return std::ranges::any_of(m_selectedIndices, [this](int index) { return m_document.elements[index].locked; });
}

bool LabelCanvas::keepsResizeAspectRatio() const
{
    if (!canResize()) return false;
    const auto& element = m_document.elements[m_selectedIndex];
    return element.type == ElementType::Circle || element.type == ElementType::QrCode
        || element.type == ElementType::DataMatrix || element.type == ElementType::Aztec
        || element.type == ElementType::Gs1DataBar
        || (element.type == ElementType::Image && element.imageKeepAspectRatio);
}

bool LabelCanvas::resizeGrowsNegative(bool horizontal) const
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_document.elements.size()) return false;
    const auto& element = m_document.elements[m_selectedIndex];
    const auto origin = displayPoint(m_document, {qreal(element.x), qreal(element.y)});
    const auto bounds = m_interaction == Interaction::Resize ? m_originBounds : elementBounds(m_selectedIndex);
    // ^FT and right-anchored fields grow away from their baseline/origin. This
    // also covers whole-label reflection without changing the model origin.
    return horizontal ? origin.x() > bounds.center().x() : origin.y() > bounds.center().y();
}

QPointF LabelCanvas::resizeHandlePosition(ResizeHandle handle) const
{
    const auto rect = selectionRect();
    const qreal right = resizeGrowsNegative(true) ? rect.left() : rect.right();
    const qreal bottom = resizeGrowsNegative(false) ? rect.top() : rect.bottom();
    if (handle == ResizeHandle::Right) return {right, rect.center().y()};
    if (handle == ResizeHandle::Bottom) return {rect.center().x(), bottom};
    return {right, bottom};
}

LabelCanvas::ResizeHandle LabelCanvas::resizeHandleAt(QPoint viewportPosition) const
{
    if (!canResize()) return ResizeHandle::None;
    auto nearest = ResizeHandle::None;
    int nearestDistance = 100;
    for (const auto handle : {ResizeHandle::Corner, ResizeHandle::Right, ResizeHandle::Bottom}) {
        if (keepsResizeAspectRatio() && handle != ResizeHandle::Corner) continue;
        const QPoint center = mapFromScene(resizeHandlePosition(handle));
        const QPoint delta = viewportPosition - center;
        const int distance = delta.x() * delta.x() + delta.y() * delta.y();
        if (QRect(center - QPoint(7, 7), QSize(15, 15)).contains(viewportPosition)
            && distance < nearestDistance) {
            nearest = handle;
            nearestDistance = distance;
        }
    }
    return nearest;
}

Qt::CursorShape LabelCanvas::resizeCursor(ResizeHandle handle) const
{
    if (handle == ResizeHandle::Right) return Qt::SizeHorCursor;
    if (handle == ResizeHandle::Bottom) return Qt::SizeVerCursor;
    return resizeGrowsNegative(true) != resizeGrowsNegative(false) ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
}

int LabelCanvas::hitElement(QPointF scenePosition) const
{
    const qreal tolerance = 4.0 / transform().m11();
    for (int index = static_cast<int>(m_document.elements.size()) - 1; index >= 0; --index) {
        if (!m_document.elements[index].editorVisible) continue;
        if (elementBounds(index).adjusted(-tolerance, -tolerance, tolerance, tolerance)
                .contains(scenePosition))
            return index;
    }
    return -1;
}

QPoint LabelCanvas::snappedPosition(QPointF position, bool bypass) const
{
    const int x = snappedAxis(position.x(), true, bypass);
    const int y = snappedAxis(position.y(), false, bypass);
    const auto logical = displayPoint(m_document, {qreal(x), qreal(y)}).toPoint();
    return {std::clamp(logical.x(), 0, std::max(0, m_document.width - 1)),
            std::clamp(logical.y(), 0, std::max(0, m_document.height - 1))};
}

std::optional<int> LabelCanvas::nearbyAid(qreal position, bool horizontal) const
{
    const int extent = horizontal ? m_document.width : m_document.height;
    qreal nearestDistance = 6.0 / std::max(minimumZoom, transform().m11());
    std::optional<int> nearest;
    const auto consider = [&](int coordinate) {
        const qreal distance = std::abs(position - coordinate);
        if (coordinate >= 0 && coordinate <= extent && distance <= nearestDistance) {
            nearest = coordinate;
            nearestDistance = distance;
        }
    };
    if (m_layout.guidesVisible) {
        const auto& guides = horizontal ? m_layout.verticalGuides : m_layout.horizontalGuides;
        for (const int coordinate : guides) consider(coordinate);
    }
    if (m_layout.marginsVisible && m_layout.margin * 2 < std::min(m_document.width, m_document.height)) {
        consider(m_layout.margin);
        consider(extent - m_layout.margin);
    }
    return nearest;
}

int LabelCanvas::snappedAxis(qreal position, bool horizontal, bool bypass) const
{
    if (!m_layout.snapEnabled || bypass) return qRound(position);
    if (const auto aid = nearbyAid(position, horizontal)) return *aid;
    return qRound(position / m_layout.gridStep) * m_layout.gridStep;
}

std::optional<qreal> LabelCanvas::nearbyObjectOffset(const QRectF& bounds, bool horizontal,
                                                   bool resize) const
{
    qreal nearestDistance = 6.0 / std::max(minimumZoom, transform().m11());
    std::optional<qreal> nearest;
    const std::array<qreal, 3> anchors = horizontal
        ? std::array{bounds.left(), bounds.center().x(), bounds.right()}
        : std::array{bounds.top(), bounds.center().y(), bounds.bottom()};
    for (int index = 0; index < m_document.elements.size(); ++index) {
        if (m_selectedIndices.contains(index) || !m_document.elements[index].editorVisible) continue;
        const QRectF neighbor = elementBounds(index);
        const std::array<qreal, 3> targets = horizontal
            ? std::array{neighbor.left(), neighbor.center().x(), neighbor.right()}
            : std::array{neighbor.top(), neighbor.center().y(), neighbor.bottom()};
        for (const qreal target : targets) {
            const int resizedAnchor = resizeGrowsNegative(horizontal) ? 0 : 2;
            for (int anchor = 0; anchor < 3; ++anchor) {
                if (resize && anchor != resizedAnchor) continue;
                const qreal offset = target - anchors[anchor];
                if (std::abs(offset) <= nearestDistance) {
                    nearestDistance = std::abs(offset);
                    nearest = offset;
                }
            }
        }
    }
    return nearest;
}

QPoint LabelCanvas::boundedSelectionDelta(QPoint delta) const
{
    int leftmost = std::numeric_limits<int>::max();
    int topmost = std::numeric_limits<int>::max();
    int rightmost = std::numeric_limits<int>::min();
    int bottommost = std::numeric_limits<int>::min();
    for (const int index : m_selectedIndices) {
        const auto& element = m_document.elements[index];
        leftmost = std::min(leftmost, element.x);
        topmost = std::min(topmost, element.y);
        rightmost = std::max(rightmost, element.x);
        bottommost = std::max(bottommost, element.y);
    }
    if (m_selectedIndices.isEmpty()) return {};
    return {std::clamp(delta.x(), std::min(0, -leftmost), std::max(0, m_document.width - 1 - rightmost)),
            std::clamp(delta.y(), std::min(0, -topmost), std::max(0, m_document.height - 1 - bottommost))};
}

QPoint LabelCanvas::dragPosition(QPointF delta, bool bypass) const
{
    if (delta.isNull()) return m_originPosition;
    const QPointF next = displayPoint(m_document, m_originPosition) + delta;
    const QRectF bounds = m_originBounds.translated(delta);
    const auto snap = [&](qreal position, bool horizontal) {
        if (!m_layout.snapEnabled || bypass) return qRound(position);
        if (const auto aid = nearbyAid(position, horizontal)) return *aid;
        if (const auto offset = nearbyObjectOffset(bounds, horizontal, false))
            return qRound(position + *offset);
        return snappedAxis(position, horizontal);
    };
    const QPoint snapped = displayPoint(m_document, {qreal(snap(next.x(), true)), qreal(snap(next.y(), false))}).toPoint();
    return m_originPosition + boundedSelectionDelta(snapped - m_originPosition);
}

QSize LabelCanvas::resizedDimensions(QPointF delta, bool bypass) const
{
    if (delta.isNull()) return m_originSize;
    delta = {resizeGrowsNegative(true) ? -delta.x() : delta.x(), resizeGrowsNegative(false) ? -delta.y() : delta.y()};
    const int step = m_layout.snapEnabled && !bypass ? m_layout.gridStep : 1;
    const auto snapped = [this, step, bypass](qreal value, bool horizontal) {
        if (m_layout.snapEnabled && !bypass) {
            auto bounds = m_originBounds;
            const bool flipped = resizeGrowsNegative(horizontal);
            if (horizontal) {
                if (flipped) bounds.setLeft(bounds.left() - value + m_originSize.width());
                else bounds.setRight(bounds.right() + value - m_originSize.width());
            } else {
                if (flipped) bounds.setTop(bounds.top() - value + m_originSize.height());
                else bounds.setBottom(bounds.bottom() + value - m_originSize.height());
            }
            const qreal edge = horizontal ? (flipped ? bounds.left() : bounds.right()) : (flipped ? bounds.top() : bounds.bottom());
            const qreal sign = flipped ? -1.0 : 1.0;
            if (const auto aid = nearbyAid(edge, horizontal))
                return std::clamp(qRound(value + sign * (*aid - edge)), 1, 4096);
            if (const auto offset = nearbyObjectOffset(bounds, horizontal, true))
                return std::clamp(qRound(value + sign * *offset), 1, 4096);
        }
        return std::clamp(qRound(value / step) * step, 1, 4096);
    };
    int width = m_originSize.width();
    int height = m_originSize.height();
    if (keepsResizeAspectRatio()) {
        // Use the axis that moved proportionally farther, including shrink
        // gestures with no movement on the other axis.
        const qreal horizontalChange = delta.x() / std::max(1, width);
        const qreal verticalChange = delta.y() / std::max(1, height);
        if (std::abs(horizontalChange) >= std::abs(verticalChange)) {
            const int maximumWidth = std::clamp(static_cast<int>(std::floor(4096.0 * width / height)), 1, 4096);
            width = std::min(snapped(width + delta.x(), true), maximumWidth);
            height = std::clamp(qRound(qreal(m_originSize.height()) * width / m_originSize.width()), 1, 4096);
        } else {
            const int maximumHeight = std::clamp(static_cast<int>(std::floor(4096.0 * height / width)), 1, 4096);
            height = std::min(snapped(height + delta.y(), false), maximumHeight);
            width = std::clamp(qRound(qreal(m_originSize.width()) * height / m_originSize.height()), 1, 4096);
        }
    } else {
        if (m_resizeHandle != ResizeHandle::Bottom && !qFuzzyIsNull(delta.x()))
            width = snapped(width + delta.x(), true);
        if (m_resizeHandle != ResizeHandle::Right && !qFuzzyIsNull(delta.y()))
            height = snapped(height + delta.y(), false);
    }
    return {width, height};
}

void LabelCanvas::paintSmartGuides(QPainter* painter, const QRectF& bounds) const
{
    if (!m_layout.snapEnabled || m_snapBypassed) return;
    std::array<std::optional<QLineF>, 2> alignments;
    std::array<qreal, 2> alignmentLengths;
    alignmentLengths.fill(std::numeric_limits<qreal>::max());
    struct Distance {
        QLineF line;
        qreal dots = std::numeric_limits<qreal>::max();
    };
    std::array<Distance, 4> distances;
    const auto considerDistance = [&](int side, QLineF line, qreal dots) {
        if (dots > 0 && dots < distances[side].dots) distances[side] = {line, dots};
    };
    for (int index = 0; index < m_document.elements.size(); ++index) {
        if (m_selectedIndices.contains(index) || !m_document.elements[index].editorVisible) continue;
        const QRectF neighbor = elementBounds(index);
        for (const bool horizontal : {true, false}) {
            const int axis = horizontal ? 0 : 1;
            const std::array<qreal, 3> anchors = horizontal
                ? std::array{bounds.left(), bounds.center().x(), bounds.right()}
                : std::array{bounds.top(), bounds.center().y(), bounds.bottom()};
            const std::array<qreal, 3> targets = horizontal
                ? std::array{neighbor.left(), neighbor.center().x(), neighbor.right()}
                : std::array{neighbor.top(), neighbor.center().y(), neighbor.bottom()};
            for (const qreal anchor : anchors) {
                for (const qreal target : targets) {
                    // Half-dot centers cannot always coincide on an integer
                    // printer grid. Show their closest representable position.
                    if (std::abs(target - anchor) > 0.51) continue;
                    const QLineF line = horizontal
                        ? QLineF(target, std::min(bounds.top(), neighbor.top()),
                                 target, std::max(bounds.bottom(), neighbor.bottom()))
                        : QLineF(std::min(bounds.left(), neighbor.left()), target,
                                 std::max(bounds.right(), neighbor.right()), target);
                    if (line.length() < alignmentLengths[axis]) {
                        alignmentLengths[axis] = line.length();
                        alignments[axis] = line;
                    }
                }
            }
        }
        const qreal overlapTop = std::max(bounds.top(), neighbor.top());
        const qreal overlapBottom = std::min(bounds.bottom(), neighbor.bottom());
        if (overlapTop < overlapBottom) {
            const qreal y = (overlapTop + overlapBottom) / 2;
            if (neighbor.right() <= bounds.left())
                considerDistance(0, QLineF(neighbor.right(), y, bounds.left(), y), bounds.left() - neighbor.right());
            if (neighbor.left() >= bounds.right())
                considerDistance(1, QLineF(bounds.right(), y, neighbor.left(), y), neighbor.left() - bounds.right());
        }
        const qreal overlapLeft = std::max(bounds.left(), neighbor.left());
        const qreal overlapRight = std::min(bounds.right(), neighbor.right());
        if (overlapLeft < overlapRight) {
            const qreal x = (overlapLeft + overlapRight) / 2;
            if (neighbor.bottom() <= bounds.top())
                considerDistance(2, QLineF(x, neighbor.bottom(), x, bounds.top()), bounds.top() - neighbor.bottom());
            if (neighbor.top() >= bounds.bottom())
                considerDistance(3, QLineF(x, bounds.bottom(), x, neighbor.top()), neighbor.top() - bounds.bottom());
        }
    }
    painter->save();
    painter->setWorldTransform(viewportTransform().inverted(), true);
    const QColor color = paperInk(palette(), QPalette::Link);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(cosmeticPen(color, 1, Qt::DashLine));
    for (const auto& line : alignments) {
        if (!line) continue;
        painter->drawLine(mapFromScene(line->p1()), mapFromScene(line->p2()));
    }
    QFont distanceFont = font();
    distanceFont.setPointSizeF(8.5);
    painter->setFont(distanceFont);
    painter->setPen(cosmeticPen(color));
    for (int side = 0; side < 4; ++side) {
        const auto& distance = distances[side];
        if (distance.dots == std::numeric_limits<qreal>::max()) continue;
        const QPoint from = mapFromScene(distance.line.p1());
        const QPoint to = mapFromScene(distance.line.p2());
        painter->drawLine(from, to);
        const QPoint tick = side < 2 ? QPoint(0, 3) : QPoint(3, 0);
        painter->drawLine(from - tick, from + tick);
        painter->drawLine(to - tick, to + tick);
        const QString text = m_layout.unit == CanvasUnit::Millimeters
            ? tr("%1 мм").arg(locale().toString(distance.dots * 25.4 / std::max(1, m_document.dpi), 'f', 2))
            : tr("%1 dots").arg(locale().toString(qRound(distance.dots)));
        const QSize size(painter->fontMetrics().horizontalAdvance(text) + 8,
                         painter->fontMetrics().height() + 4);
        const QPoint center = (from + to) / 2;
        QRect badge(side < 2 ? center - QPoint(size.width() / 2, size.height() + 4)
                             : center + QPoint(5, -size.height() / 2), size);
        // Keep the hint readable when a neighbor is near a viewport edge.
        badge.moveLeft(std::clamp(badge.left(), 0, std::max(0, viewport()->width() - badge.width())));
        badge.moveTop(std::clamp(badge.top(), 0, std::max(0, viewport()->height() - badge.height())));
        painter->fillRect(badge, palette().brush(QPalette::ToolTipBase));
        painter->setPen(palette().color(QPalette::ToolTipText));
        painter->drawText(badge, Qt::AlignCenter, text);
        painter->setPen(cosmeticPen(color));
    }
    painter->restore();
}

void LabelCanvas::drawBackground(QPainter* painter, const QRectF&)
{
    painter->save();
    // The surrounding workspace texture uses screen coordinates. The paper
    // grid is a separate foreground annotation aligned to printer dots.
    painter->save();
    // Remove only the scene transform. QWidget::render/grab may provide an
    // outer transform that must remain intact for this viewport's decorations.
    painter->setWorldTransform(viewportTransform().inverted(), true);
    paintWorkspaceBackdrop(painter, viewport()->rect(), m_layout.gridVisible);
    painter->restore();
    if (!m_hasDocument) {
        painter->restore();
        return;
    }

    const QRectF page = labelRect();
    const qreal scale = transform().m11();
    painter->setPen(Qt::NoPen);
    for (int spread = 8; spread >= 1; --spread) {
        painter->setBrush(QColor(0, 0, 0, 4));
        painter->drawRect(page.adjusted(-spread / scale, -spread / scale,
                                        spread / scale, spread / scale).translated(0, 3 / scale));
    }
    painter->fillRect(page, Qt::white);
    painter->save();
    painter->setClipRect(page);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(QPointF(0, 0), m_preview.image);
    painter->restore();
    painter->restore();
}

void LabelCanvas::drawForeground(QPainter* painter, const QRectF& exposed)
{
    if (!m_hasDocument)
        return;
    const qreal scale = transform().m11();
    const QRectF page = labelRect();
    // Selection is drawn on white paper in both themes, as in the reference.
    const QColor accentColor(36, 107, 221);
    painter->save();

    painter->save();
    painter->setClipRect(page);
    painter->setBrush(Qt::NoBrush);
    if (m_layout.gridVisible) {
        // Keep the visible grid legible while zoomed out without changing the
        // actual printer-dot snapping step. Paint only exposed paper lines.
        int step = m_layout.gridStep;
        while (step * scale < 8.0) step *= 2;
        const QRectF visible = page.intersected(exposed);
        QList<QLineF> lines;
        for (qreal x = std::ceil(visible.left() / step) * step; x <= visible.right(); x += step)
            lines.append(QLineF(x, visible.top(), x, visible.bottom()));
        for (qreal y = std::ceil(visible.top() / step) * step; y <= visible.bottom(); y += step)
            lines.append(QLineF(visible.left(), y, visible.right(), y));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        // Multiplication keeps printed black pixels intact under the editor
        // grid. The QtZpl image itself is never modified.
        painter->setCompositionMode(QPainter::CompositionMode_Multiply);
        painter->setPen(cosmeticPen(translucent(paperInk(palette(), QPalette::Mid), 60)));
        painter->drawLines(lines);
        painter->restore();
    }
    if (m_layout.marginsVisible && m_layout.margin * 2 < std::min(m_document.width, m_document.height)) {
        const int margin = m_layout.margin;
        painter->setPen(cosmeticPen(accentColor.darker(125), 1, Qt::DashLine));
        painter->drawRect(page.adjusted(margin, margin, -margin, -margin));
    }
    const auto drawGuide = [&](bool horizontal, int coordinate) {
        if (horizontal)
            painter->drawLine(QPointF(0, coordinate), QPointF(page.width(), coordinate));
        else
            painter->drawLine(QPointF(coordinate, 0), QPointF(coordinate, page.height()));
    };
    if (m_layout.guidesVisible) {
        painter->setPen(cosmeticPen(paperInk(palette(), QPalette::Link), 1, Qt::DashLine));
        for (qsizetype index = 0; index < m_layout.horizontalGuides.size(); ++index) {
            if (m_interaction != Interaction::Guide || !m_guideHorizontal || index != m_guideIndex)
                drawGuide(true, m_layout.horizontalGuides[index]);
        }
        for (qsizetype index = 0; index < m_layout.verticalGuides.size(); ++index) {
            if (m_interaction != Interaction::Guide || m_guideHorizontal || index != m_guideIndex)
                drawGuide(false, m_layout.verticalGuides[index]);
        }
    }
    if (m_interaction == Interaction::Guide && m_guideInside) {
        painter->setPen(cosmeticPen(paperInk(palette(), QPalette::Link), 2));
        drawGuide(m_guideHorizontal, m_guidePosition);
    }
    painter->restore();

    painter->setBrush(Qt::NoBrush);
    painter->setPen(cosmeticPen(designerColor(DesignerColor::Border)));
    painter->drawRect(page);

    // Decorations use viewport coordinates to remain legible at every zoom.
    painter->save();
    painter->setWorldTransform(viewportTransform().inverted(), true);
    const QRect viewportPage = mapFromScene(page).boundingRect();
    QFont captionFont = font();
    captionFont.setPixelSize(10);
    painter->setFont(captionFont);
    painter->setPen(designerColor(DesignerColor::Muted));
    const QString caption = tr("%1 × %2 dots · %3 × %4 мм")
        .arg(m_document.width).arg(m_document.height)
        .arg(m_document.width * 25.4 / std::max(1, m_document.dpi), 0, 'f', 2)
        .arg(m_document.height * 25.4 / std::max(1, m_document.dpi), 0, 'f', 2);
    painter->drawText(QRect(viewportPage.left() - 60, viewportPage.bottom() + 14,
                            viewportPage.width() + 120, 22), Qt::AlignCenter, caption);
    painter->restore();

    // Invalid data is an editor annotation, never pixels in the QtZpl raster.
    const QColor errorColor(185, 47, 47);
    for (auto error = m_preview.elementErrors.cbegin(); error != m_preview.elementErrors.cend(); ++error) {
        const int index = error.key();
        if (index < 0 || index >= m_document.elements.size() || !m_document.elements[index].editorVisible) continue;
        QRectF bounds = elementBounds(index);
        if (m_interaction == Interaction::Move && m_selectedIndices.contains(index))
            bounds.translate(logicalDelta(m_document, m_dragPosition - m_originPosition));
        else if (m_interaction == Interaction::Resize && index == m_selectedIndex)
            bounds = selectionRect();
        if (!bounds.intersects(page)) continue;
        painter->save();
        painter->setClipRect(page);
        painter->setPen(cosmeticPen(errorColor, 1.5));
        QColor fill = errorColor;
        fill.setAlpha(18);
        painter->setBrush(fill);
        painter->drawRect(bounds);
        painter->drawLine(bounds.topLeft(), bounds.bottomRight());
        painter->drawLine(bounds.topRight(), bounds.bottomLeft());
        painter->restore();

        painter->save();
        painter->setWorldTransform(viewportTransform().inverted(), true);
        painter->setFont(captionFont);
        const QString message = error.value().startsWith(QStringLiteral("Не заданы значения переменных:"))
            ? tr("Нет значений") : tr("Ошибка данных");
        const QSize badgeSize(painter->fontMetrics().horizontalAdvance(message) + 12,
                              painter->fontMetrics().height() + 6);
        const QPoint origin = mapFromScene(bounds.topLeft()) + QPoint(3, 3);
        const QRect badge(origin, badgeSize);
        painter->setPen(Qt::NoPen);
        painter->setBrush(errorColor);
        painter->drawRoundedRect(badge, 3, 3);
        painter->setPen(errorColor.lightness() < 160 ? Qt::white : Qt::black);
        painter->drawText(badge, Qt::AlignCenter, message);
        painter->restore();
    }

    if (m_document.visualEditable && m_selectedIndex >= 0 && !selectedBounds().isEmpty()) {
        const QRectF selected = selectionRect();
        if (m_selectedIndices.size() > 1) {
            painter->setPen(cosmeticPen(accentColor, 1, Qt::DashLine));
            for (const int index : m_selectedIndices) {
                if (!m_document.elements[index].editorVisible) continue;
                auto bounds = elementBounds(index);
                if (m_interaction == Interaction::Move)
                    bounds.translate(logicalDelta(m_document, m_dragPosition - m_originPosition));
                painter->drawRect(bounds);
            }
        }
        if (m_interaction == Interaction::Move || m_interaction == Interaction::Resize) {
            painter->setPen(cosmeticPen(translucent(accentColor, 100), 1, Qt::DashLine));
            painter->drawRect(m_originBounds);
            painter->setPen(cosmeticPen(translucent(accentColor, 105), 1, Qt::DashLine));
            painter->drawLine(QPointF(0, selected.top()), QPointF(selected.left(), selected.top()));
            painter->drawLine(QPointF(selected.left(), 0), QPointF(selected.left(), selected.top()));
            painter->fillRect(selected, translucent(accentColor, 24));
            paintSmartGuides(painter, selected);
        }
        painter->setPen(cosmeticPen(accentColor, 1.5));
        painter->setBrush(translucent(accentColor, 9));
        painter->drawRect(selected);
        if (canResize()) {
            const qreal handleSize = 6 / scale;
            painter->setPen(cosmeticPen(accentColor));
            painter->setBrush(Qt::white);
            for (const auto handle : {ResizeHandle::Right, ResizeHandle::Bottom, ResizeHandle::Corner}) {
                if (keepsResizeAspectRatio() && handle != ResizeHandle::Corner) continue;
                painter->drawRect(QRectF(resizeHandlePosition(handle) - QPointF(handleSize / 2, handleSize / 2),
                                        QSizeF(handleSize, handleSize)));
            }
        }
    }

    if (m_interaction == Interaction::RubberBand) {
        painter->setPen(cosmeticPen(accentColor, 1, Qt::DashLine));
        painter->setBrush(translucent(accentColor, 24));
        painter->drawRect(QRectF(m_pressScene, m_rubberEnd).normalized());
    }

    if (m_dropActive) {
        const auto position = displayPoint(m_document, m_dropPosition);
        painter->setPen(cosmeticPen(accentColor, 1.5, Qt::DashLine));
        painter->drawLine(QPointF(position.x(), 0), QPointF(position.x(), page.height()));
        painter->drawLine(QPointF(0, position.y()), QPointF(page.width(), position.y()));
        painter->setBrush(translucent(accentColor, 30));
        painter->drawEllipse(position, 8 / scale, 8 / scale);
    }
    painter->restore();
}

void LabelCanvas::changeEvent(QEvent* event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() != QEvent::PaletteChange) return;
    viewport()->update();
    for (auto* ruler : {m_horizontalRuler, m_verticalRuler})
        if (ruler) ruler->update();
}

void LabelCanvas::updateRulers()
{
    if (!m_horizontalRuler) return;
    for (auto* ruler : {m_horizontalRuler, m_verticalRuler})
        ruler->setVisible(m_hasDocument && m_layout.rulersVisible);
    if (!m_hasDocument || !m_layout.rulersVisible) return;
    // Rulers belong to the paper, so panning never leaves a detached L-shaped
    // frame at the viewport edges. Child widgets preserve guide drag gestures.
    const QRect paper = mapFromScene(labelRect()).boundingRect();
    m_horizontalRuler->setGeometry(paper.left(), paper.top() - rulerThickness,
                                   paper.width(), 17);
    m_verticalRuler->setGeometry(paper.left() - 30, paper.top(), rulerThickness, paper.height());
    for (auto* ruler : {m_horizontalRuler, m_verticalRuler}) {
        ruler->raise();
        ruler->update();
    }
}

void LabelCanvas::paintRuler(QWidget* ruler, bool horizontal)
{
    QPainter painter(ruler);
    paintWorkspaceBackdrop(&painter, ruler->rect(), m_layout.gridVisible, ruler->pos());
    QFont rulerFont = font();
    rulerFont.setPixelSize(9);
    painter.setFont(rulerFont);
    painter.setPen(designerColor(DesignerColor::Muted));
    if (!m_hasDocument) return;
    const qreal scale = std::max(minimumZoom, transform().m11());
    const qreal dotsPerUnit = m_layout.unit == CanvasUnit::Dots ? 1.0 : std::max(1, m_document.dpi) / 25.4;
    const qreal desiredStep = 100.0 / (dotsPerUnit * scale);
    const qreal power = std::pow(10.0, std::floor(std::log10(desiredStep)));
    qreal majorStep = power * 10;
    for (const qreal multiplier : {1.0, 2.0, 5.0, 10.0}) {
        if (power * multiplier >= desiredStep) {
            majorStep = power * multiplier;
            break;
        }
    }
    const qreal stepDots = majorStep * dotsPerUnit;
    const QPointF first = mapToScene(QPoint(0, 0));
    const QPointF last = mapToScene(viewport()->rect().bottomRight());
    const int extent = horizontal ? m_document.width : m_document.height;
    const qreal start = std::max(0.0, horizontal ? first.x() : first.y());
    const qreal end = std::min(qreal(extent), horizontal ? last.x() : last.y());
    const int firstTick = static_cast<int>(std::ceil(start / stepDots));
    const int lastTick = static_cast<int>(std::floor(end / stepDots));
    // Labels stay at least 100 screen pixels apart at every zoom.
    for (int tick = firstTick; tick <= lastTick && tick < firstTick + 2048; ++tick) {
        const qreal coordinate = tick * stepDots;
        const QPoint viewportPoint = horizontal ? mapFromScene(QPointF(coordinate, 0))
                                                : mapFromScene(QPointF(0, coordinate));
        const QPoint point = ruler->mapFrom(viewport(), viewportPoint);
        const int pixel = horizontal ? point.x() : point.y();
        const QString label = QString::number(coordinate / dotsPerUnit, 'f', majorStep < 1 ? 1 : 0);
        const int textWidth = painter.fontMetrics().horizontalAdvance(label);
        const int textHeight = painter.fontMetrics().height();
        if (horizontal) {
            const int x = std::clamp(pixel - textWidth / 2, 0, std::max(0, ruler->width() - textWidth));
            painter.drawText(QRect(x, 0, textWidth, ruler->height() - 2), Qt::AlignTop, label);
        } else {
            const int y = std::clamp(pixel - textHeight / 2, 0, std::max(0, ruler->height() - textHeight));
            painter.drawText(QRect(0, y, ruler->width() - 3, textHeight), Qt::AlignRight, label);
        }
    }
    painter.setPen(designerColor(DesignerColor::Border));
    if (horizontal)
        painter.drawLine(0, ruler->height() - 1, ruler->width(), ruler->height() - 1);
}

bool LabelCanvas::eventFilter(QObject* watched, QEvent* event)
{
    const bool horizontal = watched == m_horizontalRuler;
    if (!horizontal && watched != m_verticalRuler)
        return QGraphicsView::eventFilter(watched, event);
    auto* ruler = static_cast<QWidget*>(watched);
    if (event->type() == QEvent::Paint) {
        paintRuler(ruler, horizontal);
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove
        || event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint position = viewport()->mapFromGlobal(ruler->mapToGlobal(mouse->position().toPoint()));
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
            const auto& guides = horizontal ? m_layout.horizontalGuides : m_layout.verticalGuides;
            if (m_hasDocument && guides.size() < maximumGuides) {
                setFocus(Qt::MouseFocusReason);
                beginGuide(horizontal, -1, position);
            }
            return true;
        }
        if (m_interaction == Interaction::Guide) {
            if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton)
                finishGuide(position);
            else if (event->type() == QEvent::MouseMove)
                updateGuide(position);
            return true;
        }
    }
    return QGraphicsView::eventFilter(watched, event);
}

int LabelCanvas::guideAt(QPoint viewportPosition, bool& horizontal) const
{
    if (!m_layout.guidesVisible) return -1;
    const QPointF position = mapToScene(viewportPosition);
    if (!labelRect().contains(position)) return -1;
    qreal nearestDistance = 5.0 / std::max(minimumZoom, transform().m11());
    int nearest = -1;
    for (const bool axis : {false, true}) {
        const auto& guides = axis ? m_layout.horizontalGuides : m_layout.verticalGuides;
        const qreal coordinate = axis ? position.y() : position.x();
        for (qsizetype index = 0; index < guides.size(); ++index) {
            const qreal distance = std::abs(guides[index] - coordinate);
            if (distance <= nearestDistance) {
                nearestDistance = distance;
                nearest = static_cast<int>(index);
                horizontal = axis;
            }
        }
    }
    return nearest;
}

void LabelCanvas::beginGuide(bool horizontal, int index, QPoint viewportPosition)
{
    m_guideHorizontal = horizontal;
    m_guideIndex = index;
    m_interaction = Interaction::Guide;
    viewport()->setCursor(horizontal ? Qt::SplitVCursor : Qt::SplitHCursor);
    updateGuide(viewportPosition);
}

void LabelCanvas::updateGuide(QPoint viewportPosition)
{
    const QPointF position = mapToScene(viewportPosition);
    m_guideInside = viewport()->rect().contains(viewportPosition) && labelRect().contains(position);
    m_guidePosition = std::clamp(qRound(m_guideHorizontal ? position.y() : position.x()), 0, 4096);
    viewport()->update();
}

void LabelCanvas::finishGuide(QPoint viewportPosition)
{
    updateGuide(viewportPosition);
    auto settings = m_layout;
    auto& guides = m_guideHorizontal ? settings.horizontalGuides : settings.verticalGuides;
    if (m_guideIndex >= 0 && m_guideIndex < guides.size()) guides.removeAt(m_guideIndex);
    if (m_guideInside && !guides.contains(m_guidePosition)) {
        guides.append(m_guidePosition);
        settings.guidesVisible = true;
    }
    std::ranges::sort(guides);
    m_interaction = Interaction::None;
    if (settings != m_layout) {
        m_layout = settings;
        emit layoutSettingsEdited(settings);
    }
    updatePointer(viewportPosition);
    viewport()->update();
}

void LabelCanvas::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    updateRulers();
}

void LabelCanvas::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitMode)
        fitLabel();
    updateRulers();
}

void LabelCanvas::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::MiddleButton || (m_spaceDown && event->button() == Qt::LeftButton)) {
        m_interaction = Interaction::Pan;
        m_lastPan = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    bool horizontalGuide = false;
    const int guide = guideAt(event->position().toPoint(), horizontalGuide);
    if (event->button() == Qt::LeftButton && guide >= 0
        && resizeHandleAt(event->position().toPoint()) == ResizeHandle::None) {
        beginGuide(horizontalGuide, guide, event->position().toPoint());
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton || !m_document.visualEditable) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    emit editingStarted();
    const QPoint viewportPosition = event->position().toPoint();
    m_pressScene = mapToScene(viewportPosition);
    if (event->modifiers().testFlag(Qt::ControlModifier) && event->modifiers().testFlag(Qt::AltModifier)) {
        QList<int> hits;
        const qreal tolerance = 4.0 / transform().m11();
        for (int i = int(m_document.elements.size()) - 1; i >= 0; --i) {
            if (m_document.elements[i].editorVisible
                && elementBounds(i).adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(m_pressScene)) hits.append(i);
        }
        if (!hits.isEmpty()) {
            const int current = int(hits.indexOf(m_selectedIndex));
            setSelectedIndex(hits[(current + 1) % hits.size()]);
        }
        event->accept(); return;
    }
    const bool extend = event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::ShiftModifier);
    m_resizeHandle = extend ? ResizeHandle::None : resizeHandleAt(viewportPosition);
    const bool resize = m_resizeHandle != ResizeHandle::None;
    if (!resize) {
        const int hit = hitElement(m_pressScene);
        if (hit < 0) {
            m_rubberInitialSelection = m_selectedIndices;
            if (!extend) setSelectedIndex(-1);
            m_rubberEnd = m_pressScene;
            m_interaction = Interaction::RubberBand;
            viewport()->setCursor(Qt::CrossCursor);
            event->accept();
            return;
        }
        if (extend) {
            auto selected = m_selectedIndices;
            if (selected.contains(hit)) {
                for (const int member : groupMembers(hit)) selected.removeAll(member);
            } else {
                selected.append(hit);
            }
            setSelectedIndices(selected);
            event->accept();
            return;
        }
        if (!m_selectedIndices.contains(hit)) setSelectedIndex(hit);
    }
    if (m_selectedIndex >= 0 && !selectionLocked()) {
        const auto& element = m_document.elements[m_selectedIndex];
        m_originPosition = m_dragPosition = QPoint(element.x, element.y);
        m_originBounds = selectedBounds();
        m_originSize = m_dragSize = QSize(element.width, element.height);
        if (element.type == ElementType::Text || isBarcodeType(element.type))
            m_originSize = m_dragSize = elementBounds(m_selectedIndex).size().toSize();
        else if (element.type == ElementType::Circle)
            m_originSize = m_dragSize = QSize(element.width, element.width);
        m_interaction = resize ? Interaction::Resize : Interaction::Move;
        m_snapBypassed = event->modifiers().testFlag(Qt::AltModifier);
        viewport()->setCursor(resize ? resizeCursor(m_resizeHandle) : Qt::ClosedHandCursor);
    }
    viewport()->update();
    event->accept();
}

void LabelCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_document.visualEditable) {
        const int index = hitElement(mapToScene(event->position().toPoint()));
        if (index >= 0 && !m_document.elements[index].locked && m_document.elements[index].type == ElementType::Table) {
            cancelInteraction(); setSelectedIndex(index); emit elementEditRequested(index); event->accept(); return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void LabelCanvas::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint viewportPosition = event->position().toPoint();
    if (m_interaction == Interaction::Guide) {
        updateGuide(viewportPosition);
        event->accept();
        return;
    }
    if (m_interaction == Interaction::Pan) {
        const QPoint delta = viewportPosition - m_lastPan;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_lastPan = viewportPosition;
        event->accept();
        return;
    }
    if (m_interaction == Interaction::RubberBand) {
        m_rubberEnd = mapToScene(viewportPosition);
        viewport()->update();
        event->accept();
        return;
    }
    if (m_interaction == Interaction::Move || m_interaction == Interaction::Resize) {
        const QPointF delta = mapToScene(viewportPosition) - m_pressScene;
        const bool bypass = event->modifiers().testFlag(Qt::AltModifier);
        m_snapBypassed = bypass;
        if (m_interaction == Interaction::Move)
            m_dragPosition = dragPosition(delta, bypass);
        else
            m_dragSize = resizedDimensions(delta, bypass);
        viewport()->update();
        event->accept();
        return;
    }
    updatePointer(viewportPosition);
    QGraphicsView::mouseMoveEvent(event);
}

void LabelCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_interaction == Interaction::Guide && event->button() == Qt::LeftButton) {
        finishGuide(event->position().toPoint());
        event->accept();
        return;
    }
    if (m_interaction == Interaction::None) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    const auto interaction = m_interaction;
    if (interaction == Interaction::RubberBand) {
        m_rubberEnd = mapToScene(event->position().toPoint());
        const auto area = QRectF(m_pressScene, m_rubberEnd).normalized();
        auto selected = m_selectedIndices;
        if (area.width() * transform().m11() >= 3 || area.height() * transform().m11() >= 3) {
            for (int index = 0; index < m_document.elements.size(); ++index) {
                if (m_document.elements[index].editorVisible && area.intersects(elementBounds(index)) && !selected.contains(index))
                    selected.append(index);
            }
        }
        m_interaction = Interaction::None;
        setSelectedIndices(selected);
        updatePointer(event->position().toPoint());
        viewport()->update();
        event->accept();
        return;
    }
    const QPointF delta = mapToScene(event->position().toPoint()) - m_pressScene;
    const bool bypass = event->modifiers().testFlag(Qt::AltModifier);
    if (interaction == Interaction::Resize)
        m_dragSize = resizedDimensions(delta, bypass);
    else if (interaction == Interaction::Move && !delta.isNull())
        m_dragPosition = dragPosition(delta, bypass);
    m_interaction = Interaction::None;
    if (interaction == Interaction::Move && m_dragPosition != m_originPosition) {
        if (m_selectedIndices.size() > 1)
            emit elementsMoved(m_selectedIndices, m_dragPosition - m_originPosition);
        else
            emit elementMoved(m_selectedIndex, m_dragPosition);
    } else if (interaction == Interaction::Resize && m_dragSize != m_originSize)
        emit elementResized(m_selectedIndex, m_dragSize);
    updatePointer(event->position().toPoint());
    viewport()->update();
    event->accept();
}

void LabelCanvas::updatePointer(QPoint viewportPosition)
{
    bool horizontalGuide = false;
    const int guide = guideAt(viewportPosition, horizontalGuide);
    if (m_spaceDown)
        viewport()->setCursor(Qt::OpenHandCursor);
    else if (const auto handle = resizeHandleAt(viewportPosition); handle != ResizeHandle::None)
        viewport()->setCursor(resizeCursor(handle));
    else if (guide >= 0)
        viewport()->setCursor(horizontalGuide ? Qt::SplitVCursor : Qt::SplitHCursor);
    else if (m_document.visualEditable && hitElement(mapToScene(viewportPosition)) >= 0
             && !m_document.elements[hitElement(mapToScene(viewportPosition))].locked)
        viewport()->setCursor(Qt::SizeAllCursor);
    else
        viewport()->unsetCursor();
}

void LabelCanvas::cancelInteraction()
{
    if (m_interaction == Interaction::RubberBand)
        setSelectedIndices(m_rubberInitialSelection);
    m_interaction = Interaction::None;
    viewport()->unsetCursor();
    viewport()->update();
}

void LabelCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            m_spaceDown = true;
            viewport()->setCursor(Qt::OpenHandCursor);
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_interaction != Interaction::None)
            cancelInteraction();
        else
            setSelectedIndex(-1);
        event->accept();
        return;
    }
    if (m_document.visualEditable && event->matches(QKeySequence::SelectAll)) {
        QList<int> selected;
        for (int index = 0; index < m_document.elements.size(); ++index)
            if (m_document.elements[index].editorVisible) selected.append(index);
        setSelectedIndices(selected);
        event->accept();
        return;
    }
    if (!m_document.visualEditable || m_selectedIndex < 0) {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit deleteRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_D && event->modifiers().testFlag(Qt::ControlModifier)) {
        emit duplicateRequested();
        event->accept();
        return;
    }
    const int step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
    QPoint delta;
    switch (event->key()) {
    case Qt::Key_Left: delta.setX(-step); break;
    case Qt::Key_Right: delta.setX(step); break;
    case Qt::Key_Up: delta.setY(-step); break;
    case Qt::Key_Down: delta.setY(step); break;
    default:
        QGraphicsView::keyPressEvent(event);
        return;
    }
    if (selectionLocked()) { event->accept(); return; }
    delta = logicalDelta(m_document, delta).toPoint();
    if (m_selectedIndices.size() > 1) {
        delta = boundedSelectionDelta(delta);
        if (!delta.isNull()) emit elementsMoved(m_selectedIndices, delta);
    } else {
        const auto& element = m_document.elements[m_selectedIndex];
        const QPoint position(element.x, element.y);
        const QPoint next = snappedPosition(displayPoint(m_document, position + delta), true);
        if (next != position) emit elementMoved(m_selectedIndex, next);
    }
    event->accept();
}

void LabelCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            m_spaceDown = false;
            updatePointer(viewport()->mapFromGlobal(QCursor::pos()));
        }
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void LabelCanvas::focusOutEvent(QFocusEvent* event)
{
    m_spaceDown = false;
    cancelInteraction();
    QGraphicsView::focusOutEvent(event);
}

void LabelCanvas::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const qreal steps = event->angleDelta().y() / 120.0;
        applyZoom(transform().m11() * std::pow(1.2, steps), true);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void LabelCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (m_document.visualEditable
        && (droppedElement(event->mimeData()).has_value() || !droppedImage(event->mimeData()).isEmpty())) {
        m_dropActive = true;
        m_dropPosition = snappedPosition(mapToScene(event->position().toPoint()));
        event->acceptProposedAction();
        viewport()->update();
        return;
    }
    event->ignore();
}

void LabelCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    if (!m_dropActive || !m_document.visualEditable) {
        event->ignore();
        return;
    }
    const QPointF position = mapToScene(event->position().toPoint());
    m_dropPosition = snappedPosition(position, event->modifiers().testFlag(Qt::AltModifier));
    if (labelRect().contains(position))
        event->acceptProposedAction();
    else
        event->ignore();
    viewport()->update();
}

void LabelCanvas::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_dropActive = false;
    viewport()->update();
    event->accept();
}

void LabelCanvas::dropEvent(QDropEvent* event)
{
    m_dropActive = false;
    viewport()->update();
    const QPointF position = mapToScene(event->position().toPoint());
    if (!m_document.visualEditable || !labelRect().contains(position)) {
        event->ignore();
        return;
    }
    const QPoint dots = snappedPosition(position, event->modifiers().testFlag(Qt::AltModifier));
    if (const auto type = droppedElement(event->mimeData())) {
        emit elementDropped(*type, dots);
        event->acceptProposedAction();
    } else if (const QString path = droppedImage(event->mimeData()); !path.isEmpty()) {
        emit imageDropped(path, dots);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

} // namespace Designer
