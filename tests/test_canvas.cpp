#include "label_canvas.hpp"
#include "designer_theme.hpp"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QMimeData>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QSignalSpy>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>
#include <QtMath>
#include <QUrl>

#include <memory>

using Designer::DesignDocument;
using Designer::ElementType;
using Designer::LabelCanvas;
using Designer::Preview;

namespace {
class PaintCounter final : public QObject {
public:
    int count = 0;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Paint) ++count;
        return false;
    }
};

class ResizeCounter final : public QObject {
public:
    int count = 0;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Resize) ++count;
        return false;
    }
};
} // namespace

class CanvasTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void hitTestingSelectsFrontElement();
    void dragSnapsAndCommitsOnce();
    void dragCanBypassSnap();
    void repeatedNudgesUseCurrentModelBeforeRendering();
    void resizeCommitsIntegerShapeDimensions();
    void resizeEdgesPreserveOtherDimension();
    void resizeRenderedContent_data();
    void resizeRenderedContent();
    void resizePreservesCircleAndMatrixProportions();
    void resizeClickDoesNotSnapDimensions();
    void resizeHonorsZoomAndBypassesSnap();
    void resizeHonorsGeometryLimit();
    void pendingPreviewDoesNotCancelResize();
    void editingStartedRefreshesBoundsBeforeResize();
    void escapeCancelsGesture();
    void escapeCancelsResize();
    void paletteDropPreservesTypeAndPosition();
    void imageFileDropPreservesLocalPath();
    void invalidDropIsRejected();
    void rawModeDisablesEditing();
    void selectionFollowsIdentityAcrossReorder();
    void zoomPreservesDocumentCoordinates();
    void modifierClickTogglesSelection();
    void rubberBandAndSelectAll();
    void groupDragCommitsOneDeltaAndDoesNotResize();
    void groupNudgesPreserveSpacingAtBoundary();
    void groupSelectionFollowsIdentities();
    void escapeCancelsGroupMoveAndRubberBand();
    void gridToggleRepaintsPaperAndWorkspaceAtEveryZoom_data();
    void gridToggleRepaintsPaperAndWorkspaceAtEveryZoom();
    void paperGridTracksPrinterDotsThroughZoomAndPan();
    void paperGridPreservesPrintedBlackPixels();
    void gridVisibilityDoesNotChangeSnapping();
    void configuredGridUsesPrinterDotsAtZoom();
    void layoutAidsLeaveRasterAndZplUnchanged();
    void rulerDragsCreateGuides_data();
    void rulerDragsCreateGuides();
    void guidesMoveDeleteAndCancel();
    void rulerDragTracksZoomAndPan();
    void rulersShowFinalLabelDimensions_data();
    void rulersShowFinalLabelDimensions();
    void guideAndMarginSnappingUsesScreenTolerance();
    void resizingSnapsToGuides();
    void layoutSettingsAreBounded();
    void fitViewportSettlesAcrossResizeAndLayoutChanges();
    void fitModeKeepsPanAndRestoresManualZoomScrollbars();
    void invalidDataMatrixMarkerIsEditorOnlyAndFollowsIdentity();
    void persistentGroupsSelectAndToggleAtomically();
    void persistentGroupsExpandRubberBandAndFollowModel();
    void imageResizeRespectsAspectRatioSetting();
    void smartSnappingUsesEdgesCentersAndZoom_data();
    void smartSnappingUsesEdgesCentersAndZoom();
    void smartSnappingUsesWholeSelectionBounds();
    void smartResizeSnapsDraggedEdges();
    void smartGuidesAndDistanceHintsAreTransientOverlays();

private:
    [[nodiscard]] QPoint viewPoint(QPointF scenePoint) const;
    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void acceptPendingMove(int index, QPoint position);

    DesignDocument document_;
    Preview preview_;
    std::unique_ptr<LabelCanvas> canvas_;
};

void CanvasTests::initTestCase()
{
    qRegisterMetaType<ElementType>();
}

void CanvasTests::init()
{
    document_ = {};
    document_.width = 400;
    document_.height = 300;
    auto box = Designer::makeElement(ElementType::Box, {40, 48});
    box.width = 80;
    box.height = 56;
    document_.elements.append(box);
    auto front = Designer::makeElement(ElementType::Box, {64, 64});
    front.width = front.height = 24;
    document_.elements.append(front);
    const auto rendered = Designer::renderPreview(document_);
    QVERIFY2(rendered.has_value(), rendered ? "" : qPrintable(rendered.error()));
    preview_ = *rendered;
    QCOMPARE(preview_.bounds.size(), 2);

    canvas_ = std::make_unique<LabelCanvas>();
    canvas_->resize(680, 520);
    canvas_->setDocument(document_, preview_);
    canvas_->show();
    QApplication::processEvents();
    canvas_->actualSize();
    canvas_->setFocus();
    QApplication::processEvents();
}

void CanvasTests::cleanup()
{
    canvas_.reset();
}

QPoint CanvasTests::viewPoint(QPointF scenePoint) const
{
    return canvas_->mapFromScene(scenePoint);
}

void CanvasTests::drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers)
{
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, modifiers, viewPoint(from));
    QTest::mouseEvent(QTest::MouseMove, canvas_->viewport(), Qt::NoButton, modifiers, viewPoint(to));
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, modifiers, viewPoint(to));
}

void CanvasTests::acceptPendingMove(int index, QPoint position)
{
    document_.elements[index].x = position.x();
    document_.elements[index].y = position.y();
    canvas_->syncDocument(document_);
}

void CanvasTests::hitTestingSelectsFrontElement()
{
    QSignalSpy selection(canvas_.get(), &LabelCanvas::selectionChanged);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QCOMPARE(canvas_->selectedIndex(), 0);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({70, 70}));
    QCOMPARE(canvas_->selectedIndex(), 1);
    canvas_->setDocument(document_, preview_);
    QCOMPARE(canvas_->selectedIndex(), 1);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({250, 240}));
    QCOMPARE(canvas_->selectedIndex(), -1);
    QCOMPARE(selection.size(), 3);
    QVERIFY(moved.isEmpty());
}

void CanvasTests::dragSnapsAndCommitsOnce()
{
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QTest::mouseMove(canvas_->viewport(), viewPoint({55, 60}));
    QTest::mouseMove(canvas_->viewport(), viewPoint({71, 71}));
    QVERIFY(moved.isEmpty());
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({71, 71}));
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(0).toInt(), 0);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(64, 64));
}

void CanvasTests::dragCanBypassSnap()
{
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    drag({44, 52}, {71, 71}, Qt::AltModifier);
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(67, 67));

    moved.clear();
    canvas_->setSnapEnabled(false);
    drag({44, 52}, {69, 69});
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(65, 65));
}

void CanvasTests::repeatedNudgesUseCurrentModelBeforeRendering()
{
    canvas_->setSelectedIndex(0);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    connect(canvas_.get(), &LabelCanvas::elementMoved, this, &CanvasTests::acceptPendingMove);
    // No render or event-loop delay between these commands: each must build on
    // the synchronously accepted document, rather than the last raster's state.
    QTest::keyClick(canvas_.get(), Qt::Key_Right);
    QTest::keyClick(canvas_.get(), Qt::Key_Right);
    QTest::keyClick(canvas_.get(), Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(canvas_.get(), Qt::Key_Down);
    QCOMPARE(moved.size(), 4);
    QCOMPARE(moved.at(0).at(1).toPoint(), QPoint(41, 48));
    QCOMPARE(moved.at(1).at(1).toPoint(), QPoint(42, 48));
    QCOMPARE(moved.at(2).at(1).toPoint(), QPoint(52, 48));
    QCOMPARE(moved.at(3).at(1).toPoint(), QPoint(52, 49));
    QCOMPARE(document_.elements[0].x, 52);
    QCOMPARE(document_.elements[0].y, 49);

    // Bounds must follow the pending model as well, for the next mouse action.
    canvas_->setSelectedIndex(-1);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({128, 52}));
    QCOMPARE(canvas_->selectedIndex(), 0);
}

