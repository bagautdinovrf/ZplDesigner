#include "design_document.hpp"

#include <QtZpl/qtzpl.hpp>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
QString label(QStringView fields)
{
    return u"^XA^CI28^PW400^LL300^LH0,0"_s + fields + u"^XZ"_s;
}

QString notes(const DesignDocument& document)
{
    return document.importNotes.join(u"; "_s);
}

QStringList renderCodes(QStringView zpl)
{
    QStringList codes;
    const auto result = QtZpl::render(zpl);
    if (result)
        for (const auto& diagnostic : result->diagnostics) codes.append(diagnostic.code);
    return codes;
}

QImage rendered(QStringView zpl)
{
    const auto result = QtZpl::render(zpl);
    return result && result->labels.size() == 1 ? result->labels.front() : QImage{};
}

std::optional<QtZpl::ResolvedBarcode> resolvedBarcode(const QString& zpl)
{
    const auto document = QtZpl::parse(zpl);
    if (!document) return std::nullopt;
    const auto analysis = QtZpl::analyzeLabel(*document, 0);
    if (!analysis) return std::nullopt;
    for (const auto& field : analysis->fields)
        if (field.kind == QtZpl::FieldKind::Barcode) return field.settings.resolvedBarcode;
    return std::nullopt;
}
} // namespace

class BarcodeSemanticsTests final : public QObject {
    Q_OBJECT
private slots:
    void importReadsResolvedParameters_data();
    void importReadsResolvedParameters();
    void explicitNormalTextIgnoresRotatedDefault();
    void serializedElementsResolveToTheirFields_data();
    void serializedElementsResolveToTheirFields();
    void code39CaptionStillPendingInQtZpl();
    void externalFontStillSubstitutedByQtZpl();
    void repeatedLeadingFnc1StillCollapses();
    void dataMatrixFormatOneStillSelectsGs1();
};

// D1-D6 of the stage 2 plan: each import stays editable and takes QtZpl's
// resolved values, which match the Labelary references in ../qt-zpl.
void BarcodeSemanticsTests::importReadsResolvedParameters_data()
{
    QTest::addColumn<QString>("fields");
    QTest::addColumn<int>("type");
    QTest::addColumn<QChar>("orientation");
    QTest::addColumn<int>("height");
    QTest::addColumn<bool>("humanReadable");
    QTest::addColumn<bool>("above");
    QTest::newRow("d1-code128-zero-by-height") << u"^BY2,3,0^FO20,20^BCN,,Y,N^FD12345^FS"_s
        << int(ElementType::Code128) << QChar(u'N') << 1 << true << false;
    QTest::newRow("d1-ean13-zero-by-height") << u"^BY2,3,0^FO20,20^BEN,,N,N^FD123456789012^FS"_s
        << int(ElementType::Ean13) << QChar(u'N') << 1 << false << false;
    QTest::newRow("d2-code128-mode-u-above") << u"^BY2^FO20,60^BCN,60,Y,,N,U^FD0012345678901234567^FS"_s
        << int(ElementType::Code128) << QChar(u'N') << 60 << true << true;
    QTest::newRow("d4-ean8-lower-case") << u"^BY2^FO20,60^B8N,60,y,y^FD1234567^FS"_s
        << int(ElementType::Ean8) << QChar(u'N') << 60 << true << true;
    QTest::newRow("d4-ean8-lower-no") << u"^BY2^FO20,60^B8N,60,n^FD1234567^FS"_s
        << int(ElementType::Ean8) << QChar(u'N') << 60 << false << false;
    QTest::newRow("d5-code128-follows-fw") << u"^FWR^BY2^FO100,20^BC,60,N,N^FD12345^FS"_s
        << int(ElementType::Code128) << QChar(u'R') << 60 << false << false;
    QTest::newRow("d5-code128-explicit-n") << u"^FWR^BY2^FO100,20^BCN,60,N,N^FD12345^FS"_s
        << int(ElementType::Code128) << QChar(u'N') << 60 << false << false;
    QTest::newRow("d6-i2of5-zero-by-height") << u"^BY2,3,0^FO20,20^B2N,,N,N^FD12345678^FS"_s
        << int(ElementType::Interleaved2of5) << QChar(u'N') << 1 << false << false;
    QTest::newRow("d6-upca-by-height") << u"^BY2,3,40^FO40,20^BUN,,Y,N^FD03600029145^FS"_s
        << int(ElementType::UpcA) << QChar(u'N') << 40 << true << false;
}

