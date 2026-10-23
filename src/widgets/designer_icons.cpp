#include "designer_icons.hpp"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Designer {
namespace {

// SVG "A r r 0 large sweep x y" for a circle, from the path's current point.
void svgArc(QPainterPath& path, qreal radius, bool largeArc, bool sweep, QPointF end)
{
    const QPointF start = path.currentPosition();
    const QPointF delta = start - end;
    const qreal chord = std::hypot(delta.x(), delta.y());
    if (chord == 0) return;
    const qreal r = std::max(radius, chord / 2);
    const qreal offset = std::sqrt(std::max(0.0, r * r - chord * chord / 4));
    const qreal sign = largeArc != sweep ? 1 : -1;
    const QPointF center = (start + end) / 2 + sign * offset * QPointF(delta.y(), -delta.x()) / chord;
    const auto angle = [center](QPointF point) {
        return std::atan2(point.y() - center.y(), point.x() - center.x()) * 180 / std::numbers::pi;
    };
    const qreal first = angle(start);
    qreal span = angle(end) - first;
    if (!sweep && span > 0) span -= 360;
    if (sweep && span < 0) span += 360;
    // SVG angles grow clockwise on screen; QPainterPath angles grow anticlockwise.
    path.arcTo(QRectF(center - QPointF(r, r), QSizeF(2 * r, 2 * r)), -first, -span);
}

void polyline(QPainterPath& path, std::initializer_list<QPointF> points)
{
    path.moveTo(*points.begin());
    for (auto point = points.begin() + 1; point != points.end(); ++point) path.lineTo(*point);
}

QPainterPath strokePath(DesignerIcon icon)
{
    QPainterPath path;
    switch (icon) {
    case DesignerIcon::Grid:
        path.addRoundedRect(QRectF(2.5, 2.5, 11, 11), 1, 1);
        for (const qreal at : {6.2, 9.8}) {
            polyline(path, {{at, 2.5}, {at, 13.5}});
            polyline(path, {{2.5, at}, {13.5, at}});
        }
        break;
    case DesignerIcon::Magnet:
        path.moveTo(3.5, 2.5);
        path.lineTo(6.5, 2.5);
        path.lineTo(6.5, 7.5);
        svgArc(path, 1.5, false, false, {9.5, 7.5});
        path.lineTo(9.5, 2.5);
        path.lineTo(12.5, 2.5);
        path.lineTo(12.5, 7.5);
        svgArc(path, 4.5, false, true, {3.5, 7.5});
        path.closeSubpath();
        polyline(path, {{3.5, 5}, {6.5, 5}});
        polyline(path, {{9.5, 5}, {12.5, 5}});
        // The concept's magnet spans 2.5..12; centre it on the grid like its neighbours.
        path.translate(0, 0.75);
        break;
    case DesignerIcon::Reset:
        polyline(path, {{2.8, 3}, {2.8, 6.4}, {6.2, 6.4}});
        path.moveTo(3.3, 6.2);
        svgArc(path, 5, true, true, {3.1, 9.6});
        break;
    case DesignerIcon::Trash:
        polyline(path, {{3, 4.5}, {13, 4.5}});
        polyline(path, {{6.5, 4.5}, {6.5, 3}, {9.5, 3}, {9.5, 4.5}});
        polyline(path, {{4.6, 4.5}, {5.2, 13}, {10.8, 13}, {11.4, 4.5}});
        break;
    case DesignerIcon::Plus:
        polyline(path, {{8, 3.5}, {8, 12.5}});
        polyline(path, {{3.5, 8}, {12.5, 8}});
        break;
    case DesignerIcon::Close:
        polyline(path, {{4.5, 4.5}, {11.5, 11.5}});
        polyline(path, {{11.5, 4.5}, {4.5, 11.5}});
        break;
    case DesignerIcon::Link:
        // Lucide "link": two open halves of a chain, 24 units scaled by 2/3.
        path.moveTo(6.667, 8.667);
        svgArc(path, 10.0 / 3, false, false, {11.693, 9.027});
        path.lineTo(13.693, 7.027);
        svgArc(path, 10.0 / 3, false, false, {8.98, 2.313});
        path.lineTo(7.833, 3.453);
        path.moveTo(9.333, 7.333);
        svgArc(path, 10.0 / 3, false, false, {4.307, 6.973});
        path.lineTo(2.307, 8.973);
        svgArc(path, 10.0 / 3, false, false, {7.02, 13.687});
        path.lineTo(8.16, 12.547);
        break;
    case DesignerIcon::More: case DesignerIcon::GuideHorizontal: case DesignerIcon::GuideVertical:
        break;
    }
    return path;
}

class DesignerIconEngine final : public QIconEngine {
public:
    DesignerIconEngine(DesignerIcon icon, DesignerColor idle, DesignerColor active)
        : icon_(icon), idle_(idle), active_(active) {}
    QIconEngine* clone() const override { return new DesignerIconEngine(icon_, idle_, active_); }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const bool active = mode == QIcon::Selected || state == QIcon::On;
        paintDesignerIcon(painter, rect, icon_, designerColor(active ? active_ : idle_));
    }

private:
    DesignerIcon icon_;
    DesignerColor idle_;
    DesignerColor active_;
};
} // namespace

void paintDesignerIcon(QPainter* painter, const QRectF& rect, DesignerIcon icon, const QColor& color)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 16, rect.height() / 16);
    QPen pen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    switch (icon) {
    case DesignerIcon::More:
        // The concept fills the dots and still strokes them at 1.4 units.
        painter->setBrush(color);
        for (const qreal x : {3.5, 8.0, 12.5}) painter->drawEllipse(QPointF(x, 8), 0.9, 0.9);
        break;
    case DesignerIcon::GuideHorizontal: case DesignerIcon::GuideVertical: {
        painter->save();
        painter->setOpacity(painter->opacity() * 0.45);
        painter->drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 1, 1);
        painter->restore();
        pen.setDashPattern({2.2 / 1.4, 1.6 / 1.4});
        painter->setPen(pen);
        if (icon == DesignerIcon::GuideHorizontal) painter->drawLine(QPointF(0.8, 8), QPointF(15.2, 8));
        else painter->drawLine(QPointF(8, 0.8), QPointF(8, 15.2));
        break;
    }
    default:
        painter->drawPath(strokePath(icon));
        break;
    }
    painter->restore();
}

QIcon designerIcon(DesignerIcon icon, DesignerColor idle, DesignerColor active)
{
    return QIcon(new DesignerIconEngine(icon, idle, active));
}

QIcon applicationIcon()
{
    // Every size of the .ico; the resource is registered by CMake (DESIGNER_APP_ICON).
    return QIcon(QStringLiteral(":/designer/icons/application.ico"));
}

} // namespace Designer