void CanvasTests::resizeCommitsIntegerShapeDimensions()
{
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, corner + QPointF(19, 13));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(0).toInt(), 0);
    QCOMPARE(resized.front().at(1).toSize(), QSize(96, 72));
    QVERIFY(moved.isEmpty());
}

void CanvasTests::resizeHonorsGeometryLimit()
{
    document_.elements[0].width = 4090;
    document_.elements[0].height = 4090;
    // A large element is clipped by the label. Its visible resize handle is
    // still usable, but the model dimensions cannot exceed the core's limit.
    preview_.bounds[0] = QRect(40, 48, 360, 252);
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, corner + QPointF(25, 25));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(4096, 4096));
}

void CanvasTests::resizeEdgesPreserveOtherDimension()
{
    document_.elements[0].width = 81;
    document_.elements[0].height = 57;
    preview_.bounds[0] = QRect(40, 48, 81, 57);
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    const QRectF bounds(preview_.bounds[0]);
    const QPointF right(bounds.right(), bounds.center().y());
    QTest::mouseMove(canvas_->viewport(), viewPoint(right));
    QCOMPARE(canvas_->viewport()->cursor().shape(), Qt::SizeHorCursor);
    drag(right, right + QPointF(19, 21));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.takeFirst().at(1).toSize(), QSize(104, 57));

    const QPointF bottom(bounds.center().x(), bounds.bottom());
    QTest::mouseMove(canvas_->viewport(), viewPoint(bottom));
    QCOMPARE(canvas_->viewport()->cursor().shape(), Qt::SizeVerCursor);
    drag(bottom, bottom + QPointF(19, 13));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(81, 72));
    QVERIFY(moved.isEmpty());
}

void CanvasTests::resizeRenderedContent_data()
{
    QTest::addColumn<ElementType>("type");
    QTest::newRow("text") << ElementType::Text;
    QTest::newRow("code128") << ElementType::Code128;
    QTest::newRow("qr") << ElementType::QrCode;
    QTest::newRow("datamatrix") << ElementType::DataMatrix;
}

void CanvasTests::resizeRenderedContent()
{
    QFETCH(ElementType, type);
    document_.elements[0] = Designer::makeElement(type, {40, 48});
    // Rendered dimensions intentionally differ from the model's placeholder
    // width/height: the receiver scales font or modules using these bounds.
    preview_.bounds[0] = QRect(43, 51, 80, 80);
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::AltModifier, viewPoint(corner));
    QTest::mouseEvent(QTest::MouseMove, canvas_->viewport(), Qt::NoButton,
                      Qt::AltModifier, viewPoint(corner + QPointF(13, 13)));
    QVERIFY(resized.isEmpty());
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::AltModifier,
                        viewPoint(corner + QPointF(13, 13)));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(93, 93));
    QVERIFY(moved.isEmpty());
}

void CanvasTests::resizePreservesCircleAndMatrixProportions()
{
    document_.elements[0] = Designer::makeElement(ElementType::Circle, {40, 48});
    preview_.bounds[0] = QRect(40, 48, 100, 100);
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, corner + QPointF(-20, 0), Qt::AltModifier);
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.takeFirst().at(1).toSize(), QSize(80, 80));

    document_.elements[0] = Designer::makeElement(ElementType::DataMatrix, {40, 48});
    preview_.bounds[0] = QRect(40, 48, 80, 40);
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, corner + QPointF(0, 20), Qt::AltModifier);
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(120, 60));
}

void CanvasTests::resizeClickDoesNotSnapDimensions()
{
    document_.elements[0].width = 81;
    document_.elements[0].height = 57;
    preview_.bounds[0] = QRect(40, 48, 81, 57);
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(corner));
    QVERIFY(resized.isEmpty());
}

void CanvasTests::resizeHonorsZoomAndBypassesSnap()
{
    canvas_->setSelectedIndex(0);
    // Use exactly 2x to avoid viewport-pixel rounding in this assertion.
    canvas_->setTransform(QTransform::fromScale(2, 2));
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, corner + QPointF(19, 13), Qt::AltModifier);
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(99, 69));
}

void CanvasTests::pendingPreviewDoesNotCancelResize()
{
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::AltModifier, viewPoint(corner));
    QTest::mouseEvent(QTest::MouseMove, canvas_->viewport(), Qt::NoButton,
                      Qt::AltModifier, viewPoint(corner + QPointF(10, 10)));
    canvas_->setDocument(document_, preview_);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::AltModifier,
                        viewPoint(corner + QPointF(19, 13)));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(99, 69));
}

void CanvasTests::editingStartedRefreshesBoundsBeforeResize()
{
    document_.elements[0] = Designer::makeElement(ElementType::Text, {40, 48});
    preview_.bounds[0] = QRect(40, 48, 80, 40);
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    QSignalSpy started(canvas_.get(), &LabelCanvas::editingStarted);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    const QRect updatedBounds(40, 48, 128, 64);
    connect(canvas_.get(), &LabelCanvas::editingStarted, this, [&] {
        preview_.bounds[0] = updatedBounds;
        canvas_->setDocument(document_, preview_);
    });

    // The refreshed corner is outside the stale bounds. Both handle hit
    // testing and the captured starting size must use the new preview.
    const QPointF corner = QRectF(updatedBounds).bottomRight();
    drag(corner, corner + QPointF(16, 8), Qt::AltModifier);
    QCOMPARE(started.size(), 1);
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(144, 72));
    QVERIFY(moved.isEmpty());
}

void CanvasTests::escapeCancelsGesture()
{
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QTest::mouseMove(canvas_->viewport(), viewPoint({90, 100}));
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({90, 100}));
    QVERIFY(moved.isEmpty());
    QCOMPARE(canvas_->selectedIndex(), 0);
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QCOMPARE(canvas_->selectedIndex(), -1);
}

void CanvasTests::escapeCancelsResize()
{
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(corner));
    QTest::mouseMove(canvas_->viewport(), viewPoint(corner + QPointF(40, 40)));
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(corner + QPointF(40, 40)));
    QVERIFY(resized.isEmpty());
    QCOMPARE(canvas_->selectedIndex(), 0);
}

void CanvasTests::paletteDropPreservesTypeAndPosition()
{
    QSignalSpy dropped(canvas_.get(), &LabelCanvas::elementDropped);
    QMimeData mime;
    mime.setData("application/x-qtzpl-element", QByteArray::number(static_cast<int>(ElementType::QrCode)));
    const QPoint position = viewPoint({109, 117});
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas_->viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas_->viewport(), &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(dropped.size(), 1);
    QCOMPARE(qvariant_cast<ElementType>(dropped.front().at(0)), ElementType::QrCode);
    QCOMPARE(dropped.front().at(1).toPoint(), QPoint(112, 120));
}

void CanvasTests::imageFileDropPreservesLocalPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("logo.png"));
    QImage image(3, 2, QImage::Format_ARGB32);
    image.fill(Qt::black);
    QVERIFY(image.save(path));
    QSignalSpy dropped(canvas_.get(), &LabelCanvas::imageDropped);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    const QPoint position = viewPoint({160, 160});
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas_->viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas_->viewport(), &drop);
    QCOMPARE(dropped.size(), 1);
    QCOMPARE(dropped.front().at(0).toString(), path);
    QCOMPARE(dropped.front().at(1).toPoint(), QPoint(160, 160));
}

void CanvasTests::invalidDropIsRejected()
{
    QSignalSpy dropped(canvas_.get(), &LabelCanvas::elementDropped);
    QMimeData mime;
    mime.setData("application/x-qtzpl-element", "999");
    QDragEnterEvent enter(viewPoint({120, 120}), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas_->viewport(), &enter);
    QVERIFY(!enter.isAccepted());
    QVERIFY(dropped.isEmpty());
}

void CanvasTests::rawModeDisablesEditing()
{
    canvas_->setSelectedIndex(0);
    document_.visualEditable = false;
    canvas_->syncDocument(document_);
    QCOMPARE(canvas_->selectedIndex(), -1);
    canvas_->setSelectedIndex(0);
    QCOMPARE(canvas_->selectedIndex(), -1);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    QSignalSpy removed(canvas_.get(), &LabelCanvas::deleteRequested);
    QSignalSpy duplicated(canvas_.get(), &LabelCanvas::duplicateRequested);
    QSignalSpy dropped(canvas_.get(), &LabelCanvas::elementDropped);
    drag({44, 52}, {90, 100});
    QTest::keyClick(canvas_.get(), Qt::Key_Right);
    QTest::keyClick(canvas_.get(), Qt::Key_Delete);
    QTest::keyClick(canvas_.get(), Qt::Key_D, Qt::ControlModifier);
    QMimeData mime;
    mime.setData("application/x-qtzpl-element", "0");
    QDragEnterEvent enter(viewPoint({160, 160}), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas_->viewport(), &enter);
    QVERIFY(!enter.isAccepted());
    QVERIFY(moved.isEmpty());
    QVERIFY(removed.isEmpty());
    QVERIFY(duplicated.isEmpty());
    QVERIFY(dropped.isEmpty());
    QCOMPARE(canvas_->selectedIndex(), -1);
}

void CanvasTests::selectionFollowsIdentityAcrossReorder()
{
    canvas_->setSelectedIndex(0);
    const QUuid selectedId = document_.elements[0].id;
    document_.elements.move(0, 1);
    canvas_->syncDocument(document_);
    QCOMPARE(canvas_->selectedIndex(), 1);
    QCOMPARE(document_.elements[canvas_->selectedIndex()].id, selectedId);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QCOMPARE(canvas_->selectedIndex(), 1);
    document_.elements.removeAt(1);
    canvas_->syncDocument(document_);
    QCOMPARE(canvas_->selectedIndex(), -1);
}

void CanvasTests::zoomPreservesDocumentCoordinates()
{
    QSignalSpy zoom(canvas_.get(), &LabelCanvas::zoomChanged);
    canvas_->zoomIn();
    QVERIFY(canvas_->transform().m11() > 1.0);
    QCOMPARE(zoom.last().at(0).toInt(), 120);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QCOMPARE(canvas_->selectedIndex(), 0);
    canvas_->zoomOut();
    QCOMPARE(canvas_->transform().m11(), 1.0);
    canvas_->fitLabel();
    const QRect label = canvas_->mapFromScene(QRectF(0, 0, document_.width, document_.height)).boundingRect();
    QVERIFY(canvas_->viewport()->rect().contains(label));
    canvas_->actualSize();
    QCOMPARE(canvas_->transform().m11(), 1.0);
    QCOMPARE(zoom.last().at(0).toInt(), 100);
}

void CanvasTests::modifierClickTogglesSelection()
{
    QSignalSpy selection(canvas_.get(), &LabelCanvas::selectionSetChanged);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::ControlModifier, viewPoint({70, 70}));
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
    QCOMPARE(canvas_->selectedIndex(), 1);
    // Clicking any selected member keeps the entire set available for dragging.
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::ShiftModifier, viewPoint({70, 70}));
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0}));
    QCOMPARE(canvas_->selectedIndex(), 0);
    QCOMPARE(selection.size(), 3);
    canvas_->setSelectedIndices({0, 0, 1, -1, 99});
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
}

void CanvasTests::rubberBandAndSelectAll()
{
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementsMoved);
    drag({15, 20}, {130, 115});
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
    QVERIFY(moved.isEmpty());
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({250, 240}));
    QVERIFY(canvas_->selectedIndices().isEmpty());
    QTest::keyClick(canvas_.get(), Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QVERIFY(canvas_->selectedIndices().isEmpty());
    document_.visualEditable = false;
    canvas_->syncDocument(document_);
    QTest::keyClick(canvas_.get(), Qt::Key_A, Qt::ControlModifier);
    QVERIFY(canvas_->selectedIndices().isEmpty());
}

void CanvasTests::groupDragCommitsOneDeltaAndDoesNotResize()
{
    canvas_->setSelectedIndices({0, 1});
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementsMoved);
    QSignalSpy single(canvas_.get(), &LabelCanvas::elementMoved);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::AltModifier, viewPoint({44, 52}));
    QTest::mouseEvent(QTest::MouseMove, canvas_->viewport(), Qt::NoButton,
                      Qt::AltModifier, viewPoint({64, 82}));
    QVERIFY(moved.isEmpty());
    // A raster arriving during a gesture must keep its complete selection.
    canvas_->setDocument(document_, preview_);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::AltModifier, viewPoint({64, 82}));
    QCOMPARE(moved.size(), 1);
    QCOMPARE(qvariant_cast<QList<int>>(moved.front().at(0)), QList<int>({0, 1}));
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(20, 30));
    QVERIFY(single.isEmpty());
    QVERIFY(resized.isEmpty());
    const auto corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, corner + QPointF(13, 9), Qt::AltModifier);
    QCOMPARE(moved.size(), 2);
    QVERIFY(resized.isEmpty());
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
}

void CanvasTests::groupNudgesPreserveSpacingAtBoundary()
{
    document_.elements[0].x = 0;
    canvas_->syncDocument(document_);
    canvas_->setSelectedIndices({0, 1});
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementsMoved);
    connect(canvas_.get(), &LabelCanvas::elementsMoved, this, [&](QList<int> indices, QPoint delta) {
        for (const int index : indices) {
            document_.elements[index].x += delta.x();
            document_.elements[index].y += delta.y();
        }
        canvas_->syncDocument(document_);
    });
    QTest::keyClick(canvas_.get(), Qt::Key_Left);
    QVERIFY(moved.isEmpty());
    QTest::keyClick(canvas_.get(), Qt::Key_Right);
    QTest::keyClick(canvas_.get(), Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(canvas_.get(), Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(moved.size(), 3);
    QCOMPARE(document_.elements[0].x, 1);
    QCOMPARE(document_.elements[1].x, 65);
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
    QTest::keyClick(canvas_.get(), Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(moved.last().at(1).toPoint(), QPoint(-1, 0));
    QCOMPARE(document_.elements[0].x, 0);
    QCOMPARE(document_.elements[1].x, 64);
}

void CanvasTests::groupSelectionFollowsIdentities()
{
    canvas_->setSelectedIndices({0, 1});
    const auto primaryId = document_.elements[1].id;
    document_.elements.move(0, 1);
    preview_.bounds.move(0, 1);
    canvas_->setDocument(document_, preview_);
    QCOMPARE(canvas_->selectedIndices(), QList<int>({1, 0}));
    QCOMPARE(document_.elements[canvas_->selectedIndex()].id, primaryId);
    document_.elements.removeAt(1);
    canvas_->syncDocument(document_);
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0}));
    QCOMPARE(document_.elements[canvas_->selectedIndex()].id, primaryId);
    document_.elements.clear();
    canvas_->syncDocument(document_);
    QVERIFY(canvas_->selectedIndices().isEmpty());
}

