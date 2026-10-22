#include "design_document.hpp"
#include "data_matrix_values.hpp"
#include "element_clipboard.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

class TableBarcodeTest final : public QObject {
    Q_OBJECT
private slots:
    void tableRoundTripAndEditing();
    void tablesUnderGlobalTextDefaults();
    void tablePlaceholdersAndEscaping();
    void tableResizeAndClipboard();
    void metadataMismatchPreservesSource();
    void layerFlagsAndNamesRoundTrip();
    void namedImageMetadataKeepsPrintedPixels();
    void editorVisibilityDiffersFromPrinting();
    void additionalBarcodes_data();
    void additionalBarcodes();
    void qrCorrectionLevels();
    void rejectedInputsRemainEditable();
    void extendedBarcodeOptionsRoundTrip();
    void compressedGraphicsBecomeEditable();
    void labelaryReferenceAcceptance_data();
    void labelaryReferenceAcceptance();
};

void TableBarcodeTest::tableRoundTripAndEditing()
{
    DesignDocument document;
    auto table = makeElement(ElementType::Table, {20, 20});
    resizeTableGrid(table, 3, 2);
    table.tableCells = {u"Name"_s, u"Value"_s, u"Milk"_s, u"25"_s, u"Bread"_s, u"42"_s};
    table.name = u"Состав продукта"_s; table.groupId = QUuid::createUuid();
    document.elements = {table};
    QVERIFY(validateForExport(document).has_value());
    const auto preview = renderPreview(document);
    QVERIFY2(preview.has_value(), qPrintable(preview ? QString{} : preview.error()));
    QCOMPARE(preview->bounds.first(), QRect(20, 20, 240, 144));
    const auto zpl = toZpl(document);
    auto loaded = fromZpl(zpl);
    QVERIFY2(loaded.has_value(), qPrintable(loaded ? QString{} : loaded.error()));
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.size(), 1);
    QCOMPARE(loaded->elements.first().type, ElementType::Table);
    QCOMPARE(loaded->elements.first().tableCells, table.tableCells);
    QCOMPARE(loaded->elements.first().groupId, table.groupId);
    QCOMPARE(loaded->elements.first().name, table.name);
    loaded->originalZpl.clear(); loaded->elements.first().x += 20;
    const auto reopened = fromZpl(toZpl(*loaded));
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->elements.first().x, 40);
    QCOMPARE(reopened->elements.first().tableCells, table.tableCells);
    auto empty = *reopened; empty.originalZpl.clear(); empty.elements.clear();
    QVERIFY(validateForExport(empty).has_value());
    const auto emptyReloaded = fromZpl(toZpl(empty));
    QVERIFY(emptyReloaded.has_value());
    QVERIFY(emptyReloaded->visualEditable);
    QVERIFY(emptyReloaded->elements.isEmpty());
}

void TableBarcodeTest::tablePlaceholdersAndEscaping()
{
    DesignDocument document;
    auto table = makeElement(ElementType::Table);
    table.tableCells = {u"{{name}}"_s, u"[[price]]"_s, u"^FS_5E~"_s, u""_s};
    document.elements = {table};
    QCOMPARE(*dataMatrixParameters(document), QStringList({u"name"_s, u"price"_s}));
    document.previewValues = {{u"name"_s, u"Milk"_s}, {u"price"_s, u"42"_s}};
    const auto resolved = resolveDataMatrixValues(document);
    QVERIFY(resolved.has_value());
    QCOMPARE(resolved->elements.first().tableCells.first(), u"Milk"_s);
    const auto loaded = fromZpl(toZpl(document));
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.first().tableCells, table.tableCells);
    const auto concrete = fromZpl(toZpl(*resolved, false));
    QVERIFY(concrete.has_value());
    QVERIFY2(concrete->visualEditable, qPrintable(concrete->importNotes.join(u'\n')));
    QCOMPARE(concrete->elements.first().tableCells[2], u"^FS_5E~"_s);
}

void TableBarcodeTest::tablesUnderGlobalTextDefaults()
{
    for (const bool followedByText : {false, true}) {
        DesignDocument document; document.defaultOrientation = u'R'; document.defaultJustification = 1;
        auto table = makeElement(ElementType::Table, {20, 20}); table.tableCells[0] = u"ABC"_s;
        document.elements = {table};
        if (followedByText) document.elements.append(makeElement(ElementType::Text, {280, 30}));
        auto loaded = fromZpl(toZpl(document));
        QVERIFY(loaded.has_value());
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.first().type, ElementType::Table);
        QCOMPARE(loaded->elements.first().tableCells, table.tableCells);
        loaded->originalZpl.clear(); loaded->elements.first().tableCells[0] = u"Changed"_s;
        const auto reopened = fromZpl(toZpl(*loaded));
        QVERIFY(reopened.has_value());
        QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
        QCOMPARE(reopened->elements.first().tableCells.first(), u"Changed"_s);
    }
}

