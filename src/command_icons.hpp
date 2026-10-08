#pragma once

#include <QGuiApplication>
#include <QIcon>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace Designer {

enum class CommandIcon {
    Save, Undo, Redo, Print,
    AlignLeft, AlignHCenter, AlignRight, AlignTop, AlignVCenter, AlignBottom,
    DistributeHorizontal, DistributeVertical,
    Group, Ungroup, Cut, Copy, Paste, Duplicate, Delete
};

namespace CommandIconDetail {

class Engine final : public QIconEngine {
public:
    explicit Engine(CommandIcon command) : command_(command) {}

    [[nodiscard]] QIconEngine* clone() const override { return new Engine(command_); }
    [[nodiscard]] QString key() const override { return QStringLiteral("QtZpl.CommandIcon"); }
    [[nodiscard]] bool isNull() override { return false; }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        if (!painter || rect.isEmpty()) return;

        // Read colors at paint time: QAction icons survive palette/theme changes.
        const auto palette = QGuiApplication::palette();
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        const auto role = mode != QIcon::Disabled
                && (mode == QIcon::Active || mode == QIcon::Selected || state == QIcon::On)
            ? QPalette::Link : QPalette::Text;
        const qreal side = qMin(rect.width(), rect.height());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2.0,
                           rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 24.0, side / 24.0);
        painter->setPen(QPen(palette.color(group, role), 1.75, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        draw(*painter);
        painter->restore();
    }

    [[nodiscard]] QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return renderPixmap(size, mode, state, 1.0);
    }

    [[nodiscard]] QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode,
                                       QIcon::State state, qreal scale) override {
        return renderPixmap(size, mode, state, scale);
    }