void CanvasTests::escapeCancelsGroupMoveAndRubberBand()
{
    canvas_->setSelectedIndices({0, 1});
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementsMoved);
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::AltModifier, viewPoint({44, 52}));
    QTest::mouseMove(canvas_->viewport(), viewPoint({84, 92}));
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::AltModifier, viewPoint({84, 92}));
    QVERIFY(moved.isEmpty());
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({200, 200}));
    QVERIFY(canvas_->selectedIndices().isEmpty());
    QTest::mouseMove(canvas_->viewport(), viewPoint({250, 240}));
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({250, 240}));
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
}

void CanvasTests::gridToggleRepaintsPaperAndWorkspaceAtEveryZoom_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::newRow("minimum-zoom") << qreal(0.1);
    QTest::newRow("quarter-size") << qreal(0.25);
    QTest::newRow("actual-size") << qreal(1.0);
}

void CanvasTests::gridToggleRepaintsPaperAndWorkspaceAtEveryZoom()
{
    QFETCH(qreal, scale);
    document_.elements.clear();
    const auto rendered = Designer::renderPreview(document_);
    QVERIFY(rendered);
    canvas_->setDocument(document_, *rendered);
    canvas_->setTransform(QTransform::fromScale(scale, scale));
    canvas_->centerOn(QPointF(document_.width / 2.0, document_.height / 2.0));
    QApplication::processEvents();
    const QTransform transform = canvas_->transform();
    PaintCounter paints;
    canvas_->viewport()->installEventFilter(&paints);
    const int beforeHide = paints.count;
    canvas_->setGridVisible(false);
    QTRY_VERIFY(paints.count > beforeHide);
    const QImage hidden = canvas_->viewport()->grab().toImage();
    QVERIFY(!hidden.isNull());
    const qreal dpr = hidden.devicePixelRatio();
    const QRect viewportPage = canvas_->mapFromScene(QRectF(0, 0, document_.width, document_.height)).boundingRect();
    const QRect paper(qRound(viewportPage.x() * dpr), qRound(viewportPage.y() * dpr),
                      qRound(viewportPage.width() * dpr), qRound(viewportPage.height() * dpr));
    const int inset = qCeil(2 * dpr);
    const QRect interior = paper.adjusted(inset, inset, -inset, -inset).intersected(hidden.rect());
    QVERIFY(!interior.isEmpty());
    const int beforeShow = paints.count;
    canvas_->setGridVisible(true);
    QTRY_VERIFY(paints.count > beforeShow);
    const QImage visible = canvas_->viewport()->grab().toImage();
    QCOMPARE(visible.size(), hidden.size());
    QVERIFY(visible.copy(interior) != hidden.copy(interior));
    int paperGridPixels = 0;
    for (int y = interior.top(); y <= interior.bottom(); ++y) {
        for (int x = interior.left(); x <= interior.right(); ++x) {
            QCOMPARE(hidden.pixelColor(x, y), QColor(Qt::white));
            if (qGray(hidden.pixel(x, y)) - qGray(visible.pixel(x, y)) >= 8)
                ++paperGridPixels;
        }
    }
    // Even at 10%, visible lines remain separated by clear paper.
    QVERIFY(paperGridPixels > 0);
    QVERIFY(paperGridPixels < interior.width() * interior.height() / 3);
    int clearlyChangedPixels = 0;
    int workspacePixels = 0;
    for (int y = 0; y < visible.height(); ++y) {
        for (int x = 0; x < visible.width(); ++x) {
            if (paper.contains(x, y)) continue;
            ++workspacePixels;
            if (qGray(hidden.pixel(x, y)) - qGray(visible.pixel(x, y)) >= 8)
                ++clearlyChangedPixels;
        }
    }
    // The surrounding workspace keeps its subtle screen-space texture.
    QVERIFY2(clearlyChangedPixels > workspacePixels / 1000,
             qPrintable(QStringLiteral("Only %1 clearly changed pixels at scale %2")
                            .arg(clearlyChangedPixels).arg(scale)));
    QVERIFY(clearlyChangedPixels < workspacePixels / 4);
    const int beforeHideAgain = paints.count;
    canvas_->setGridVisible(false);
    QTRY_VERIFY(paints.count > beforeHideAgain);
    QCOMPARE(canvas_->viewport()->grab().toImage(), hidden);
    QCOMPARE(canvas_->transform(), transform);
}

void CanvasTests::paperGridTracksPrinterDotsThroughZoomAndPan()
{
    document_.elements.clear();
    const auto rendered = Designer::renderPreview(document_);
    QVERIFY(rendered);
    canvas_->setDocument(document_, *rendered);
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 17;
    settings.gridVisible = true;
    settings.rulersVisible = false;
    canvas_->setLayoutSettings(settings);
    const QString source = Designer::toZpl(document_);
    for (const qreal scale : {qreal(1), qreal(2)}) {
        canvas_->setTransform(QTransform::fromScale(scale, scale));
        canvas_->centerOn(QPointF(180, 140));
        for (const int pan : {0, 13}) {
            canvas_->horizontalScrollBar()->setValue(canvas_->horizontalScrollBar()->value() + pan);
            canvas_->verticalScrollBar()->setValue(canvas_->verticalScrollBar()->value() + pan);
            const QImage image = canvas_->viewport()->grab().toImage();
            const auto inkNear = [&](QPointF position) {
                const QPoint viewportPoint = canvas_->mapFromScene(position);
                const qreal dpr = image.devicePixelRatio();
                const QPoint pixel(qRound(viewportPoint.x() * dpr), qRound(viewportPoint.y() * dpr));
                const int radius = qCeil(dpr);
                const QRect sample = QRect(pixel - QPoint(radius, radius), QSize(radius * 2 + 1, radius * 2 + 1))
                                         .intersected(image.rect());
                for (int y = sample.top(); y <= sample.bottom(); ++y)
                    for (int x = sample.left(); x <= sample.right(); ++x)
                        if (qGray(image.pixel(x, y)) < 247) return true;
                return false;
            };
            QVERIFY(inkNear({170, 144})); // Vertical line: 10 * 17 dots.
            QVERIFY(inkNear({178, 153})); // Horizontal line: 9 * 17 dots.
            QVERIFY(!inkNear({178, 144})); // Clear cell interior.
        }
    }
    QCOMPARE(Designer::toZpl(document_), source);
    QCOMPARE(Designer::renderPreview(document_)->image, rendered->image);
}

