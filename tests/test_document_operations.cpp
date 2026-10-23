#include "document_operations.hpp"

#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

class DocumentOperationTests final : public QObject {
    Q_OBJECT
private slots:
    void barcodeChangePreservesPayloadAndSource();
    void barcodeConstraints();
    void rejectsIneligibleBarcodeChanges();
    void propertiesAreAtomicAndKeepIntermediateText();
    void textAndPropertiesNoopPreserveImportedSource();
    void lockedAndMissingTargetsAreRejected();
    void selectionPropertiesAreAtomicAndTyped();
    void textOperationClearsLiteralFlagOnlyWhenPayloadChanges();
    void resizeHonorsDefaultOrientationAndSource();
    void resizeRejectsUnavailableGeometry();
    void resizeNoopAndFixedMaxiCode();
    void contentUpdatePreservesCommonAndInactiveValues();
    void tableContentKeepsEditablePlaceholdersAndDerivesSize();
    void layerOperationsHonorLockAndKeepStableOrder();
    void labelAndAddFailuresAreAtomic();
    void unchangedImportedPropertiesDoNotBlockEditing();
};

void DocumentOperationTests::barcodeChangePreservesPayloadAndSource()
{
    DesignDocument document;
    auto code = makeElement(ElementType::Code128, {12, 34});
    code.name = u"Batch"_s;
    code.text = u"Unfinished Unicode код"_s;
    code.literalText = true;
    code.groupId = QUuid::createUuid();
    code.editorVisible = false;
    code.printable = false;
    code.sourcePrefix = u"^FXbefore^FS"_s;
    code.sourceZpl = u"^FO12,34^BCN,80^FDold^FS"_s;
    code.sourceState = u"printer state"_s;
    code.sourceSignature = u"old signature"_s;
    // Inactive values remain available if a later type switch needs them.
    code.pdf417Columns = 8;
    document.elements = {code};
    document.originalZpl = u"original source"_s;
    document.sourcePreamble = u"^XA^FXpreamble^FS"_s;
    document.sourceEpilogue = u"^FXend^FS^XZ"_s;
    const auto changed = applyOperation(document, ChangeBarcodeType{code.id, ElementType::Code93});
    QVERIFY(changed && *changed);
    const auto& after = document.elements.front();
    QCOMPARE(after.type, ElementType::Code93);
    QCOMPARE(after.id, code.id);
    QCOMPARE(after.groupId, code.groupId);
    QCOMPARE(after.name, code.name);
    QCOMPARE(after.text, code.text);
    QCOMPARE(after.literalText, code.literalText);
    QCOMPARE(after.editorVisible, code.editorVisible);
    QCOMPARE(after.printable, code.printable);
    QCOMPARE(QPoint(after.x, after.y), QPoint(12, 34));
    QCOMPARE(after.sourcePrefix, code.sourcePrefix);
    QCOMPARE(after.sourceZpl, code.sourceZpl);
    QCOMPARE(after.sourceState, code.sourceState);
    QCOMPARE(after.sourceSignature, code.sourceSignature);
    QCOMPARE(after.pdf417Columns, 8);
    QVERIFY(document.originalZpl.isEmpty());
    QCOMPARE(document.sourcePreamble, u"^XA^FXpreamble^FS"_s);
    QCOMPARE(document.sourceEpilogue, u"^FXend^FS^XZ"_s);
}