void BarcodeSemanticsTests::importReadsResolvedParameters()
{
    QFETCH(QString, fields);
    QFETCH(int, type);
    QFETCH(QChar, orientation);
    QFETCH(int, height);
    QFETCH(bool, humanReadable);
    QFETCH(bool, above);
    const auto imported = fromZpl(label(fields));
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(notes(*imported)));
    QCOMPARE(imported->elements.size(), 1);
    const auto& element = imported->elements.first();
    QCOMPARE(int(element.type), type);
    QCOMPARE(element.orientation, orientation);
    QCOMPARE(element.height, height);
    QCOMPARE(element.humanReadable, humanReadable);
    QCOMPARE(element.interpretationAbove, above);
}

void BarcodeSemanticsTests::explicitNormalTextIgnoresRotatedDefault()
{
    // Labelary and Zebra keep an explicit N; the serializer needs no ^FWN reset.
    const auto imported = fromZpl(label(u"^FWR^FO100,20^A0N,30,30^FDABC^FS^FO200,20^A0,30,30^FDDEF^FS"));
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(notes(*imported)));
    QCOMPARE(imported->defaultOrientation, QChar(u'R'));
    QCOMPARE(imported->elements.size(), 2);
    QCOMPARE(imported->elements[0].orientation, QChar(u'N'));
    QVERIFY(!imported->elements[0].useDefaultOrientation);
    QVERIFY(imported->elements[1].useDefaultOrientation);
    auto regenerated = *imported;
    regenerated.originalZpl.clear();
    regenerated.sourcePreamble.clear();
    QVERIFY(!toZpl(regenerated).contains(u"^FWN"));

    // A ^FW after an explicit rotation inside the field wins for text.
    const auto later = fromZpl(label(u"^FO100,20^A0N,30,30^FWB^FDABC^FS"));
    QVERIFY(later.has_value());
    QCOMPARE(later->elements.first().orientation, QChar(u'B'));

    // MicroPDF417's default row height divides the ^BY height by its rows.
    const auto micro = fromZpl(label(u"^BY2,3,100^FO20,20^BFN,,3^FDABC123^FS"));
    QVERIFY(micro.has_value());
    QVERIFY2(micro->visualEditable, qPrintable(notes(*micro)));
    QCOMPARE(micro->elements.first().pdf417RowHeight, 5);
    QCOMPARE(micro->elements.first().microPdf417Mode, 3);
}

void BarcodeSemanticsTests::serializedElementsResolveToTheirFields_data()
{
    QTest::addColumn<int>("type");
    for (const auto type : {ElementType::Code128, ElementType::Ean13, ElementType::Ean8, ElementType::UpcA,
                            ElementType::UpcE, ElementType::Code39, ElementType::Codabar, ElementType::Code93,
                            ElementType::Interleaved2of5, ElementType::Industrial2of5, ElementType::Gs1DataBar,
                            ElementType::QrCode, ElementType::DataMatrix, ElementType::Pdf417,
                            ElementType::MicroPdf417, ElementType::Aztec, ElementType::MaxiCode})
        QTest::newRow(qPrintable(elementTypeName(type))) << int(type);
}