void CanvasTests::paperGridPreservesPrintedBlackPixels()
{
    document_.elements.clear();
    Preview black;
    black.image = QImage(document_.width, document_.height, QImage::Format_ARGB32);
    black.image.fill(Qt::black);
    canvas_->setDocument(document_, black);
    canvas_->setGridVisible(false);
    const QImage hidden = canvas_->viewport()->grab().toImage();
    canvas_->setGridVisible(true);
    const QImage visible = canvas_->viewport()->grab().toImage();
    const QRect interior = canvas_->mapFromScene(QRectF(20, 20, 100, 100)).boundingRect();
    const qreal dpr = visible.devicePixelRatio();
    const QRect pixels(qRound(interior.x() * dpr), qRound(interior.y() * dpr),
                       qRound(interior.width() * dpr), qRound(interior.height() * dpr));
    QCOMPARE(visible.copy(pixels), hidden.copy(pixels));
    QCOMPARE(visible.pixelColor(pixels.center()), QColor(Qt::black));
}

void CanvasTests::gridVisibilityDoesNotChangeSnapping()
{
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    canvas_->setSnapEnabled(true);
    canvas_->setGridVisible(false);
    drag({44, 52}, {61, 69});
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(56, 64));
    canvas_->setGridVisible(true);
    drag({44, 52}, {61, 69});
    QCOMPARE(moved.size(), 2);
    QCOMPARE(moved.last().at(1).toPoint(), moved.front().at(1).toPoint());
}

void CanvasTests::layoutAidsLeaveRasterAndZplUnchanged()
{
    document_.elements.clear();
    const auto rendered = Designer::renderPreview(document_);
    QVERIFY(rendered);
    canvas_->setDocument(document_, *rendered);
    const QString source = Designer::toZpl(document_);
    auto settings = canvas_->layoutSettings();
    settings.gridVisible = settings.guidesVisible = settings.marginsVisible = false;
    canvas_->setLayoutSettings(settings);
    QApplication::processEvents();
    const QImage cleanCanvas = canvas_->viewport()->grab().toImage();
    settings.gridStep = 17;
    settings.margin = 23;
    settings.verticalGuides = {173};
    settings.horizontalGuides = {139};
    settings.gridVisible = settings.guidesVisible = settings.marginsVisible = true;
    canvas_->setLayoutSettings(settings);
    QApplication::processEvents();
    QVERIFY(canvas_->viewport()->grab().toImage() != cleanCanvas);
    QCOMPARE(Designer::toZpl(document_), source);
    settings.gridVisible = settings.guidesVisible = settings.marginsVisible = false;
    canvas_->setLayoutSettings(settings);
    QApplication::processEvents();
    // Hiding every overlay must reveal exactly the original QtZpl raster.
    QCOMPARE(canvas_->viewport()->grab().toImage(), cleanCanvas);
    QCOMPARE(Designer::renderPreview(document_)->image, rendered->image);
}

void CanvasTests::configuredGridUsesPrinterDotsAtZoom()
{
    // This test isolates the configured grid from neighboring-object snaps.
    document_.elements.removeLast();
    preview_.bounds.removeLast();
    canvas_->setDocument(document_, preview_);
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 17;
    settings.gridVisible = false;
    canvas_->setLayoutSettings(settings);
    canvas_->setTransform(QTransform::fromScale(2, 2));
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    drag({44, 52}, {71, 71});
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(68, 68));
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, corner + QPointF(21, 15));
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(102, 68));
}

void CanvasTests::rulersShowFinalLabelDimensions_data()
{
    QTest::addColumn<QSize>("dots");
    QTest::addColumn<bool>("millimeters");
    QTest::addColumn<QString>("localeName");
    QTest::addColumn<int>("zoomPercent");
    QTest::addColumn<QString>("widthText");
    QTest::addColumn<QString>("heightText");
    QTest::newRow("millimeters-minimum-zoom")
        << QSize(1180, 591) << true << QStringLiteral("en_US") << 10
        << QStringLiteral("99.9") << QStringLiteral("50");
    QTest::newRow("millimeters-fit")
        << QSize(1180, 591) << true << QStringLiteral("en_US") << 0
        << QStringLiteral("99.9") << QStringLiteral("50");
    QTest::newRow("russian-millimeters-fit")
        << QSize(1180, 591) << true << QStringLiteral("ru_RU") << 0
        << QStringLiteral("99,9") << QStringLiteral("50");
    QTest::newRow("russian-portrait-fit")
        << QSize(591, 1180) << true << QStringLiteral("ru_RU") << 0
        << QStringLiteral("50") << QStringLiteral("99,9");
    // The nearby 100 / 50 major marks must not overlap the final dimensions.
    QTest::newRow("beside-100-millimeter-mark")
        << QSize(1182, 1182) << true << QStringLiteral("en_US") << 10
        << QStringLiteral("100.1") << QStringLiteral("100.1");
    QTest::newRow("beside-50-millimeter-mark")
        << QSize(592, 592) << true << QStringLiteral("en_US") << 25
        << QStringLiteral("50.1") << QStringLiteral("50.1");
    QTest::newRow("long-dot-dimensions")
        << QSize(4096, 3071) << false << QStringLiteral("en_US") << 10
        << QLocale(QStringLiteral("en_US")).toString(4096)
        << QLocale(QStringLiteral("en_US")).toString(3071);
    QTest::newRow("labels-larger-than-paper")
        << QSize(1, 1) << true << QStringLiteral("en_US") << 10
        << QStringLiteral("0.08") << QStringLiteral("0.08");
}

void CanvasTests::rulersShowFinalLabelDimensions()
{
    QFETCH(QSize, dots);
    QFETCH(bool, millimeters);
    QFETCH(QString, localeName);
    QFETCH(int, zoomPercent);
    QFETCH(QString, widthText);
    QFETCH(QString, heightText);
    document_.elements.clear();
    document_.width = dots.width();
    document_.height = dots.height();
    document_.dpi = 300;
    canvas_->syncDocument(document_);
    canvas_->setLocale(QLocale(localeName));
    auto settings = canvas_->layoutSettings();
    settings.gridVisible = false;
    settings.unit = millimeters ? Designer::CanvasUnit::Millimeters : Designer::CanvasUnit::Dots;
    canvas_->setLayoutSettings(settings);
    if (zoomPercent == 0)
        canvas_->fitLabel();
    else
        canvas_->setZoomPercent(zoomPercent);
    QApplication::processEvents();

    for (const bool horizontal : {true, false}) {
        auto* ruler = canvas_->findChild<QWidget*>(horizontal ? QStringLiteral("horizontalRuler")
                                                            : QStringLiteral("verticalRuler"));
        QVERIFY(ruler);
        QVERIFY(ruler->isVisible());
        const QString text = horizontal ? widthText : heightText;
        QFont font = canvas_->font();
        font.setPixelSize(9);
        const QFontMetrics metrics(font);
        const int textWidth = metrics.horizontalAdvance(text);
        const int textHeight = metrics.height();
        QVERIFY(ruler->width() >= textWidth + (horizontal ? 0 : 3));
        QVERIFY(ruler->height() >= textHeight);
        const QRect textRect = horizontal
            ? QRect(ruler->width() - textWidth, 0, textWidth, ruler->height() - 2)
            : QRect(0, ruler->height() - textHeight, ruler->width() - 3, textHeight);

        const QPixmap actual = ruler->grab();
        QPixmap expected(actual.size());
        expected.setDevicePixelRatio(actual.devicePixelRatio());
        expected.fill(Designer::designerColor(Designer::DesignerColor::Canvas));
        {
            QPainter painter(&expected);
            painter.setFont(font);
            painter.setPen(Designer::designerColor(Designer::DesignerColor::Muted));
            painter.drawText(textRect, horizontal ? Qt::AlignTop : Qt::AlignRight, text);
            if (horizontal)
                painter.drawLine(ruler->width() - 1, ruler->height() - 4,
                                 ruler->width() - 1, ruler->height() - 1);
            else
                painter.drawLine(ruler->width() - 3, ruler->height() - 1,
                                 ruler->width() - 1, ruler->height() - 1);
        }
        // Check actual painted glyphs and the gap before them. The latter also
        // detects a regular ruler mark painted through the final dimension.
        const QRect checked = (horizontal ? textRect.adjusted(-6, 0, 0, 0)
                                          : textRect.adjusted(0, -6, 3, 0)).intersected(ruler->rect());
        const qreal dpr = actual.devicePixelRatio();
        const QRect pixels(qRound(checked.x() * dpr), qRound(checked.y() * dpr),
                           qRound(checked.width() * dpr), qRound(checked.height() * dpr));
        QCOMPARE(actual.toImage().copy(pixels).convertToFormat(QImage::Format_ARGB32),
                 expected.toImage().copy(pixels).convertToFormat(QImage::Format_ARGB32));
    }
}

