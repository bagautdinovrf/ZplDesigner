#include "label_canvas.hpp"
#include "document_session.hpp"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

using namespace Designer;

namespace {
DesignDocument document(bool mirror, bool inverted)
{
    DesignDocument result;
    result.width = 400; result.height = 300;
    result.printMirror = mirror; result.printInverted = inverted;
    auto box = makeElement(ElementType::Box, {50, 60});
    box.width = 80; box.height = 40;
    result.elements = {box};
    return result;
}

Preview preview(const DesignDocument& document)
{
    Preview result;
    result.image = QImage(document.width, document.height, QImage::Format_Grayscale8);
    result.image.fill(Qt::white);
    for (const auto& element : document.elements)
        result.bounds.append(mapDocumentRect(document, {element.x, element.y, element.width, element.height}));
    return result;
}

void setup(LabelCanvas& canvas, const DesignDocument& document)
{
    canvas.resize(700, 540);
    canvas.setDocument(document, preview(document));
    CanvasLayoutSettings layout;
    layout.snapEnabled = false;
    layout.rulersVisible = false;
    layout.marginsVisible = false;
    canvas.setLayoutSettings(layout);
    canvas.show(); canvas.actualSize();
    QApplication::processEvents();
}

void drag(LabelCanvas& canvas, QPointF start, QPointF finish, Qt::KeyboardModifiers modifiers = Qt::AltModifier)
{
    const auto from = canvas.mapFromScene(start);
    const auto to = canvas.mapFromScene(finish);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, modifiers, from);
    QTest::mouseMove(canvas.viewport(), to);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, modifiers, to);
}

void transformData()
{
    QTest::addColumn<bool>("mirror"); QTest::addColumn<bool>("inverted");
    QTest::newRow("normal") << false << false;
    QTest::newRow("mirror") << true << false;
    QTest::newRow("inverted") << false << true;
    QTest::newRow("mirror-inverted") << true << true;
}
} // namespace

class CanvasTransformTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); qRegisterMetaType<ElementType>(); }
    void hitKeyboardAndPendingGeometry_data() { transformData(); }
    void hitKeyboardAndPendingGeometry();
    void draggingAndCornerResize_data() { transformData(); }
    void draggingAndCornerResize();
    void physicalGridAndGuides_data() { transformData(); }
    void physicalGridAndGuides();
    void paletteDropsAndGroups_data() { transformData(); }
    void paletteDropsAndGroups();
    void alignmentAndDistribution_data() { transformData(); }
    void alignmentAndDistribution();
    void baselineAndRightAnchorsResizeAwayFromOrigin_data() { transformData(); }
    void baselineAndRightAnchorsResizeAwayFromOrigin();
    void flippedPendingBoundsStaySelectable();
    void maxiCodeCannotResize();
    void hoverBorrowsGeometryLabels();
    void coordinateLabelsNameZplOrigins_data() { transformData(); }
    void coordinateLabelsNameZplOrigins();
};

void CanvasTransformTests::hitKeyboardAndPendingGeometry()
{
    QFETCH(bool, mirror); QFETCH(bool, inverted);
    auto doc = document(mirror, inverted);
    LabelCanvas canvas; setup(canvas, doc);
    const auto bounds = preview(doc).bounds.front();
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, canvas.mapFromScene(bounds.center()));
    QCOMPARE(canvas.selectedIndex(), 0);
    QSignalSpy moved(&canvas, &LabelCanvas::elementMoved);
    QTest::keyClick(&canvas, Qt::Key_Right);
    QCOMPARE(moved.size(), 1);
    const auto expected = QPoint(50, 60) + mapDocumentDelta(doc, {1, 0});
    QCOMPARE(moved.front().at(1).toPoint(), expected);
    doc.elements[0].x = expected.x(); doc.elements[0].y = expected.y();
    canvas.syncDocument(doc); // no render available yet
    QTest::keyClick(&canvas, Qt::Key_Down);
    QCOMPARE(moved.size(), 2);
    QCOMPARE(moved.back().at(1).toPoint(), expected + mapDocumentDelta(doc, {0, 1}));
}