void DocumentOperationTests::barcodeConstraints()
{
    DesignDocument document;
    for (auto type : {ElementType::Code39, ElementType::Codabar}) {
        auto code = makeElement(ElementType::Code128);
        code.interpretationAbove = true;
        document.elements = {code};
        const auto result = applyOperation(document, ChangeBarcodeType{code.id, type});
        QVERIFY(result && *result);
        const auto& after = document.elements.front();
        QCOMPARE(after.name, elementTypeName(type));
        QVERIFY(!after.humanReadable);
        QVERIFY(!after.interpretationAbove);
    }
    auto matrix = makeElement(ElementType::DataMatrix);
    matrix.orientation = u'R';
    matrix.useDefaultOrientation = true;
    QVERIFY(matrix.gs1);
    document.elements = {matrix};
    QVERIFY(applyOperation(document, ChangeBarcodeType{matrix.id, ElementType::QrCode}).value_or(false));
    QVERIFY(!document.elements.front().gs1);
    QVERIFY(applyOperation(document, ChangeBarcodeType{matrix.id, ElementType::DataMatrix}).value_or(false));
    QVERIFY(!document.elements.front().gs1);
    QVERIFY(applyOperation(document, ChangeBarcodeType{matrix.id, ElementType::MaxiCode}).value_or(false));
    const auto& maxi = document.elements.front();
    QCOMPARE(maxi.orientation, QChar(u'N'));
    QVERIFY(!maxi.useDefaultOrientation);
    QCOMPARE(QSize(maxi.width, maxi.height), QSize(201, 195));
    QCOMPARE(maxi.text, matrix.text);

    // Codabar start/stop are ^BK k and l, edited as their own A-D values.
    const auto codabar = makeElement(ElementType::Codabar);
    document.elements = {codabar};
    ElementProperties invalid; invalid.codabarStart = u'E';
    QVERIFY(!applyOperation(document, UpdateElementProperties{codabar.id, invalid}).has_value());
    ElementProperties start; start.codabarStart = u'C';
    QVERIFY(applyOperation(document, UpdateElementProperties{codabar.id, start}).value_or(false));
    QCOMPARE(document.elements.front().codabarStart, QChar(u'C'));
    auto options = document.elements.front(); options.codabarStop = u'D';
    QVERIFY(applyOperation(document, UpdateElementContent{codabar.id, options}).value_or(false));
    QCOMPARE(document.elements.front().codabarStop, QChar(u'D'));
    QCOMPARE(document.elements.front().text, codabar.text);
    options.codabarStop = u'X';
    QVERIFY(!applyOperation(document, UpdateElementContent{codabar.id, options}).has_value());
}

void DocumentOperationTests::rejectsIneligibleBarcodeChanges()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text);
    auto code = makeElement(ElementType::Code128);
    document.elements = {text, code};
    document.originalZpl = u"untouched"_s;
    QVERIFY(!applyOperation(document, ChangeBarcodeType{text.id, ElementType::Code128}));
    QVERIFY(!applyOperation(document, ChangeBarcodeType{code.id, ElementType::QrCode}));
    QVERIFY(!applyOperation(document, ChangeBarcodeType{code.id, static_cast<ElementType>(999)}));
    const auto same = applyOperation(document, ChangeBarcodeType{code.id, code.type});
    QVERIFY(same && !*same);
    QCOMPARE(document.elements[0].type, text.type);
    QCOMPARE(document.elements[1].type, code.type);
    QCOMPARE(document.originalZpl, u"untouched"_s);
}

void DocumentOperationTests::propertiesAreAtomicAndKeepIntermediateText()
{
    DesignDocument document;
    auto matrix = makeElement(ElementType::DataMatrix);
    matrix.literalText = true;
    document.elements = {matrix};
    ElementProperties values;
    values.text = u"(01)incomplete"_s;
    values.x = 123;
    values.moduleWidth = 0;
    QVERIFY(!applyOperation(document, UpdateElementProperties{matrix.id, values}));
    QCOMPARE(document.elements.front().text, matrix.text);
    QCOMPARE(document.elements.front().x, matrix.x);
    QVERIFY(document.elements.front().literalText);
    values.moduleWidth = 4;
    const auto result = applyOperation(document, UpdateElementProperties{matrix.id, values});
    QVERIFY(result && *result);
    QCOMPARE(document.elements.front().text, *values.text);
    QCOMPARE(document.elements.front().x, 123);
    QCOMPARE(document.elements.front().moduleWidth, 4);
    QVERIFY(!document.elements.front().literalText);
    QVERIFY(!validateForExport(document));
}

void DocumentOperationTests::textAndPropertiesNoopPreserveImportedSource()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text);
    text.literalText = true;
    document.elements = {text};
    document.originalZpl = u"untouched"_s;
    ElementProperties properties;
    properties.text = text.text;
    properties.x = text.x;
    properties.fontHeight = text.fontHeight;
    const auto result = applyOperation(document, UpdateElementProperties{text.id, properties});
    QVERIFY(result && !*result);
    const auto unchangedText = applyOperation(document, UpdateText{text.id, text.text});
    QVERIFY(unchangedText && !*unchangedText);
    QVERIFY(document.elements.front().literalText);
    QCOMPARE(document.originalZpl, u"untouched"_s);
}

