#include "design_document.hpp"
#include "document_session.hpp"
#include "element_clipboard.hpp"
#include "main_window.hpp"
#include "label_canvas.hpp"
#include "selection_properties.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;
class EditingFeaturesTests : public QObject {
    Q_OBJECT
private slots:
    void clipboardKeepsEditableValuesAndRejectsBadData();
    void groupsRoundTripWithoutChangingPrintedPixels();
    void groupsMoveAlignDuplicateAndUndoAsUnits();
    void clipboardActionsAcrossDocumentsAndTextFocus();
    void sharedPropertiesApplyToMatchingTypesAndUndo();
    void imageAspectRatioResize();
    void commonPropertyDraftSurvivesPreviewAndKeepsLargeThickness();
};

void EditingFeaturesTests::clipboardKeepsEditableValuesAndRejectsBadData()
{
    auto text = makeElement(ElementType::Text, {-10, 32});
    text.text = u"Текст ^ ~ _ {{name}}"_s; text.blockWidth = 180; text.blockMaxLines = 4;
    text.groupId = QUuid::createUuid();
    auto image = makeElement(ElementType::Image);
    image.image = QImage(13, 7, QImage::Format_ARGB32); image.image.fill(QColor(20, 90, 70, 123));
    image.imageKeepAspectRatio = false; image.groupId = text.groupId; image.threshold = 87; image.dither = true;
    auto invalidCode = makeElement(ElementType::DataMatrix); invalidCode.text = u"unfinished"_s;
    const auto encoded = encodeElements({text, image, invalidCode});
    const auto decoded = decodeElements(encoded);
    QVERIFY(decoded); QCOMPARE(decoded->size(), 3);
    QCOMPARE(decoded->at(0).text, text.text); QCOMPARE(decoded->at(0).blockWidth, 180);
    QCOMPARE(decoded->at(0).groupId, text.groupId); QCOMPARE(decoded->at(0).x, -10);
    QCOMPARE(decoded->at(1).image, image.image); QVERIFY(!decoded->at(1).imageKeepAspectRatio);
    QCOMPARE(decoded->at(1).threshold, 87); QVERIFY(decoded->at(1).dither);
    QCOMPARE(decoded->at(2).text, invalidCode.text); QVERIFY(decoded->at(2).gs1);
    QVERIFY(!decodeElements("{}")); QVERIFY(!decodeElements(encoded.left(encoded.size() / 2)));
    auto huge = text; huge.width = 9999;
    QVERIFY(!decodeElements(encodeElements({huge})));
}

void EditingFeaturesTests::groupsRoundTripWithoutChangingPrintedPixels()
{
    DesignDocument document;
    auto a = makeElement(ElementType::Box, {30, 20}); a.width = 50; a.height = 30;
    auto b = makeElement(ElementType::Text, {120, 80}); b.text = u"Group"_s;
    document.elements = {a, b};
    const auto before = renderPreview(document); QVERIFY(before);
    DocumentSession session; session.reset(document); QVERIFY(session.groupElements({0, 1}));
    const auto source = toZpl(session.document());
    QVERIFY(source.contains(u"QTZPL_EDITOR_V1:"));
    const auto restored = fromZpl(source); QVERIFY(restored); QVERIFY(restored->visualEditable);
    QCOMPARE(restored->elements[0].groupId, session.document().elements[0].groupId);
    QCOMPARE(restored->elements[1].groupId, restored->elements[0].groupId);
    auto resaved = *restored;
    resaved.originalZpl.clear();
    QCOMPARE(toZpl(resaved), source);
    const auto after = renderPreview(*restored); QVERIFY(after); QCOMPARE(after->image, before->image);
    QTemporaryDir directory;
    QVERIFY(session.save(directory.filePath(u"groups.zpl"_s)));
    DocumentSession reopened; QVERIFY(reopened.open(directory.filePath(u"groups.zpl"_s)));
    QCOMPARE(reopened.document().elements[0].groupId, session.document().elements[0].groupId);
    QVERIFY(reopened.ungroupElements({0}));
    const auto ungroupedSource = toZpl(reopened.document());
    QVERIFY(!ungroupedSource.contains(u"QTZPL_EDITOR_V1:"));
    const auto ungrouped = fromZpl(ungroupedSource); QVERIFY(ungrouped); QVERIFY(ungrouped->visualEditable);
    QVERIFY(ungrouped->elements[0].groupId.isNull());
    reopened.undoStack()->undo(); QVERIFY(!reopened.document().elements[0].groupId.isNull());
    auto malformed = source;
    const auto metadataStart = malformed.indexOf(u"QTZPL_EDITOR_V1:") + QStringView(u"QTZPL_EDITOR_V1:").size();
    malformed.insert(metadataStart, u'!');
    const auto raw = fromZpl(malformed); QVERIFY(raw); QVERIFY(!raw->visualEditable);
    QCOMPARE(toZpl(*raw), malformed);
    auto commented = source;
    commented.insert(commented.indexOf(u"^XZ"), u"^FXKeep this comment^FS\n"_s);
    auto imported = fromZpl(commented); QVERIFY(imported); QVERIFY(imported->visualEditable);
    imported->originalZpl.clear(); imported->elements[1].text = u"Edited"_s;
    const auto edited = toZpl(*imported);
    QVERIFY(edited.contains(u"^FXKeep this comment^FS"));
    QCOMPARE(edited.count(u"QTZPL_EDITOR_V1:"), 1);
}