void CanvasTransformTests::draggingAndCornerResize()
{
    QFETCH(bool, mirror); QFETCH(bool, inverted);
    const auto doc = document(mirror, inverted);
    LabelCanvas canvas; setup(canvas, doc);
    const auto bounds = preview(doc).bounds.front();
    QSignalSpy moved(&canvas, &LabelCanvas::elementMoved);
    drag(canvas, bounds.center(), bounds.center() + QPoint(17, 11));
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(50, 60) + mapDocumentDelta(doc, {17, 11}));
    canvas.setDocument(doc, preview(doc)); canvas.setSelectedIndex(0);
    QSignalSpy resized(&canvas, &LabelCanvas::elementResized);
    const auto corner = mapDocumentPoint(doc, {130, 100});
    drag(canvas, corner, corner + mapDocumentDelta(doc, {20, 10}));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(100, 50));
}

void CanvasTransformTests::physicalGridAndGuides()
{
    QFETCH(bool, mirror); QFETCH(bool, inverted);
    const auto doc = document(mirror, inverted);
    LabelCanvas canvas; setup(canvas, doc);
    auto layout = canvas.layoutSettings(); layout.snapEnabled = true; layout.gridStep = 8;
    canvas.setLayoutSettings(layout);
    const auto bounds = preview(doc).bounds.front();
    const auto origin = mapDocumentPoint(doc, {50, 60});
    QSignalSpy moved(&canvas, &LabelCanvas::elementMoved);
    drag(canvas, bounds.center(), bounds.center() + QPoint(7, 7), Qt::NoModifier);
    QCOMPARE(moved.size(), 1);
    const QPoint snapped(qRound((origin.x() + 7) / 8.0) * 8, qRound((origin.y() + 7) / 8.0) * 8);
    QCOMPARE(moved.front().at(1).toPoint(), mapDocumentPoint(doc, snapped));

    canvas.setDocument(doc, preview(doc)); canvas.setSelectedIndex(0);
    layout.verticalGuides = {200}; layout.guidesVisible = true;
    canvas.setLayoutSettings(layout);
    moved.clear();
    drag(canvas, bounds.center(), bounds.center() + QPoint(197 - origin.x(), 0), Qt::NoModifier);
    QCOMPARE(moved.size(), 1);
    QCOMPARE(mapDocumentPoint(doc, moved.front().at(1).toPoint()).x(), 200);
}

void CanvasTransformTests::paletteDropsAndGroups()
{
    QFETCH(bool, mirror); QFETCH(bool, inverted);
    auto doc = document(mirror, inverted);
    auto second = makeElement(ElementType::Box, {160, 130}); second.width = 40; second.height = 30;
    doc.elements.append(second);
    LabelCanvas canvas; setup(canvas, doc);
    QSignalSpy dropped(&canvas, &LabelCanvas::elementDropped);
    QMimeData mime;
    mime.setData("application/x-qtzpl-element", QByteArray::number(static_cast<int>(ElementType::Diagonal)));
    const auto point = canvas.mapFromScene(QPointF(120, 100));
    QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &drop);
    QCOMPARE(dropped.size(), 1);
    QCOMPARE(dropped.front().at(0).value<ElementType>(), ElementType::Diagonal);
    QCOMPARE(dropped.front().at(1).toPoint(), mapDocumentPoint(doc, {120, 100}));
    canvas.setSelectedIndices({0, 1});
    QSignalSpy moved(&canvas, &LabelCanvas::elementsMoved);
    QTest::keyClick(&canvas, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), mapDocumentDelta(doc, {10, 0}));
}

void CanvasTransformTests::alignmentAndDistribution()
{
    QFETCH(bool, mirror); QFETCH(bool, inverted);
    const auto doc = document(mirror, inverted);
    DocumentSession session; session.reset(doc);
    QVERIFY(session.alignElements({0}, ElementAlignment::Left, AlignmentReference::Label, preview(doc).bounds));
    const auto& aligned = session.document().elements.front();
    QCOMPARE(mapDocumentRect(doc, {aligned.x, aligned.y, aligned.width, aligned.height}).left(), 0);
    session.undoStack()->undo();
    QVERIFY(session.alignElements({0}, ElementAlignment::Top, AlignmentReference::Label, preview(doc).bounds));
    const auto& top = session.document().elements.front();
    QCOMPARE(mapDocumentRect(doc, {top.x, top.y, top.width, top.height}).top(), 0);

    auto multiple = doc; multiple.elements.clear();
    for (const auto& geometry : {QRect(50, 40, 40, 40), QRect(140, 40, 60, 40), QRect(300, 40, 80, 40)}) {
        auto box = makeElement(ElementType::Box, geometry.topLeft()); box.width = geometry.width(); box.height = geometry.height();
        multiple.elements.append(box);
    }
    session.reset(multiple);
    QVERIFY(session.distributeElements({0, 1, 2}, Qt::Horizontal, preview(multiple).bounds));
    QCOMPARE(session.document().elements[1].x, 165);
    QCOMPARE(session.document().elements[0].x, 50);
    QCOMPARE(session.document().elements[2].x, 300);
    session.reset({});
}