void CanvasTests::rulerDragsCreateGuides_data()
{
    QTest::addColumn<bool>("horizontal");
    QTest::newRow("horizontal") << true;
    QTest::newRow("vertical") << false;
}

void CanvasTests::rulerDragsCreateGuides()
{
    QFETCH(bool, horizontal);
    auto* ruler = canvas_->findChild<QWidget*>(horizontal ? QStringLiteral("horizontalRuler")
                                                        : QStringLiteral("verticalRuler"));
    QVERIFY(ruler);
    QVERIFY(ruler->isVisible());
    const QRect rulerInViewport(ruler->mapTo(canvas_->viewport(), QPoint{}), ruler->size());
    const QRect paper = canvas_->mapFromScene(QRectF(0, 0, document_.width, document_.height)).boundingRect();
    QVERIFY(!rulerInViewport.intersects(paper));
    QSignalSpy edits(canvas_.get(), &LabelCanvas::layoutSettingsEdited);
    const QPoint end = ruler->mapFromGlobal(canvas_->viewport()->mapToGlobal(viewPoint({173, 139})));
    QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, ruler->rect().center());
    QTest::mouseEvent(QTest::MouseMove, ruler, Qt::NoButton, Qt::NoModifier, end);
    QVERIFY(edits.isEmpty());
    QVERIFY(canvas_->layoutSettings().horizontalGuides.isEmpty());
    QVERIFY(canvas_->layoutSettings().verticalGuides.isEmpty());
    QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(edits.size(), 1);
    const auto committed = qvariant_cast<Designer::CanvasLayoutSettings>(edits.front().front());
    QCOMPARE(horizontal ? committed.horizontalGuides : committed.verticalGuides,
             QList<int>({horizontal ? 139 : 173}));
    QCOMPARE(canvas_->layoutSettings(), committed);

    QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, ruler->rect().center());
    QTest::mouseEvent(QTest::MouseMove, ruler, Qt::NoButton, Qt::NoModifier, end + QPoint(30, 30));
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, end + QPoint(30, 30));
    QCOMPARE(edits.size(), 1);
    QCOMPARE(canvas_->layoutSettings(), committed);
}

void CanvasTests::guidesMoveDeleteAndCancel()
{
    auto settings = canvas_->layoutSettings();
    settings.verticalGuides = {173};
    canvas_->setLayoutSettings(settings);
    QSignalSpy edits(canvas_.get(), &LabelCanvas::layoutSettingsEdited);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    drag({173, 180}, {197, 180});
    QCOMPARE(edits.size(), 1);
    QCOMPARE(canvas_->layoutSettings().verticalGuides, QList<int>({197}));
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({197, 180}));
    QTest::mouseMove(canvas_->viewport(), viewPoint({237, 180}));
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({237, 180}));
    QCOMPARE(edits.size(), 1);
    QCOMPARE(canvas_->layoutSettings().verticalGuides, QList<int>({197}));
    drag({197, 180}, {-20, 180});
    QCOMPARE(edits.size(), 2);
    QVERIFY(canvas_->layoutSettings().verticalGuides.isEmpty());
    QVERIFY(moved.isEmpty());
}

void CanvasTests::rulerDragTracksZoomAndPan()
{
    canvas_->setTransform(QTransform::fromScale(2, 2));
    canvas_->centerOn(QPointF(200, 150));
    canvas_->horizontalScrollBar()->setValue(canvas_->horizontalScrollBar()->value() + 37);
    QApplication::processEvents();
    auto* ruler = canvas_->findChild<QWidget*>(QStringLiteral("verticalRuler"));
    QVERIFY(ruler);
    const QPoint end = ruler->mapFromGlobal(canvas_->viewport()->mapToGlobal(viewPoint({213, 170})));
    QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, ruler->rect().center());
    QTest::mouseEvent(QTest::MouseMove, ruler, Qt::NoButton, Qt::NoModifier, end);
    QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(canvas_->layoutSettings().verticalGuides, QList<int>({213}));
    auto settings = canvas_->layoutSettings();
    settings.rulersVisible = false;
    canvas_->setLayoutSettings(settings);
    QVERIFY(!ruler->isVisible());
    QCOMPARE(canvas_->transform().m11(), 2.0);
    QCOMPARE(canvas_->viewport()->geometry().topLeft(), QPoint(0, 0));
}

void CanvasTests::guideAndMarginSnappingUsesScreenTolerance()
{
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 1;
    settings.verticalGuides = {101};
    settings.horizontalGuides = {91};
    canvas_->setLayoutSettings(settings);
    canvas_->setTransform(QTransform::fromScale(2, 2));
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    drag({44, 52}, {103, 96}); // next origin (99, 92), within 6 screen px.
    QCOMPARE(moved.last().at(1).toPoint(), QPoint(101, 91));
    drag({44, 52}, {101, 96}); // x = 97 is 8 screen px from the guide.
    QCOMPARE(moved.last().at(1).toPoint(), QPoint(97, 91));
    drag({44, 52}, {103, 96}, Qt::AltModifier);
    QCOMPARE(moved.last().at(1).toPoint(), QPoint(99, 92));
    settings.guidesVisible = false;
    canvas_->setLayoutSettings(settings);
    drag({44, 52}, {103, 96});
    QCOMPARE(moved.last().at(1).toPoint(), QPoint(99, 92));

    settings.marginsVisible = true;
    settings.margin = 31;
    canvas_->setLayoutSettings(settings);
    drag({44, 52}, {36, 38}); // next origin (32, 34).
    QCOMPARE(moved.last().at(1).toPoint(), QPoint(31, 31));
    canvas_->setSelectedIndex(0);
    QTest::keyClick(canvas_.get(), Qt::Key_Right);
    QCOMPARE(moved.last().at(1).toPoint(), QPoint(41, 48));
    QTest::keyClick(canvas_.get(), Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(moved.last().at(1).toPoint(), QPoint(50, 48));
}

void CanvasTests::resizingSnapsToGuides()
{
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 1;
    settings.verticalGuides = {141};
    settings.horizontalGuides = {119};
    canvas_->setLayoutSettings(settings);
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, {139, 117});
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.last().at(1).toSize(), QSize(101, 71));
    drag(corner, {139, 117}, Qt::AltModifier);
    QCOMPARE(resized.last().at(1).toSize(), QSize(99, 69));
}