void TableBarcodeTest::tableResizeAndClipboard()
{
    auto table = makeElement(ElementType::Table);
    table.tableCells[0] = u"keep"_s;
    resizeTableGrid(table, 4, 3);
    QCOMPARE(table.tableCells[0], u"keep"_s);
    setTableDimensions(table, 600, 320);
    QCOMPARE(table.tableColumnWidths, QList<int>({200, 200, 200}));
    QCOMPARE(table.tableRowHeights, QList<int>({80, 80, 80, 80}));
    table.locked = true; table.printable = false;
    const auto clipboard = decodeElements(encodeElements({table}));
    QVERIFY(clipboard.has_value());
    QCOMPARE(clipboard->first().tableCells, table.tableCells);
    QCOMPARE(clipboard->first().tableColumnWidths, table.tableColumnWidths);
    QVERIFY(clipboard->first().locked);
    QVERIFY(!clipboard->first().printable);
}

void TableBarcodeTest::metadataMismatchPreservesSource()
{
    DesignDocument document;
    document.elements = {makeElement(ElementType::Table)};
    QString changed = toZpl(document);
    changed.replace(u"^FO40,40"_s, u"^FO41,40"_s);
    const auto loaded = fromZpl(changed);
    QVERIFY(loaded.has_value());
    QVERIFY(!loaded->visualEditable);
    QCOMPARE(toZpl(*loaded), changed);
    QString unknown = toZpl(document);
    unknown.replace(u"^XZ"_s, u"^ZZ1^XZ"_s);
    const auto unsupported = fromZpl(unknown);
    QVERIFY(unsupported.has_value());
    QVERIFY(!unsupported->visualEditable);
    QCOMPARE(toZpl(*unsupported), unknown);
    const QString malformedTemplate = u"^XA^PW400^LL200^FO10,10^B7N,6,bad,0,0,N^FD{{value}}^FS^XZ"_s;
    const auto malformed = fromZpl(malformedTemplate);
    QVERIFY(malformed.has_value());
    QVERIFY(!malformed->visualEditable);
    QCOMPARE(toZpl(*malformed), malformedTemplate);
}

void TableBarcodeTest::layerFlagsAndNamesRoundTrip()
{
    DesignDocument document;
    auto first = makeElement(ElementType::Text);
    first.name = u"Locked title"_s; first.locked = true; first.editorVisible = false;
    auto second = makeElement(ElementType::Text, {20, 120});
    second.name = u"Instruction"_s; second.printable = false;
    document.elements = {first, second};
    const auto loaded = fromZpl(toZpl(document));
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.size(), 2);
    QCOMPARE(loaded->elements.first().name, first.name);
    QVERIFY(loaded->elements.first().locked);
    QVERIFY(!loaded->elements.first().editorVisible);
    QVERIFY(!loaded->elements.last().printable);
    auto incomplete = makeElement(ElementType::Code39);
    incomplete.text = u"still typing /"_s; incomplete.printable = false;
    document.elements.append(incomplete);
    const auto reopened = fromZpl(toZpl(document));
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->elements.last().text, incomplete.text);
    auto filledBox = makeElement(ElementType::Box);
    filledBox.width = filledBox.height = filledBox.thickness = 30; filledBox.name = u"Solid background"_s;
    document.elements = {filledBox};
    const auto filledReloaded = fromZpl(toZpl(document));
    QVERIFY(filledReloaded.has_value());
    QVERIFY2(filledReloaded->visualEditable, qPrintable(filledReloaded->importNotes.join(u'\n')));
    QCOMPARE(filledReloaded->elements.first().type, ElementType::Box);
}

void TableBarcodeTest::editorVisibilityDiffersFromPrinting()
{
    DesignDocument document;
    auto box = makeElement(ElementType::Box, {20, 20});
    box.width = box.height = box.thickness = 30;
    box.printable = false; document.elements = {box};
    const auto printed = renderPreview(document);
    const auto editor = renderEditorPreview(document);
    QVERIFY(printed.has_value()); QVERIFY(editor.has_value());
    QVERIFY(qGray(printed->image.pixel(25, 25)) > 200);
    QVERIFY(qGray(editor->image.pixel(25, 25)) < 100);
    document.elements.first().printable = true;
    document.elements.first().editorVisible = false;
    const auto hidden = renderEditorPreview(document);
    QVERIFY(hidden.has_value());
    QCOMPARE(hidden->bounds.size(), 1);
    QVERIFY(qGray(hidden->image.pixel(25, 25)) > 200);
    const auto visiblePrint = renderPreview(document);
    QVERIFY(visiblePrint.has_value());
    QVERIFY(qGray(visiblePrint->image.pixel(25, 25)) < 100);
}