void EditingFeaturesTests::groupsMoveAlignDuplicateAndUndoAsUnits()
{
    DesignDocument document;
    document.elements = {makeElement(ElementType::Box, {40, 40}), makeElement(ElementType::Box, {100, 90})};
    DocumentSession session; session.reset(document); QVERIFY(session.groupElements({0, 1}));
    const auto group = session.document().elements[0].groupId;
    QVERIFY(session.moveElements({0}, {10, 20})); QCOMPARE(session.document().elements[1].x, 110);
    QVERIFY(!session.alignElements({0}, ElementAlignment::Left, AlignmentReference::Selection));
    QVERIFY(session.alignElements({0}, ElementAlignment::Left, AlignmentReference::Label));
    QCOMPARE(session.document().elements[1].x - session.document().elements[0].x, 60);
    const auto copies = session.duplicateElements({0}); QCOMPARE(copies.size(), 2);
    QVERIFY(session.document().elements[copies[0]].groupId != group);
    QCOMPARE(session.document().elements[copies[0]].groupId, session.document().elements[copies[1]].groupId);
    session.undoStack()->undo(); QCOMPARE(session.document().elements.size(), 2);
    QVERIFY(session.deleteElements({1})); QVERIFY(session.document().elements.isEmpty());
    session.undoStack()->undo(); QCOMPARE(session.document().elements.size(), 2);
}

void EditingFeaturesTests::clipboardActionsAcrossDocumentsAndTextFocus()
{
    MainWindow window; window.show();
    DesignDocument document;
    document.elements = {makeElement(ElementType::Box, {40, 40}), makeElement(ElementType::Box, {140, 90})};
    window.setDocument(document);
    auto* canvas = window.findChild<LabelCanvas*>();
    canvas->setSelectedIndices({0, 1});
    window.findChild<QAction*>(u"groupElements"_s)->trigger();
    const auto originalGroup = window.document().elements[0].groupId;
    canvas->setFocus(); QApplication::processEvents();
    QTest::keyClick(canvas, Qt::Key_C, Qt::ControlModifier);
    QVERIFY(QApplication::clipboard()->mimeData()->hasFormat(QLatin1StringView(elementMimeType)));
    window.setDocument(DesignDocument{});
    QTest::keyClick(canvas, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(window.document().elements.size(), 2); QCOMPARE(canvas->selectedIndices().size(), 2);
    QVERIFY(window.document().elements[0].groupId != originalGroup);
    QCOMPARE(window.document().elements[0].groupId, window.document().elements[1].groupId);
    QCOMPARE(window.document().elements[1].x - window.document().elements[0].x, 100);
    QTest::keyClick(canvas, Qt::Key_X, Qt::ControlModifier); QVERIFY(window.document().elements.isEmpty());
    window.findChild<QAction*>(u"undoAction"_s)->trigger(); QCOMPARE(window.document().elements.size(), 2);
    auto* tabs = window.findChild<QTabWidget*>(u"editorTabs"_s); tabs->setCurrentIndex(1);
    auto* code = window.findChild<QPlainTextEdit*>(u"zplEditor"_s);
    code->setFocus(); code->selectAll();
    QTest::keyClick(code, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), code->toPlainText());
    QVERIFY(!QApplication::clipboard()->mimeData()->hasFormat(QLatin1StringView(elementMimeType)));
    window.setDocument(DesignDocument{});
}