void DocumentOperationTests::lockedAndMissingTargetsAreRejected()
{
    DesignDocument document;
    auto code = makeElement(ElementType::DataMatrix);
    code.locked = true;
    document.elements = {code};
    ElementProperties values;
    values.x = 99;
    QVERIFY(!applyOperation(document, ChangeBarcodeType{code.id, ElementType::QrCode}));
    QVERIFY(!applyOperation(document, UpdateText{code.id, u"new"_s}));
    QVERIFY(!applyOperation(document, UpdateElementProperties{code.id, values}));
    QVERIFY(!applyOperation(document, ResizeElement{code.id, {100, 100}, {50, 50}}));
    QVERIFY(!applyOperation(document, UpdateText{QUuid::createUuid(), u"new"_s}));
    QCOMPARE(document.elements.front().text, code.text);
    QCOMPARE(document.elements.front().x, code.x);
    document.elements.front().locked = false;
    document.visualEditable = false;
    QVERIFY(!applyOperation(document, UpdateElementProperties{code.id, values}));
    QCOMPARE(document.elements.front().x, code.x);
}

void DocumentOperationTests::selectionPropertiesAreAtomicAndTyped()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text);
    auto shape = makeElement(ElementType::Box);
    auto code = makeElement(ElementType::QrCode);
    auto maxi = makeElement(ElementType::MaxiCode);
    code.locked = true;
    document.elements = {text, shape, code, maxi};
    SelectionPropertyValues values;
    values.fontHeight = 50;
    values.thickness = 8;
    values.moduleWidth = 7;
    UpdateSelectionProperties request{{text.id, shape.id, code.id, maxi.id, text.id}, values};
    QVERIFY(!applyOperation(document, request));
    QCOMPARE(document.elements[0].fontHeight, text.fontHeight);
    QCOMPARE(document.elements[1].thickness, shape.thickness);
    document.elements[2].locked = false;
    const auto changed = applyOperation(document, request);
    QVERIFY(changed && *changed);
    QCOMPARE(document.elements[0].fontHeight, 50);
    QCOMPARE(document.elements[0].moduleWidth, text.moduleWidth);
    QCOMPARE(document.elements[1].thickness, 8);
    QCOMPARE(document.elements[1].fontHeight, shape.fontHeight);
    QCOMPARE(document.elements[2].moduleWidth, 7);
    QCOMPARE(document.elements[3].moduleWidth, maxi.moduleWidth);
    const auto noop = applyOperation(document, request);
    QVERIFY(noop && !*noop);
    request.ids.append(QUuid::createUuid());
    request.values.fontHeight = 80;
    QVERIFY(!applyOperation(document, request));
    QCOMPARE(document.elements[0].fontHeight, 50);
}

void DocumentOperationTests::textOperationClearsLiteralFlagOnlyWhenPayloadChanges()
{
    DesignDocument document;
    auto matrix = makeElement(ElementType::DataMatrix);
    matrix.gs1 = false;
    matrix.literalText = true;
    document.elements = {matrix};
    QVERIFY(applyOperation(document, UpdateText{matrix.id, matrix.text, true}).value_or(false));
    QVERIFY(document.elements.front().literalText);
    QVERIFY(document.elements.front().gs1);
    QVERIFY(applyOperation(document, UpdateText{matrix.id, u"(01)"_s, true, true}).value_or(false));
    QVERIFY(document.elements.front().literalText);
    QCOMPARE(document.elements.front().text, u"(01)"_s);
    QVERIFY(applyOperation(document, UpdateText{matrix.id, u"(01)0"_s}).value_or(false));
    QVERIFY(!document.elements.front().literalText);
}