void CanvasTransformTests::flippedPendingBoundsStaySelectable()
{
    auto doc = document(false, false);
    LabelCanvas canvas; setup(canvas, doc); canvas.setSelectedIndex(0);
    doc.printMirror = true;
    canvas.syncDocument(doc);
    QCOMPARE(canvas.selectedIndex(), 0);
    canvas.setSelectedIndex(-1);
    const auto flipped = preview(doc).bounds.front();
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, canvas.mapFromScene(flipped.center()));
    QCOMPARE(canvas.selectedIndex(), 0);
}

void CanvasTransformTests::baselineAndRightAnchorsResizeAwayFromOrigin()
{
    QFETCH(bool, mirror); QFETCH(bool, inverted);
    auto doc = document(mirror, inverted);
    auto& box = doc.elements.front();
    box.x = 150; box.y = 160; box.baselineOrigin = true; box.originJustification = -1;
    doc.defaultJustification = 1;
    const auto rendered = renderEditorPreview(doc);
    QVERIFY2(rendered.has_value(), rendered ? "" : qPrintable(rendered.error()));
    QCOMPARE(rendered->bounds.front(), mapDocumentRect(doc, {70, 120, 80, 40}));
    LabelCanvas canvas; setup(canvas, doc); canvas.setDocument(doc, *rendered); canvas.setSelectedIndex(0);
    QSignalSpy resized(&canvas, &LabelCanvas::elementResized);
    const auto growingCorner = mapDocumentPoint(doc, {70, 120});
    drag(canvas, growingCorner, growingCorner + mapDocumentDelta(doc, {-20, -10}));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(100, 50));

    DocumentSession session; session.reset(doc);
    QVERIFY(session.alignElements({0}, ElementAlignment::Left, AlignmentReference::Label, {}));
    auto result = renderEditorPreview(session.document()); QVERIFY(result);
    QCOMPARE(result->bounds.front().left(), 0);
    session.undoStack()->undo();
    QVERIFY(session.alignElements({0}, ElementAlignment::Top, AlignmentReference::Label, {}));
    result = renderEditorPreview(session.document()); QVERIFY(result);
    QCOMPARE(result->bounds.front().top(), 0);
    session.reset({});
}

void CanvasTransformTests::maxiCodeCannotResize()
{
    auto doc = document(true, true);
    doc.elements = {makeElement(ElementType::MaxiCode, {50, 60})};
    LabelCanvas canvas; setup(canvas, doc); canvas.setSelectedIndex(0);
    QSignalSpy resized(&canvas, &LabelCanvas::elementResized);
    const auto& element = doc.elements.front();
    const auto corner = mapDocumentPoint(doc, {element.x + element.width, element.y + element.height});
    drag(canvas, corner, corner + QPoint(20, 10));
    QCOMPARE(resized.size(), 0);
}

void CanvasTransformTests::hoverBorrowsGeometryLabels()
{
    auto doc = document(false, false);
    auto other = makeElement(ElementType::Box, {250, 160});
    other.width = 60; other.height = 30;
    doc.elements.append(other);
    LabelCanvas canvas; setup(canvas, doc);
    const QImage idle = canvas.viewport()->grab().toImage();
    // Geometry labels belong to the element under the idle pointer...
    QTest::mouseMove(canvas.viewport(), canvas.mapFromScene(QPointF(280, 175)));
    QApplication::processEvents();
    const QImage hovered = canvas.viewport()->grab().toImage();
    QVERIFY(hovered != idle);
    // ...and disappear with it, leaving only editor state that existed before.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(canvas.viewport(), &leave);
    QApplication::processEvents();
    QCOMPARE(canvas.viewport()->grab().toImage(), idle);

    // A unit change repaints only the labels: without a selection nothing moves.
    auto layout = canvas.layoutSettings();
    const auto initialUnit = layout.unit;
    const auto otherUnit = initialUnit == CanvasUnit::Dots ? CanvasUnit::Millimeters : CanvasUnit::Dots;
    layout.unit = otherUnit;
    canvas.setLayoutSettings(layout);
    QApplication::processEvents();
    QCOMPARE(canvas.viewport()->grab().toImage(), idle);
    layout.unit = initialUnit;
    canvas.setLayoutSettings(layout);
    canvas.setSelectedIndex(0);
    QApplication::processEvents();
    const QImage selected = canvas.viewport()->grab().toImage();
    layout.unit = otherUnit;
    canvas.setLayoutSettings(layout);
    QApplication::processEvents();
    QVERIFY(canvas.viewport()->grab().toImage() != selected);

    // Each label kind can be switched off; with both off hover paints nothing.
    canvas.setGeometryLabelsVisible(false, true);
    QApplication::processEvents();
    const QImage coordinatesOnly = canvas.viewport()->grab().toImage();
    canvas.setGeometryLabelsVisible(false, false);
    QApplication::processEvents();
    const QImage bare = canvas.viewport()->grab().toImage();
    QVERIFY(coordinatesOnly != bare);
    canvas.setSelectedIndex(-1);
    QApplication::processEvents();
    QCOMPARE(canvas.viewport()->grab().toImage(), idle);
    QTest::mouseMove(canvas.viewport(), canvas.mapFromScene(QPointF(282, 176)));
    QApplication::processEvents();
    QCOMPARE(canvas.viewport()->grab().toImage(), idle);
}