void TableBarcodeTest::namedImageMetadataKeepsPrintedPixels()
{
    DesignDocument document;
    auto image = makeElement(ElementType::Image, {30, 30});
    image.image = QImage(43, 21, QImage::Format_RGB32); image.image.fill(QColor(90, 120, 50));
    image.width = 43; image.height = 21; image.reverse = true; image.threshold = 100;
    image.name = u"Logo"_s; image.locked = true;
    document.elements = {image};
    const auto loaded = fromZpl(toZpl(document));
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.first().width, 43);
    QCOMPARE(loaded->elements.first().name, u"Logo"_s);
    QCOMPARE(renderPreview(*loaded)->image, renderPreview(document)->image);
}

void TableBarcodeTest::additionalBarcodes_data()
{
    QTest::addColumn<int>("kind");
    for (const auto type : {ElementType::Ean13, ElementType::Ean8, ElementType::UpcA, ElementType::UpcE,
                           ElementType::Code39, ElementType::Pdf417, ElementType::Codabar, ElementType::Code93,
                           ElementType::Interleaved2of5, ElementType::Industrial2of5, ElementType::Gs1DataBar,
                           ElementType::MicroPdf417, ElementType::Aztec, ElementType::MaxiCode})
        QTest::newRow(qPrintable(elementTypeName(type))) << int(type);
}

void TableBarcodeTest::additionalBarcodes()
{
    QFETCH(int, kind);
    DesignDocument document;
    auto code = makeElement(static_cast<ElementType>(kind), {50, 50});
    document.elements = {code};
    const auto valid = validateForExport(document, false);
    QVERIFY2(valid.has_value(), qPrintable(valid ? QString{} : valid.error()));
    const auto preview = renderPreview(document);
    QVERIFY2(preview.has_value(), qPrintable(preview ? QString{} : preview.error()));
    QVERIFY(!preview->bounds.first().isEmpty());
    const auto loaded = fromZpl(toZpl(document));
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.first().type, code.type);
    QCOMPARE(loaded->elements.first().text, code.text);
    QCOMPARE(renderPreview(*loaded)->image, preview->image);
}

void TableBarcodeTest::qrCorrectionLevels()
{
    for (const QChar ecc : QStringView{u"LMQH"}) {
        DesignDocument document;
        auto code = makeElement(ElementType::QrCode); code.qrErrorCorrection = ecc;
        document.elements = {code};
        const auto loaded = fromZpl(toZpl(document));
        QVERIFY(loaded.has_value());
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.first().qrErrorCorrection, ecc);
        QVERIFY(renderPreview(document).has_value());
    }
}

void TableBarcodeTest::rejectedInputsRemainEditable()
{
    for (const auto type : {ElementType::Ean13, ElementType::Ean8, ElementType::UpcA, ElementType::UpcE,
                           ElementType::Code39, ElementType::Codabar, ElementType::Code93,
                           ElementType::Interleaved2of5, ElementType::Industrial2of5, ElementType::Gs1DataBar}) {
        DesignDocument document;
        auto code = makeElement(type); code.text = u"Нельзя"_s;
        document.elements = {code};
        QVERIFY(!validateForExport(document, false).has_value());
        const auto editor = renderEditorPreview(document);
        QVERIFY2(editor.has_value(), qPrintable(editor ? QString{} : editor.error()));
        QCOMPARE(editor->elementErrors.size(), 1);
        QVERIFY(decodeElements(encodeElements({code})).has_value());
    }
    // Byte-mode symbols encode Unicode as UTF-8 under ^CI28 (product decision
    // of 2026-10-22); scanners without ECI may read those bytes as Latin-1.
    for (const auto type : {ElementType::Pdf417, ElementType::MicroPdf417, ElementType::Aztec, ElementType::MaxiCode}) {
        DesignDocument document;
        auto code = makeElement(type); code.text = u"Можно"_s;
        if (type == ElementType::MicroPdf417) code.microPdf417Mode = 20; // Three columns fit ten bytes.
        document.elements = {code};
        const auto valid = validateForExport(document, false);
        QVERIFY2(valid.has_value(), qPrintable(valid ? QString{} : valid.error()));
        const auto editor = renderEditorPreview(document);
        QVERIFY2(editor.has_value(), qPrintable(editor ? QString{} : editor.error()));
        QVERIFY(editor->elementErrors.isEmpty());
        const auto loaded = fromZpl(toZpl(document));
        QVERIFY(loaded.has_value());
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.first().type, type);
        QCOMPARE(loaded->elements.first().text, code.text);
    }
}