void DocumentOperationTests::resizeHonorsDefaultOrientationAndSource()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text);
    text.text = u"AB"_s;
    text.fontHeight = 20;
    text.fontWidth = 10;
    text.useDefaultOrientation = true;
    text.orientation = u'N';
    text.sourcePrefix = u"^FXretained^FS"_s;
    document.defaultOrientation = u'R';
    document.elements = {text};
    const auto result = applyOperation(document, ResizeElement{text.id, {40, 40}, {20, 40}});
    QVERIFY2(result.has_value(), result ? "" : qPrintable(result.error()));
    QVERIFY(*result);
    const auto& after = document.elements.front();
    QCOMPARE(after.fontHeight, 40);
    QCOMPARE(after.fontWidth, 10);
    QCOMPARE(after.orientation, QChar(u'N'));
    QVERIFY(after.useDefaultOrientation);
    QCOMPARE(after.sourcePrefix, text.sourcePrefix);
    QCOMPARE(after.id, text.id);
}

void DocumentOperationTests::resizeRejectsUnavailableGeometry()
{
    DesignDocument document;
    auto code = makeElement(ElementType::QrCode);
    document.elements = {code};
    ResizeElement request{code.id, {100, 100}, {50, 50}};
    request.geometryError = u"unresolved placeholder"_s;
    QVERIFY(!applyOperation(document, request));
    request.geometryError.clear();
    request.currentSize = {};
    QVERIFY(!applyOperation(document, request));
    QCOMPARE(document.elements.front().moduleWidth, code.moduleWidth);
}

void DocumentOperationTests::resizeNoopAndFixedMaxiCode()
{
    DesignDocument document;
    auto shape = makeElement(ElementType::Box);
    document.elements = {shape};
    document.originalZpl = u"untouched"_s;
    const QSize current(shape.width, shape.height);
    const auto noop = applyOperation(document, ResizeElement{shape.id, current, current});
    QVERIFY(noop && !*noop);
    QCOMPARE(document.originalZpl, u"untouched"_s);
    auto maxi = makeElement(ElementType::MaxiCode);
    document.elements = {maxi};
    const auto fixed = applyOperation(document, ResizeElement{maxi.id, {400, 400}, {201, 195}});
    QVERIFY(fixed && !*fixed);
    QCOMPARE(document.elements.front().width, maxi.width);
    QCOMPARE(document.elements.front().height, maxi.height);
    QCOMPARE(document.originalZpl, u"untouched"_s);
}

void DocumentOperationTests::contentUpdatePreservesCommonAndInactiveValues()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text);
    text.name = u"Retained"_s;
    text.sourcePrefix = u"^FXretained^FS"_s;
    text.groupId = QUuid::createUuid();
    document.elements = {text};
    auto edited = makeElement(ElementType::Text, {900, 800});
    edited.name = u"Must not replace common values"_s;
    edited.text = u"Must not replace text in layout dialog"_s;
    edited.baselineOrigin = true;
    edited.textDirection = u'R';
    edited.characterSpacing = 3;
    edited.pdf417Columns = 20;
    const auto result = applyOperation(document, UpdateElementContent{text.id, edited});
    QVERIFY2(result.has_value(), result ? "" : qPrintable(result.error()));
    QVERIFY(*result);
    const auto& after = document.elements.front();
    QCOMPARE(after.id, text.id);
    QCOMPARE(after.groupId, text.groupId);
    QCOMPARE(after.name, text.name);
    QCOMPARE(after.text, text.text);
    QCOMPARE(after.x, text.x);
    QCOMPARE(after.y, text.y);
    QCOMPARE(after.sourcePrefix, text.sourcePrefix);
    QCOMPARE(after.pdf417Columns, text.pdf417Columns);
    QVERIFY(after.baselineOrigin);
    QCOMPARE(after.textDirection, QChar(u'R'));
    QCOMPARE(after.characterSpacing, 3);
    const auto noop = applyOperation(document, UpdateElementContent{text.id, edited});
    QVERIFY(noop && !*noop);
    edited.type = ElementType::Box;
    QVERIFY(!applyOperation(document, UpdateElementContent{text.id, edited}));
    QCOMPARE(document.elements.front().type, ElementType::Text);
}