void CanvasTransformTests::coordinateLabelsNameZplOrigins()
{
    QFETCH(bool, mirror); QFETCH(bool, inverted);
    auto doc = document(mirror, inverted);
    doc.elements.append(makeElement(ElementType::Text, {120, 150}));
    // Rendered text ink starts after its ^FO: glyph bearings and leading.
    auto shown = preview(doc);
    shown.bounds[1] = mapDocumentRect(doc, {126, 158, 90, 24});
    LabelCanvas canvas; setup(canvas, doc);
    canvas.setDocument(doc, shown);
    const QRectF ink = shown.bounds[1];
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, canvas.mapFromScene(ink.center()));
    QCOMPARE(canvas.selectedIndex(), 1);
    QVERIFY(elementZpl(doc.elements[1]).startsWith(QStringLiteral("^FO120,150")));
    QCOMPARE(canvas.geometryOrigin(1), QPoint(120, 150));
    QCOMPARE(canvas.geometryOrigin(0), QPoint(50, 60));

    // A drag names the origin it will commit; other fields keep theirs.
    QSignalSpy moved(&canvas, &LabelCanvas::elementMoved);
    const auto from = canvas.mapFromScene(ink.center());
    const auto to = canvas.mapFromScene(ink.center() + QPointF(17, 11));
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::AltModifier, from);
    QTest::mouseMove(canvas.viewport(), to);
    const QPoint landing = QPoint(120, 150) + mapDocumentDelta(doc, {17, 11});
    QCOMPARE(canvas.geometryOrigin(1), landing);
    QCOMPARE(canvas.geometryOrigin(0), QPoint(50, 60));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::AltModifier, to);
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), landing);
    QCOMPARE(canvas.geometryOrigin(1), QPoint(120, 150));

    // A group move shifts every selected origin by the committed delta.
    canvas.setDocument(doc, shown);
    canvas.setSelectedIndices({0, 1});
    QSignalSpy groupMoved(&canvas, &LabelCanvas::elementsMoved);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::AltModifier, from);
    QTest::mouseMove(canvas.viewport(), to);
    const QPoint delta = mapDocumentDelta(doc, {17, 11});
    QCOMPARE(canvas.geometryOrigin(0), QPoint(50, 60) + delta);
    QCOMPARE(canvas.geometryOrigin(1), QPoint(120, 150) + delta);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::AltModifier, to);
    QCOMPARE(groupMoved.size(), 1);
    QCOMPARE(groupMoved.front().at(1).toPoint(), delta);

    // The painted coordinates follow the model origin, not the rendered frame.
    canvas.setGeometryLabelsVisible(false, true);
    canvas.setSelectedIndex(1);
    QApplication::processEvents();
    const QImage before = canvas.viewport()->grab().toImage();
    auto shifted = doc;
    shifted.elements[1].y -= 3;
    canvas.setDocument(shifted, shown);
    QApplication::processEvents();
    QCOMPARE(canvas.geometryOrigin(1), QPoint(120, 147));
    QVERIFY(canvas.viewport()->grab().toImage() != before);
    canvas.setGeometryLabelsVisible(false, false);
    QApplication::processEvents();
    const QImage bare = canvas.viewport()->grab().toImage();
    canvas.setDocument(doc, shown);
    QApplication::processEvents();
    QCOMPARE(canvas.viewport()->grab().toImage(), bare);
}

QTEST_MAIN(CanvasTransformTests)
#include "test_canvas_transforms.moc"