void TableBarcodeTest::extendedBarcodeOptionsRoundTrip()
{
    QList<Element> examples;
    auto rectangular = makeElement(ElementType::DataMatrix);
    rectangular.gs1 = false; rectangular.text = u"ABC123"_s;
    rectangular.dataMatrixRectangular = true; rectangular.dataMatrixRows = 8; rectangular.dataMatrixColumns = 32;
    examples.append(rectangular);
    auto pdf = makeElement(ElementType::Pdf417);
    pdf.pdf417Rows = 12; pdf.pdf417Columns = 3; pdf.pdf417Truncated = true;
    examples.append(pdf);
    auto aztec = makeElement(ElementType::Aztec);
    aztec.aztecSize = 300; aztec.text = u"42"_s; examples.append(aztec);
    auto micro = makeElement(ElementType::MicroPdf417);
    micro.microPdf417Mode = 33; examples.append(micro);
    auto maxi = makeElement(ElementType::MaxiCode);
    maxi.maxiCodeMode = 5; maxi.maxiCodePosition = 2; maxi.maxiCodeTotal = 3; examples.append(maxi);
    auto code93 = makeElement(ElementType::Code93);
    code93.barcodeCheckDigit = true; examples.append(code93);
    for (const auto& code : examples) {
        DesignDocument document; document.elements = {code};
        const auto rendered = renderPreview(document);
        QVERIFY2(rendered.has_value(), qPrintable(rendered ? QString{} : rendered.error()));
        auto loaded = fromZpl(toZpl(document));
        QVERIFY(loaded.has_value());
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(elementZpl(loaded->elements.first()), elementZpl(code));
        const auto decoded = decodeElements(encodeElements({code}));
        QVERIFY(decoded.has_value());
        QCOMPARE(elementZpl(decoded->first()), elementZpl(code));
    }
    DesignDocument inherited; inherited.defaultOrientation = u'R';
    auto code = makeElement(ElementType::Code128); code.useDefaultOrientation = true;
    inherited.elements = {code};
    const auto reloaded = fromZpl(toZpl(inherited));
    QVERIFY(reloaded.has_value());
    QVERIFY2(reloaded->visualEditable, qPrintable(reloaded->importNotes.join(u'\n')));
    QVERIFY(reloaded->elements.first().useDefaultOrientation);
}

void TableBarcodeTest::compressedGraphicsBecomeEditable()
{
    for (const auto source : {
        u"^XA^PW80^LL40^FO10,10^GFA,10,2,1,:Z64:eJxbFQoAAasBAA==:B023^FS^XZ"_s,
        u"^XA^PW80^LL40^FO10,10^GFA,4,4,2,FF00:^FS^XZ"_s}) {
        auto loaded = fromZpl(source);
        QVERIFY2(loaded.has_value(), qPrintable(loaded ? QString{} : loaded.error()));
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.first().type, ElementType::Image);
        QCOMPARE(toZpl(*loaded), source);
        loaded->originalZpl.clear(); loaded->elements.first().x += 5;
        const auto reopened = fromZpl(toZpl(*loaded));
        QVERIFY(reopened.has_value());
        QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
        QCOMPARE(reopened->elements.first().x, 15);
        QCOMPARE(reopened->elements.first().image, loaded->elements.first().image);
    }
}

void TableBarcodeTest::labelaryReferenceAcceptance_data()
{
    QTest::addColumn<QString>("fixture");
    for (const auto fixture : {u"extensions/ba-n", u"extensions/b2-n", u"extensions/bi-n",
             u"databar/type1-m1-h25", u"micropdf417/mode-00", u"aztec/hello",
             u"maxicode-extended/mode4", u"pdf417-truncated/rotation-n", u"datamatrix-rectangular/8x32-n"})
        QTest::newRow(qPrintable(QString(fixture))) << QString(fixture);
}

void TableBarcodeTest::labelaryReferenceAcceptance()
{
    QFETCH(QString, fixture);
    const auto relative = u"../../qt-zpl/tests/golden/"_s + fixture;
    const QString sourcePath = QFINDTESTDATA(qPrintable(relative + u".zpl"_s));
    const QString imagePath = QFINDTESTDATA(qPrintable(relative + u"-labelary-bitonal.png"_s));
    if (sourcePath.isEmpty() || imagePath.isEmpty()) QSKIP("Optional sibling Labelary references unavailable.");
    QFile file(sourcePath); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());
    auto imported = fromZpl(source);
    QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    // Acceptance tests regenerated Designer output against Labelary pixels,
    // rather than feeding the original fixture back into the same renderer.
    imported->originalZpl.clear(); imported->sourcePreamble.clear();
    const auto rendered = renderPreview(*imported);
    QVERIFY2(rendered.has_value(), qPrintable(rendered ? QString{} : rendered.error()));
    const QImage expected(imagePath); QVERIFY(!expected.isNull());
    QCOMPARE(rendered->image.convertToFormat(QImage::Format_RGB32), expected.convertToFormat(QImage::Format_RGB32));
}

QTEST_MAIN(TableBarcodeTest)
#include "test_table_barcodes.moc"