void BarcodeSemanticsTests::serializedElementsResolveToTheirFields()
{
    QFETCH(int, type);
    auto element = makeElement(ElementType(type), {40, 40});
    element.orientation = u'B';
    element.moduleWidth = 3;
    element.height = 57;
    element.interpretationAbove = element.humanReadable;
    element.barcodeCheckDigit = true;
    element.code128Mode = u'A';
    element.pdf417RowHeight = 7;
    element.pdf417Security = 3;
    element.pdf417Columns = 4;
    element.pdf417Truncated = true;
    element.microPdf417Mode = 20;
    element.dataBarType = 2;
    element.dataBarSeparator = 2;
    element.aztecSize = 15;
    element.aztecReaderInit = true;
    element.dataMatrixRectangular = true;
    element.maxiCodeMode = 4;
    element.maxiCodePosition = 2;
    element.maxiCodeTotal = 3;
    DesignDocument document;
    document.elements = {element};
    const auto zpl = toZpl(document);
    const auto code = resolvedBarcode(zpl);
    QVERIFY2(code.has_value(), qPrintable(zpl));
    using Symbology = QtZpl::Symbology;
    if (element.type == ElementType::MaxiCode) {
        QCOMPARE(code->symbology, Symbology::MaxiCode);
        const auto& options = std::get<QtZpl::MaxiCodeOptions>(code->options);
        QCOMPARE(options.mode, element.maxiCodeMode);
        QCOMPARE(options.position, element.maxiCodePosition);
        QCOMPARE(options.total, element.maxiCodeTotal);
        return;
    }
    QCOMPARE(QChar(QLatin1Char(static_cast<char>(code->orientation))), element.orientation);
    QVERIFY(!code->orientationOmitted);
    QCOMPARE(code->moduleWidth, element.moduleWidth);
    switch (element.type) {
    case ElementType::QrCode:
        QCOMPARE(code->symbology, Symbology::QrCode);
        break;
    case ElementType::DataMatrix: {
        QCOMPARE(code->symbology, Symbology::DataMatrix);
        const auto& options = std::get<QtZpl::DataMatrixOptions>(code->options);
        QCOMPARE(options.rectangular, element.dataMatrixRectangular);
        QCOMPARE(options.gs1, element.gs1);
        QCOMPARE(options.columns, element.dataMatrixColumns);
        QCOMPARE(options.rows, element.dataMatrixRows);
        break;
    }
    case ElementType::Pdf417: {
        QCOMPARE(code->symbology, Symbology::Pdf417);
        QCOMPARE(code->height, element.pdf417RowHeight);
        const auto& options = std::get<QtZpl::Pdf417Options>(code->options);
        QCOMPARE(options.security, element.pdf417Security);
        QCOMPARE(options.columns, element.pdf417Columns);
        QCOMPARE(options.rows, element.pdf417Rows);
        QCOMPARE(options.truncated, element.pdf417Truncated);
        break;
    }
    case ElementType::MicroPdf417:
        QCOMPARE(code->symbology, Symbology::MicroPdf417);
        QCOMPARE(code->height, element.pdf417RowHeight);
        QCOMPARE(std::get<QtZpl::MicroPdf417Options>(code->options).mode, element.microPdf417Mode);
        break;
    case ElementType::Gs1DataBar: {
        QCOMPARE(code->symbology, Symbology::DataBar);
        QCOMPARE(code->height, element.height);
        const auto& options = std::get<QtZpl::DataBarOptions>(code->options);
        QCOMPARE(options.type, element.dataBarType);
        QCOMPARE(options.separatorHeight, element.dataBarSeparator);
        QCOMPARE(options.segments, element.dataBarSegments);
        break;
    }
    case ElementType::Aztec: {
        QCOMPARE(code->symbology, Symbology::Aztec);
        const auto& options = std::get<QtZpl::AztecOptions>(code->options);
        QCOMPARE(options.size, element.aztecSize);
        QCOMPARE(options.readerInitialization, element.aztecReaderInit);
        QVERIFY(!options.eci);
        QCOMPARE(options.structuredAppend, 1);
        break;
    }
    case ElementType::Code128: {
        QCOMPARE(code->symbology, Symbology::Code128);
        QCOMPARE(code->height, element.height);
        QCOMPARE(code->interpretation, element.humanReadable);
        QCOMPARE(code->interpretationAbove, element.interpretationAbove);
        const auto& options = std::get<QtZpl::Code128Options>(code->options);
        QCOMPARE(options.mode, element.code128Mode);
        QCOMPARE(options.uccCheckDigit, element.code128CheckDigit);
        break;
    }
    default: {
        QCOMPARE(code->height, element.height);
        QCOMPARE(code->interpretation, element.humanReadable);
        QCOMPARE(code->interpretationAbove, element.interpretationAbove);
        const bool checkDigit = std::get<QtZpl::LinearOptions>(code->options).checkDigit;
        // The serializer omits UPC's e, so UPC prints its check digit; Code 39
        // and Codabar are written without one.
        if (element.type == ElementType::UpcA || element.type == ElementType::UpcE) QVERIFY(checkDigit);
        else if (element.type == ElementType::Code93 || element.type == ElementType::Interleaved2of5)
            QCOMPARE(checkDigit, element.barcodeCheckDigit);
        else QVERIFY(!checkDigit);
        break;
    }
    }
}