void CanvasTests::layoutSettingsAreBounded()
{
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 0;
    settings.margin = 99999;
    settings.horizontalGuides = {-1, 4097, 42, 42, 7};
    for (int value = 0; value < 500; ++value) settings.verticalGuides.append(value);
    canvas_->setLayoutSettings(settings);
    QCOMPARE(canvas_->layoutSettings().gridStep, 1);
    QCOMPARE(canvas_->layoutSettings().margin, 2048);
    QCOMPARE(canvas_->layoutSettings().horizontalGuides, QList<int>({7, 42}));
    QCOMPARE(canvas_->layoutSettings().verticalGuides.size(), 128);
    QVERIFY(!canvas_->viewport()->grab().isNull());
}

void CanvasTests::fitViewportSettlesAcrossResizeAndLayoutChanges()
{
    document_.width = 812;
    document_.height = 406;
    canvas_->syncDocument(document_);
    ResizeCounter resizes;
    canvas_->viewport()->installEventFilter(&resizes);
    canvas_->fitLabel();
    QCOMPARE(canvas_->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QCOMPARE(canvas_->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    const auto verifySettled = [&] {
        QTest::qWait(10);
        const int settled = resizes.count;
        const QTransform fitted = canvas_->transform();
        QTest::qWait(20);
        QCOMPARE(resizes.count, settled);
        QCOMPARE(canvas_->transform(), fitted);
        const QRect paper = canvas_->mapFromScene(QRectF(0, 0, document_.width, document_.height)).boundingRect();
        QVERIFY(canvas_->viewport()->rect().contains(paper));
    };
    // These dimensions straddle scrollbar thresholds of the padded scene.
    for (const QSize size : {QSize(958, 508), QSize(958, 482), QSize(760, 410), QSize(900, 480)}) {
        const int before = resizes.count;
        canvas_->resize(size);
        verifySettled();
        QVERIFY(resizes.count - before < 8);
        for (const bool rulersVisible : {false, true}) {
            auto settings = canvas_->layoutSettings();
            settings.rulersVisible = rulersVisible;
            canvas_->setLayoutSettings(settings);
            verifySettled();
            const QTransform fitted = canvas_->transform();
            settings.gridVisible = !settings.gridVisible;
            canvas_->setLayoutSettings(settings);
            verifySettled();
            QCOMPARE(canvas_->transform(), fitted);
        }
    }
}

void CanvasTests::fitModeKeepsPanAndRestoresManualZoomScrollbars()
{
    document_.width = 812;
    document_.height = 406;
    canvas_->syncDocument(document_);
    canvas_->resize(958, 508);
    canvas_->fitLabel();
    QTest::qWait(10);
    const QTransform fitted = canvas_->transform();
    const int beforePan = canvas_->verticalScrollBar()->value();
    const QPoint start = canvas_->viewport()->rect().center();
    QTest::mousePress(canvas_->viewport(), Qt::MiddleButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas_->viewport(), start - QPoint(0, 20));
    QTest::mouseRelease(canvas_->viewport(), Qt::MiddleButton, Qt::NoModifier, start - QPoint(0, 20));
    QVERIFY(canvas_->verticalScrollBar()->value() > beforePan);
    QCOMPARE(canvas_->transform(), fitted);
    QCOMPARE(canvas_->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

    canvas_->actualSize();
    QCOMPARE(canvas_->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(canvas_->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(canvas_->transform().m11(), 1.0);
    canvas_->resize(720, 430);
    QTest::qWait(10);
    QCOMPARE(canvas_->transform().m11(), 1.0);
    canvas_->fitLabel();
    QCOMPARE(canvas_->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QCOMPARE(canvas_->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    canvas_->zoomIn();
    QCOMPARE(canvas_->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(canvas_->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
}

void CanvasTests::invalidDataMatrixMarkerIsEditorOnlyAndFollowsIdentity()
{
    document_.elements = {Designer::makeElement(ElementType::DataMatrix, {40, 48}),
                          Designer::makeElement(ElementType::DataMatrix, {220, 100})};
    Preview editor;
    editor.image = QImage(document_.width, document_.height, QImage::Format_ARGB32);
    editor.image.fill(Qt::white);
    editor.bounds = {QRect(40, 48, 100, 100), QRect(220, 100, 100, 100)};
    canvas_->setGridVisible(false);
    canvas_->setDocument(document_, editor);
    const QImage cleanCanvas = canvas_->viewport()->grab().toImage();
    editor.elementErrors.insert(0, QStringLiteral("Некорректные данные GS1"));
    canvas_->setDocument(document_, editor);
    const QImage markedCanvas = canvas_->viewport()->grab().toImage();
    QVERIFY(markedCanvas != cleanCanvas);

    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    drag({80, 90}, {90, 101}, Qt::AltModifier);
    QCOMPARE(canvas_->selectedIndex(), 0);
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(50, 59));
    canvas_->setSelectedIndex(-1);
    // While a new raster is pending, error indices follow original identities.
    document_.elements.move(0, 1);
    canvas_->syncDocument(document_);
    QCOMPARE(canvas_->viewport()->grab().toImage(), markedCanvas);
    document_.elements.removeAt(1);
    canvas_->syncDocument(document_);
    // Removing the invalid element reveals the unchanged blank raster.
    QCOMPARE(canvas_->viewport()->grab().toImage(), cleanCanvas);
}

void CanvasTests::persistentGroupsSelectAndToggleAtomically()
{
    const QUuid group = QUuid::createUuid();
    document_.elements[0].groupId = document_.elements[1].groupId = group;
    canvas_->setDocument(document_, preview_);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QCOMPARE(canvas_->selectedIndices(), QList<int>({1, 0}));
    QCOMPARE(canvas_->selectedIndex(), 0);
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::ControlModifier, viewPoint({70, 70}));
    QVERIFY(canvas_->selectedIndices().isEmpty());
    QTest::mouseClick(canvas_->viewport(), Qt::LeftButton, Qt::ShiftModifier, viewPoint({70, 70}));
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementsMoved);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    drag({44, 52}, {64, 72}, Qt::AltModifier);
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(20, 20));
    QVERIFY(resized.isEmpty());
    QSignalSpy removed(canvas_.get(), &LabelCanvas::deleteRequested);
    QTest::keyClick(canvas_.get(), Qt::Key_Delete);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(canvas_->selectedIndices().size(), 2);
}

void CanvasTests::persistentGroupsExpandRubberBandAndFollowModel()
{
    document_.elements[1].x = 240;
    document_.elements[1].y = 160;
    document_.elements[0].groupId = document_.elements[1].groupId = QUuid::createUuid();
    preview_.bounds[1].moveTopLeft({240, 160});
    canvas_->setDocument(document_, preview_);
    drag({20, 20}, {60, 60});
    QCOMPARE(canvas_->selectedIndices(), QList<int>({1, 0}));
    canvas_->setSelectedIndices({1});
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
    document_.elements[0].groupId = {};
    document_.elements[1].groupId = {};
    canvas_->syncDocument(document_);
    canvas_->setSelectedIndices({1});
    QCOMPARE(canvas_->selectedIndices(), QList<int>({1}));
    document_.elements[0].groupId = document_.elements[1].groupId = QUuid::createUuid();
    canvas_->syncDocument(document_);
    QCOMPARE(canvas_->selectedIndices(), QList<int>({0, 1}));
}

void CanvasTests::imageResizeRespectsAspectRatioSetting()
{
    document_.elements[0] = Designer::makeElement(ElementType::Image, {40, 48});
    document_.elements[0].width = 80;
    document_.elements[0].height = 40;
    document_.elements[0].imageKeepAspectRatio = true;
    preview_.bounds[0] = QRect(40, 48, 80, 40);
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, corner + QPointF(20, 0), Qt::AltModifier);
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.takeFirst().at(1).toSize(), QSize(100, 50));
    document_.elements[0].imageKeepAspectRatio = false;
    canvas_->setDocument(document_, preview_);
    drag(corner, corner + QPointF(20, 0), Qt::AltModifier);
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.front().at(1).toSize(), QSize(100, 40));
}

void CanvasTests::smartSnappingUsesEdgesCentersAndZoom_data()
{
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<QPoint>("origin");
    QTest::addColumn<QPoint>("expected");
    QTest::addColumn<bool>("bypass");
    QTest::newRow("left-edge") << qreal(1) << QPoint(218, 80) << QPoint(220, 80) << false;
    QTest::newRow("right-edge") << qreal(1) << QPoint(138, 80) << QPoint(140, 80) << false;
    QTest::newRow("center") << qreal(1) << QPoint(208, 80) << QPoint(210, 80) << false;
    QTest::newRow("vertical-center") << qreal(1) << QPoint(20, 150) << QPoint(20, 152) << false;
    QTest::newRow("zoom-in-tolerance") << qreal(2) << QPoint(218, 80) << QPoint(220, 80) << false;
    QTest::newRow("zoom-in-outside-tolerance") << qreal(2) << QPoint(216, 80) << QPoint(216, 80) << false;
    QTest::newRow("zoom-out-tolerance") << qreal(0.5) << QPoint(148, 80) << QPoint(140, 80) << false;
    QTest::newRow("alt-bypass") << qreal(1) << QPoint(218, 80) << QPoint(218, 80) << true;
}

void CanvasTests::smartSnappingUsesEdgesCentersAndZoom()
{
    QFETCH(qreal, zoom);
    QFETCH(QPoint, origin);
    QFETCH(QPoint, expected);
    QFETCH(bool, bypass);
    document_.elements[1].x = 220;
    document_.elements[1].y = 160;
    document_.elements[1].width = 60;
    document_.elements[1].height = 40;
    preview_.bounds[1] = QRect(220, 160, 60, 40);
    canvas_->setDocument(document_, preview_);
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 1;
    canvas_->setLayoutSettings(settings);
    canvas_->setTransform(QTransform::fromScale(zoom, zoom));
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    drag({44, 52}, origin + QPoint(4, 4), bypass ? Qt::AltModifier : Qt::NoModifier);
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), expected);
}