private:
    [[nodiscard]] QPixmap renderPixmap(const QSize& size, QIcon::Mode mode,
                                       QIcon::State state, qreal scale) {
        if (size.isEmpty() || scale <= 0.0) return {};
        // Do not cache rendered colors; the same engine must reflect a new palette.
        QPixmap result(size * scale);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint{}, size), mode, state);
        return result;
    }

    static void line(QPainter& painter, qreal x1, qreal y1, qreal x2, qreal y2) {
        painter.drawLine(QPointF{x1, y1}, QPointF{x2, y2});
    }

    static void leftAlignment(QPainter& painter) {
        line(painter, 4, 3, 4, 21);
        painter.drawRoundedRect(QRectF{8, 5, 12, 4}, 0.5, 0.5);
        painter.drawRoundedRect(QRectF{8, 15, 7, 4}, 0.5, 0.5);
    }

    static void centerAlignment(QPainter& painter) {
        // Break the reference line around the objects to keep small sizes legible.
        line(painter, 12, 2, 12, 5);
        line(painter, 12, 9, 12, 15);
        line(painter, 12, 19, 12, 22);
        painter.drawRoundedRect(QRectF{4, 5, 16, 4}, 0.5, 0.5);
        painter.drawRoundedRect(QRectF{8, 15, 8, 4}, 0.5, 0.5);
    }

    static void horizontalDistribution(QPainter& painter) {
        line(painter, 3, 4, 3, 20);
        line(painter, 21, 4, 21, 20);
        painter.drawRoundedRect(QRectF{6, 8, 3, 8}, 0.5, 0.5);
        painter.drawRoundedRect(QRectF{10.5, 5, 3, 14}, 0.5, 0.5);
        painter.drawRoundedRect(QRectF{15, 8, 3, 8}, 0.5, 0.5);
    }

    static void copySheets(QPainter& painter) {
        QPainterPath rear;
        rear.moveTo(7, 16);
        rear.lineTo(4, 16);
        rear.lineTo(4, 3);
        rear.lineTo(15, 3);
        rear.lineTo(15, 6);
        painter.drawPath(rear);
        painter.drawRoundedRect(QRectF{8, 7, 12, 14}, 1, 1);
    }

    void draw(QPainter& painter) const {
        switch (command_) {
        case CommandIcon::Save: {
            QPainterPath body;
            body.moveTo(4, 3);
            body.lineTo(17, 3);
            body.lineTo(21, 7);
            body.lineTo(21, 21);
            body.lineTo(3, 21);
            body.lineTo(3, 4);
            body.closeSubpath();
            painter.drawPath(body);
            painter.drawRect(QRectF{7, 3, 9, 6});
            painter.drawRect(QRectF{7, 14, 10, 7});
            line(painter, 13, 5, 13, 7);
            break;
        }
        case CommandIcon::Undo:
        case CommandIcon::Redo: {
            if (command_ == CommandIcon::Redo) {
                painter.translate(24, 0);
                painter.scale(-1, 1);
            }
            QPainterPath arrow;
            arrow.moveTo(8, 4);
            arrow.lineTo(3, 9);
            arrow.lineTo(8, 14);
            arrow.moveTo(3, 9);
            arrow.lineTo(14, 9);
            arrow.cubicTo(22, 9, 23, 20, 14, 20);
            painter.drawPath(arrow);
            break;
        }
        case CommandIcon::Print: {
            QPainterPath printer;
            printer.moveTo(6, 9);
            printer.lineTo(6, 3);
            printer.lineTo(18, 3);
            printer.lineTo(18, 9);
            printer.moveTo(6, 18);
            printer.lineTo(3, 18);
            printer.lineTo(3, 9);
            printer.lineTo(21, 9);
            printer.lineTo(21, 18);
            printer.lineTo(18, 18);
            painter.drawPath(printer);
            painter.drawRect(QRectF{6, 14, 12, 7});
            line(painter, 17, 12, 18, 12);
            break;
        }
        case CommandIcon::AlignLeft:
            leftAlignment(painter);
            break;
        case CommandIcon::AlignRight:
            painter.translate(24, 0);
            painter.scale(-1, 1);
            leftAlignment(painter);
            break;
        case CommandIcon::AlignHCenter:
            centerAlignment(painter);
            break;
        case CommandIcon::AlignTop:
            painter.translate(24, 0);
            painter.rotate(90);
            leftAlignment(painter);
            break;
        case CommandIcon::AlignBottom:
            painter.translate(0, 24);
            painter.rotate(-90);
            leftAlignment(painter);
            break;
        case CommandIcon::AlignVCenter:
            painter.translate(24, 0);
            painter.rotate(90);
            centerAlignment(painter);
            break;
        case CommandIcon::DistributeHorizontal:
            horizontalDistribution(painter);
            break;
        case CommandIcon::DistributeVertical:
            painter.translate(24, 0);
            painter.rotate(90);
            horizontalDistribution(painter);
            break;
        case CommandIcon::Group:
            painter.drawRoundedRect(QRectF{3, 3, 18, 18}, 1, 1);
            painter.drawRect(QRectF{6.5, 6.5, 6, 6});
            painter.drawRect(QRectF{11.5, 11.5, 6, 6});
            break;
        case CommandIcon::Ungroup:
            painter.drawRect(QRectF{3, 3, 7, 7});
            painter.drawRect(QRectF{14, 14, 7, 7});
            line(painter, 15, 9, 21, 3);
            line(painter, 17, 3, 21, 3);
            line(painter, 21, 3, 21, 7);
            line(painter, 9, 15, 3, 21);
            line(painter, 3, 17, 3, 21);
            line(painter, 3, 21, 7, 21);
            break;
        case CommandIcon::Cut:
            painter.drawEllipse(QRectF{3, 3, 6, 6});
            painter.drawEllipse(QRectF{3, 15, 6, 6});
            line(painter, 8, 8, 21, 21);
            line(painter, 8, 16, 21, 3);
            break;
        case CommandIcon::Copy:
            copySheets(painter);
            break;
        case CommandIcon::Paste: {
            QPainterPath clipboard;
            clipboard.moveTo(8, 5);
            clipboard.lineTo(5, 5);
            clipboard.lineTo(5, 21);
            clipboard.lineTo(19, 21);
            clipboard.lineTo(19, 5);
            clipboard.lineTo(16, 5);
            painter.drawPath(clipboard);
            painter.drawRoundedRect(QRectF{8, 3, 8, 5}, 1, 1);
            line(painter, 9, 12, 15, 12);
            line(painter, 9, 16, 15, 16);
            break;
        }
        case CommandIcon::Duplicate:
            copySheets(painter);
            line(painter, 11, 14, 17, 14);
            line(painter, 14, 11, 14, 17);
            break;
        case CommandIcon::Delete: {
            line(painter, 3, 6, 21, 6);
            QPainterPath bin;
            bin.moveTo(9, 6);
            bin.lineTo(9, 3);
            bin.lineTo(15, 3);
            bin.lineTo(15, 6);
            bin.moveTo(5, 6);
            bin.lineTo(6, 21);
            bin.lineTo(18, 21);
            bin.lineTo(19, 6);
            painter.drawPath(bin);
            line(painter, 10, 10, 10, 17);
            line(painter, 14, 10, 14, 17);
            break;
        }
        }
    }

    CommandIcon command_;
};

} // namespace CommandIconDetail

[[nodiscard]] inline QIcon commandIcon(CommandIcon command) {
    return QIcon(new CommandIconDetail::Engine(command));
}

} // namespace Designer