// Canaries: each fails once QtZpl gains the capability and names the Designer
// rule that should then be lifted.
void BarcodeSemanticsTests::code39CaptionStillPendingInQtZpl()
{
    for (const auto* command : {u"^B3N,N,40,Y,N", u"^BKN,N,40,Y,N"})
        QVERIFY2(renderCodes(label(u"^FO10,10"_s + QStringView(command) + u"^FDA123A^FS"_s))
                     .contains(u"barcode-interpretation-pending"_s),
                 "QtZpl draws Code 39/Codabar captions now: lift the caption rule in validateElement "
                 "and the barcode options dialog");
    auto element = makeElement(ElementType::Code39, {10, 10});
    element.humanReadable = true;
    DesignDocument document;
    document.elements = {element};
    QVERIFY(!validateForExport(document).has_value());
}

void BarcodeSemanticsTests::externalFontStillSubstitutedByQtZpl()
{
    const auto source = label(u"^FO10,10^A@N,30,20,E:ARIAL.TTF^FDABC^FS");
    QVERIFY2(renderCodes(source).contains(u"external-font-substituted"_s),
             "QtZpl renders ^A@ fonts now: reconsider the external-font-preview source-mode rule in fromZpl");
    const auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY(!imported->visualEditable);
    QVERIFY(notes(*imported).contains(u"external-font-preview"));
}

void BarcodeSemanticsTests::repeatedLeadingFnc1StillCollapses()
{
    const auto single = label(u"^FO10,10^BXN,4,200,0,0,1,|^FD|10109506000134352^FS");
    const auto repeated = label(u"^FO10,10^BXN,4,200,0,0,1,|^FD|1|10109506000134352^FS");
    QVERIFY2(rendered(single) == rendered(repeated),
             "QtZpl encodes each leading FNC1 now: reconsider the repeated-FNC1 source-mode rule in fromZpl");
    const auto imported = fromZpl(repeated);
    QVERIFY(imported.has_value());
    QVERIFY(!imported->visualEditable);
}

void BarcodeSemanticsTests::dataMatrixFormatOneStillSelectsGs1()
{
    const auto gs1 = label(u"^FO10,10^BXN,4,200,0,0,1,|^FD|10109506000134352^FS");
    const auto plain = label(u"^FO10,10^BXN,4,200,0,0,0,|^FD|10109506000134352^FS");
    QVERIFY2(rendered(gs1) != rendered(plain),
             "QtZpl derives GS1 from the leading FNC1 now: the serializer may stop writing format 1");
}

QTEST_MAIN(BarcodeSemanticsTests)
#include "test_barcode_semantics.moc"
