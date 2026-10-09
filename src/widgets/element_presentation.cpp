#include "element_presentation.hpp"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>

namespace Designer {

class ElementIconEngine final : public QIconEngine {
public:
    explicit ElementIconEngine(ElementType type) : type_(type) {}
    QIconEngine* clone() const override { return new ElementIconEngine(type_); }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
    painter->save();
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 32.0, rect.height() / 32.0);
    auto& p = *painter;
    const auto palette = QApplication::palette();
    const auto color = mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::Text)
        : mode == QIcon::Selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::PlaceholderText);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 1.8));
    switch (type_) {
    case ElementType::Text:
        p.drawLine(8, 8, 24, 8); p.drawLine(16, 8, 16, 25); p.drawLine(11, 25, 21, 25); break;
    case ElementType::Box: p.drawRoundedRect(QRectF(6, 7, 21, 18), 2, 2); break;
    case ElementType::Line: p.drawLine(6, 16, 26, 16); break;
    case ElementType::Diagonal: p.drawLine(6, 25, 26, 7); break;
    case ElementType::Circle: p.drawEllipse(QRectF(6, 6, 21, 21)); break;
    case ElementType::Ellipse: p.drawEllipse(QRectF(5, 9, 23, 15)); break;
    case ElementType::Image:
        p.drawRoundedRect(QRectF(5, 6, 23, 21), 2, 2); p.drawEllipse(QPointF(12, 12), 2, 2);
        p.drawPolyline(QPolygonF{{6, 24}, {15, 16}, {19, 20}, {24, 14}, {27, 18}}); break;
    case ElementType::Code128:
    case ElementType::Ean13:
    case ElementType::Ean8:
    case ElementType::UpcA:
    case ElementType::UpcE:
    case ElementType::Code39:
    case ElementType::Pdf417:
    case ElementType::Codabar:
    case ElementType::Code93:
    case ElementType::Interleaved2of5:
    case ElementType::Industrial2of5:
    case ElementType::Gs1DataBar:
    case ElementType::MicroPdf417:
        for (int x = 5; x < 28; x += 4) { p.setPen(QPen(color, x % 3 + 1)); p.drawLine(x, 7, x, 25); } break;
    case ElementType::QrCode:
    case ElementType::DataMatrix:
    case ElementType::Aztec:
    case ElementType::MaxiCode:
        p.drawRect(5, 5, 8, 8); p.drawRect(19, 5, 8, 8); p.drawRect(5, 19, 8, 8);
        p.fillRect(20, 20, 4, 4, color); p.fillRect(25, 25, 3, 3, color); break;
    case ElementType::Table:
        p.drawRect(5, 6, 23, 21);
        p.drawLine(5, 13, 28, 13); p.drawLine(5, 20, 28, 20);
        p.drawLine(13, 6, 13, 27); p.drawLine(21, 6, 21, 27); break;
    }
    painter->restore();
    }
private:
    ElementType type_;
};

QIcon elementIcon(ElementType type) { return QIcon(new ElementIconEngine(type)); }

QString elementSymbol(ElementType type) {
    if (type == ElementType::Text) return QStringLiteral("T");
    if (isLinearBarcodeType(type)) return QStringLiteral("▥");
    if (isBarcodeType(type)) return QStringLiteral("▦");
    if (type == ElementType::Table) return QStringLiteral("▤");
    if (type == ElementType::Circle || type == ElementType::Ellipse) return QStringLiteral("○");
    if (type == ElementType::Line || type == ElementType::Diagonal) return QStringLiteral("╱");
    if (type == ElementType::Image) return QStringLiteral("▧");
    return QStringLiteral("□");
}

} // namespace Designer
