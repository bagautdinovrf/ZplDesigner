#include "canvas_layout.hpp"

#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

class CanvasUnitsTest final : public QObject {
    Q_OBJECT
private slots:
    void convertsDotsAtLabelResolution();
    void formatsLengthsInSelectedUnit();
};

void CanvasUnitsTest::convertsDotsAtLabelResolution()
{
    QCOMPARE(canvasUnitValue(203, CanvasUnit::Millimeters, 203), 25.4);
    QCOMPARE(canvasUnitValue(600, CanvasUnit::Millimeters, 300), 50.8);
    QCOMPARE(canvasUnitValue(244, CanvasUnit::Dots, 203), 244.0);
    // A malformed resolution must not divide by zero.
    QCOMPARE(canvasUnitValue(1, CanvasUnit::Millimeters, 0), 25.4);
}

void CanvasUnitsTest::formatsLengthsInSelectedUnit()
{
    const QLocale russian(QLocale::Russian, QLocale::Russia);
    QCOMPARE(formatCanvasLength(244, CanvasUnit::Millimeters, 203, russian), u"30,53 мм"_s);
    QCOMPARE(formatCanvasLength(27, CanvasUnit::Millimeters, 203, russian), u"3,38 мм"_s);
    QCOMPARE(formatCanvasLength(0, CanvasUnit::Millimeters, 203, russian), u"0,00 мм"_s);
    QCOMPARE(formatCanvasLength(244, CanvasUnit::Millimeters, 203, QLocale::c()), u"30.53 мм"_s);
    // Rendered bounds may be fractional; dots are reported as whole printer dots.
    QCOMPARE(formatCanvasLength(27.4, CanvasUnit::Dots, 203, russian), u"27 dots"_s);
    QCOMPARE(formatCanvasLength(26.6, CanvasUnit::Dots, 300, russian), u"27 dots"_s);
}

QTEST_MAIN(CanvasUnitsTest)
#include "test_canvas_units.moc"