void DocumentOperationTests::tableContentKeepsEditablePlaceholdersAndDerivesSize()
{
    DesignDocument document;
    auto table = makeElement(ElementType::Table);
    table.literalText = true;
    document.elements = {table};
    auto edited = table;
    edited.tableColumnWidths = {100, 200};
    edited.tableCells[0] = u"{{unfinished"_s;
    edited.width = 999; // The table width is derived from the grid.
    const auto result = applyOperation(document, UpdateElementContent{table.id, edited});
    QVERIFY(result && *result);
    QCOMPARE(document.elements.front().width, 300);
    QCOMPARE(document.elements.front().tableCells.front(), edited.tableCells.front());
    QVERIFY(!document.elements.front().literalText);
    edited.tableRowHeights = {1, 48};
    QVERIFY(!applyOperation(document, UpdateElementContent{table.id, edited}));
    QCOMPARE(document.elements.front().tableRowHeights, table.tableRowHeights);
}

void DocumentOperationTests::layerOperationsHonorLockAndKeepStableOrder()
{
    DesignDocument document;
    auto first = makeElement(ElementType::Box);
    auto second = makeElement(ElementType::Text);
    auto third = makeElement(ElementType::QrCode);
    first.locked = true;
    document.elements = {first, second, third};
    QVERIFY(!applyOperation(document, SetElementFlags{{second.id, first.id}, LayerFlag::Printable, false}));
    QVERIFY(document.elements[1].printable);
    QVERIFY(!applyOperation(document, RenameElement{first.id, u"name"_s}));
    QVERIFY(applyOperation(document, SetElementFlags{{first.id}, LayerFlag::Visible, false}).value_or(false));
    QVERIFY(!document.elements.front().editorVisible);
    QVERIFY(applyOperation(document, SetElementFlags{{first.id}, LayerFlag::Locked, false}).value_or(false));
    QVERIFY(applyOperation(document, RenameElement{first.id, u"  name  "_s}).value_or(false));
    QCOMPARE(document.elements.front().name, u"name"_s);
    QVERIFY(applyOperation(document, ReorderElements{{first.id, third.id}, 200}).value_or(false));
    QCOMPARE(document.elements[0].id, second.id);
    QCOMPARE(document.elements[1].id, first.id);
    QCOMPARE(document.elements[2].id, third.id);
    const auto noop = applyOperation(document, ReorderElements{{first.id, third.id}, 200});
    QVERIFY(noop && !*noop);
    QVERIFY(!applyOperation(document, ReorderElements{{first.id, QUuid::createUuid()}, -200}));
    QCOMPARE(document.elements[0].id, second.id);
}

void DocumentOperationTests::labelAndAddFailuresAreAtomic()
{
    DesignDocument document;
    const auto oldWidth = document.width;
    UpdateLabelSettings settings;
    settings.width = 700;
    settings.dpi = 42;
    QVERIFY(!applyOperation(document, settings));
    QCOMPARE(document.width, oldWidth);
    settings.dpi = 300;
    QVERIFY(applyOperation(document, settings).value_or(false));
    QCOMPARE(document.width, 700);
    QCOMPARE(document.dpi, 300);
    const auto noop = applyOperation(document, settings);
    QVERIFY(noop && !*noop);
    auto element = makeElement(ElementType::Text);
    QVERIFY(applyOperation(document, AddElement{element}).value_or(false));
    QVERIFY(!applyOperation(document, AddElement{element}));
    QCOMPARE(document.elements.size(), 1);
    element.id = QUuid::createUuid();
    element.x = 5000;
    QVERIFY(!applyOperation(document, AddElement{element}));
    QCOMPARE(document.elements.size(), 1);
}

void DocumentOperationTests::unchangedImportedPropertiesDoNotBlockEditing()
{
    DesignDocument document;
    auto element = makeElement(ElementType::Text);
    element.pdf417RowHeight = 100;
    document.elements = {element};
    ElementProperties properties;
    properties.pdf417RowHeight = 100;
    properties.x = 50;
    QVERIFY(applyOperation(document, UpdateElementProperties{element.id, properties}).value_or(false));
    QCOMPARE(document.elements.front().x, 50);
    QCOMPARE(document.elements.front().pdf417RowHeight, 100);
    properties.pdf417RowHeight = 101;
    properties.x = 51;
    QVERIFY(!applyOperation(document, UpdateElementProperties{element.id, properties}));
    QCOMPARE(document.elements.front().x, 50);
}

QTEST_MAIN(DocumentOperationTests)
#include "test_document_operations.moc"