void EditingFeaturesTests::sharedPropertiesApplyToMatchingTypesAndUndo()
{
    MainWindow window; window.show();
    DesignDocument document;
    auto text = makeElement(ElementType::Text); text.fontHeight = 25;
    auto text2 = text; text2.id = QUuid::createUuid(); text2.fontHeight = 40;
    auto code = makeElement(ElementType::QrCode);
    auto box = makeElement(ElementType::Box);
    document.elements = {text, text2, code, box}; window.setDocument(document);
    window.findChild<LabelCanvas*>()->setSelectedIndices({0, 1, 2, 3});
    auto* panel = window.findChild<SelectionProperties*>(); QVERIFY(panel->isVisible());
    auto* font = panel->findChild<QSpinBox*>(u"selection_fontHeight"_s);
    QCOMPARE(font->value(), font->minimum());
    font->setValue(36);
    panel->findChild<QSpinBox*>(u"selection_moduleWidth"_s)->setValue(5);
    panel->findChild<QSpinBox*>(u"selection_thickness"_s)->setValue(7);
    panel->findChild<QPushButton*>(u"applySelectionProperties"_s)->click();
    QCOMPARE(window.document().elements[0].fontHeight, 36); QCOMPARE(window.document().elements[1].fontHeight, 36);
    QCOMPARE(window.document().elements[2].moduleWidth, 5); QCOMPARE(window.document().elements[3].thickness, 7);
    QCOMPARE(window.document().elements[0].moduleWidth, text.moduleWidth);
    window.findChild<QAction*>(u"undoAction"_s)->trigger();
    QCOMPARE(window.document().elements[0].fontHeight, 25); QCOMPARE(window.document().elements[1].fontHeight, 40);
    QCOMPARE(window.document().elements[2].moduleWidth, code.moduleWidth);
    window.setDocument(DesignDocument{});
}

void EditingFeaturesTests::commonPropertyDraftSurvivesPreviewAndKeepsLargeThickness()
{
    DesignDocument document;
    auto shape = makeElement(ElementType::Box); shape.thickness = 700;
    auto text = makeElement(ElementType::Text);
    document.elements = {shape, text};
    SelectionProperties panel;
    panel.setSelection(document, {0, 1});
    auto* thickness = panel.findChild<QSpinBox*>(u"selection_thickness"_s);
    QCOMPARE(thickness->value(), 700);
    auto* font = panel.findChild<QSpinBox*>(u"selection_fontHeight"_s);
    font->setValue(50);
    panel.setSelection(document, {0, 1});
    QCOMPARE(font->value(), 50);
    SelectionPropertyValues changes;
    connect(&panel, &SelectionProperties::changesRequested, &panel, [&](const SelectionPropertyValues& values) { changes = values; });
    panel.findChild<QPushButton*>(u"applySelectionProperties"_s)->click();
    QVERIFY(!changes.thickness.has_value());
    const auto result = applyOperation(document, UpdateSelectionProperties{{shape.id, text.id}, changes});
    QVERIFY(result && *result);
    QCOMPARE(document.elements[0].thickness, 700);
    QCOMPARE(document.elements[1].fontHeight, 50);
}

void EditingFeaturesTests::imageAspectRatioResize()
{
    auto element = makeElement(ElementType::Image); element.width = 200; element.height = 100;
    auto resized = resizedElement(element, {300, 100}, {200, 100});
    QCOMPARE(resized.width, 300); QCOMPARE(resized.height, 150);
    element.imageKeepAspectRatio = false;
    resized = resizedElement(element, {300, 100}, {200, 100});
    QCOMPARE(resized.width, 300); QCOMPARE(resized.height, 100);
    element.image = QImage(16, 8, QImage::Format_RGB32); element.image.fill(Qt::black);
    DesignDocument document; document.elements = {element};
    const auto reopened = fromZpl(toZpl(document)); QVERIFY(reopened); QVERIFY(reopened->visualEditable);
    QVERIFY(!reopened->elements[0].imageKeepAspectRatio);
}

QTEST_MAIN(EditingFeaturesTests)
#include "test_editing_features.moc"