void CanvasTests::smartSnappingUsesWholeSelectionBounds()
{
    document_.elements[1].x = 150;
    document_.elements[1].y = 48;
    preview_.bounds[1] = QRect(150, 48, 24, 24);
    auto neighbor = Designer::makeElement(ElementType::Box, {220, 160});
    neighbor.width = 60;
    neighbor.height = 40;
    document_.elements.append(neighbor);
    preview_.bounds.append(QRect(220, 160, 60, 40));
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndices({0, 1});
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 1;
    canvas_->setLayoutSettings(settings);
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementsMoved);
    // The group's right edge is at 174: a delta of 44 is two dots from 220.
    drag({44, 52}, {88, 52});
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.front().at(1).toPoint(), QPoint(46, 0));
}

void CanvasTests::smartResizeSnapsDraggedEdges()
{
    document_.elements[1].x = 220;
    document_.elements[1].y = 160;
    document_.elements[1].width = 60;
    document_.elements[1].height = 40;
    preview_.bounds[1] = QRect(220, 160, 60, 40);
    canvas_->setDocument(document_, preview_);
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 1;
    canvas_->setLayoutSettings(settings);
    canvas_->setSelectedIndex(0);
    QSignalSpy resized(canvas_.get(), &LabelCanvas::elementResized);
    const QPointF corner = QRectF(preview_.bounds[0]).bottomRight();
    drag(corner, {218, 178});
    QCOMPARE(resized.size(), 1);
    QCOMPARE(resized.last().at(1).toSize(), QSize(180, 132));
    drag(corner, {218, 178}, Qt::AltModifier);
    QCOMPARE(resized.last().at(1).toSize(), QSize(178, 130));
}

void CanvasTests::smartGuidesAndDistanceHintsAreTransientOverlays()
{
    document_.elements[1].x = 220;
    document_.elements[1].y = 48;
    document_.elements[1].width = 60;
    document_.elements[1].height = 56;
    const auto rendered = Designer::renderPreview(document_);
    QVERIFY(rendered);
    preview_ = *rendered;
    canvas_->setDocument(document_, preview_);
    canvas_->setSelectedIndex(0);
    auto settings = canvas_->layoutSettings();
    settings.gridStep = 1;
    settings.gridVisible = false;
    canvas_->setLayoutSettings(settings);
    const QString source = Designer::toZpl(document_);
    const QImage clean = canvas_->viewport()->grab().toImage();
    QSignalSpy moved(canvas_.get(), &LabelCanvas::elementMoved);
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({44, 52}));
    QTest::mouseEvent(QTest::MouseMove, canvas_->viewport(), Qt::NoButton, Qt::NoModifier, viewPoint({64, 52}));
    const QImage guided = canvas_->viewport()->grab().toImage();
    // Same geometry with Alt: only the smart alignment/distance overlay differs.
    QTest::mouseEvent(QTest::MouseMove, canvas_->viewport(), Qt::NoButton, Qt::AltModifier, viewPoint({64, 52}));
    const QImage bypassed = canvas_->viewport()->grab().toImage();
    QVERIFY(guided != bypassed);
    const qreal dpr = guided.devicePixelRatio();
    const QRect gapViewport = canvas_->mapFromScene(QRectF(150, 50, 65, 50)).boundingRect();
    const QRect gap(qRound(gapViewport.x() * dpr), qRound(gapViewport.y() * dpr),
                    qRound(gapViewport.width() * dpr), qRound(gapViewport.height() * dpr));
    QVERIFY(guided.copy(gap) != bypassed.copy(gap));

    // Units and printer DPI affect only the transient distance annotation.
    // Keep the same live drag and crop inside the paper, away from its caption.
    QTest::mouseEvent(QTest::MouseMove, canvas_->viewport(), Qt::NoButton, Qt::NoModifier, viewPoint({64, 52}));
    settings.unit = Designer::CanvasUnit::Dots;
    canvas_->setLayoutSettings(settings);
    const QImage dots = canvas_->viewport()->grab().toImage().copy(gap);
    QVERIFY(dots != guided.copy(gap));
    auto highDpi = document_;
    highDpi.dpi = 300;
    canvas_->setDocument(highDpi, preview_);
    QCOMPARE(canvas_->viewport()->grab().toImage().copy(gap), dots);
    settings.unit = Designer::CanvasUnit::Millimeters;
    canvas_->setLayoutSettings(settings);
    QVERIFY(canvas_->viewport()->grab().toImage().copy(gap) != guided.copy(gap));
    canvas_->setDocument(document_, preview_);
    QCOMPARE(canvas_->viewport()->grab().toImage().copy(gap), guided.copy(gap));
    QCOMPARE(Designer::toZpl(document_), source);
    QCOMPARE(Designer::renderPreview(document_)->image, preview_.image);
    QTest::keyClick(canvas_.get(), Qt::Key_Escape);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint({64, 52}));
    QVERIFY(moved.isEmpty());
    QCOMPARE(canvas_->viewport()->grab().toImage(), clean);
}

QTEST_MAIN(CanvasTests)
#include "test_canvas.moc"
