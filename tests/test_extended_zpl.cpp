#include "design_document.hpp"
#include "element_clipboard.hpp"
#include "project_codec.hpp"

#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
DesignDocument compactDocument()
{
    DesignDocument document;
    document.width = 400;
    document.height = 280;
    return document;
}

QString notes(const DesignDocument& document)
{
    return document.importNotes.join(u"; "_s);
}
} // namespace

class ExtendedZplTests final : public QObject {
    Q_OBJECT
private slots:
    void baselineAndDirection_data();
    void baselineAndDirection();
    void implicitTypesetCoordinatesStayInSource();
    void defaultDirectionKeepsExplicitNormalIndependent();
    void globalTransformsKeepBoundsOnInk_data();
    void globalTransformsKeepBoundsOnInk();
    void changeImportedGlobalsPreservesUnrelatedSource();
    void multilineGlobalSettingsKeepTokenBoundaries();
    void changingFieldDefaultsRemainLosslessSource();
    void whiteShapesAndDiagonalRemainEditable();
    void extendedFieldsSurviveProjectAndClipboard();
    void spacingIsBoundedBeforeRendering();
};

void ExtendedZplTests::baselineAndDirection_data()
{
    QTest::addColumn<QChar>("orientation");
    QTest::addColumn<QChar>("direction");
    for (const QChar orientation : QStringView{u"NRIB"})
        for (const QChar direction : QStringView{u"HVR"})
            QTest::newRow(qPrintable(QString(orientation) + direction)) << orientation << direction;
}

void ExtendedZplTests::baselineAndDirection()
{
    QFETCH(QChar, orientation);
    QFETCH(QChar, direction);
    auto document = compactDocument();
    auto text = makeElement(ElementType::Text, {180, 130});
    text.text = u"ABC"_s;
    text.fontHeight = 24;
    text.fontWidth = 16;
    text.baselineOrigin = true;
    text.originJustification = 1;
    text.orientation = orientation;
    text.textDirection = direction;
    text.characterSpacing = 2;
    document.elements.append(text);
    const auto source = toZpl(document);
    QVERIFY(source.contains(u"^FT180,130,1"));
    QVERIFY(source.contains(u"^FP%1,2"_s.arg(direction)));
    const auto original = renderPreview(document);
    QVERIFY2(original.has_value(), original ? "" : qPrintable(original.error()));
    const auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(notes(*imported)));
    QCOMPARE(toZpl(*imported), source);
    QCOMPARE(imported->elements[0].baselineOrigin, true);
    QCOMPARE(imported->elements[0].originJustification, 1);
    QCOMPARE(imported->elements[0].textDirection, direction);
    QCOMPARE(imported->elements[0].characterSpacing, 2);
    auto regenerated = *imported;
    regenerated.originalZpl.clear();
    regenerated.sourcePreamble.clear();
    const auto next = renderPreview(regenerated);
    QVERIFY(next.has_value());
    QCOMPARE(next->image, original->image);
    QCOMPARE(next->bounds, original->bounds);
}

void ExtendedZplTests::implicitTypesetCoordinatesStayInSource()
{
    const auto source = u"^XA^PW400^LL280^FT10,50^A0N,24,16^FDABC^FS^FT,90^FDDEF^FS^XZ"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY(!imported->visualEditable);
    QCOMPARE(toZpl(*imported), source);
    QVERIFY(notes(*imported).contains(u"Неявные координаты ^FT"));
}

void ExtendedZplTests::defaultDirectionKeepsExplicitNormalIndependent()
{
    auto document = compactDocument();
    document.defaultOrientation = u'R';
    document.defaultJustification = 1;
    auto normal = makeElement(ElementType::Text, {150, 30});
    normal.text = u"ABC"_s; normal.fontHeight = 24; normal.fontWidth = 16;
    normal.originJustification = 0;
    auto inherited = normal;
    inherited.id = QUuid::createUuid(); inherited.x = 280;
    inherited.useDefaultOrientation = true;
    document.elements = {normal, inherited};
    const auto rendered = renderPreview(document);
    QVERIFY2(rendered.has_value(), rendered ? "" : qPrintable(rendered.error()));
    QVERIFY(rendered->bounds[0].width() > rendered->bounds[0].height());
    QVERIFY(rendered->bounds[1].height() > rendered->bounds[1].width());
    const auto imported = fromZpl(toZpl(document));
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(notes(*imported)));
    QCOMPARE(imported->defaultOrientation, QChar(u'R'));
    QCOMPARE(imported->defaultJustification, 1);
    QVERIFY(!imported->elements[0].useDefaultOrientation);
    QVERIFY(imported->elements[1].useDefaultOrientation);
    auto edited = *imported;
    edited.originalZpl.clear();
    edited.defaultOrientation = u'B';
    const auto changed = renderPreview(edited);
    QVERIFY(changed.has_value());
    QCOMPARE(changed->bounds[0], rendered->bounds[0]);
    QVERIFY(changed->image != rendered->image);
}

void ExtendedZplTests::globalTransformsKeepBoundsOnInk_data()
{
    QTest::addColumn<bool>("mirror");
    QTest::addColumn<bool>("inverted");
    QTest::newRow("none") << false << false;
    QTest::newRow("mirror") << true << false;
    QTest::newRow("180") << false << true;
    QTest::newRow("mirror180") << true << true;
}

void ExtendedZplTests::globalTransformsKeepBoundsOnInk()
{
    QFETCH(bool, mirror);
    QFETCH(bool, inverted);
    auto document = compactDocument();
    auto box = makeElement(ElementType::Box, {40, 60});
    box.width = 65; box.height = 42; box.thickness = 4;
    auto text = makeElement(ElementType::Text, {180, 130});
    text.text = u"ABC"_s; text.fontHeight = 24; text.fontWidth = 16;
    document.elements = {box, text};
    const auto untransformed = renderPreview(document);
    QVERIFY(untransformed.has_value());
    document.printMirror = mirror;
    document.printInverted = inverted;
    const auto transformed = renderPreview(document);
    QVERIFY2(transformed.has_value(), transformed ? "" : qPrintable(transformed.error()));
    QImage expected = untransformed->image;
    if (inverted) expected.flip(Qt::Horizontal | Qt::Vertical);
    if (mirror) expected.flip(Qt::Horizontal);
    QCOMPARE(transformed->image, expected);
    for (qsizetype index = 0; index < transformed->bounds.size(); ++index)
        QCOMPARE(transformed->bounds[index], mapDocumentRect(document, untransformed->bounds[index]));
    const auto rect = QRect(30, 50, 60, 70);
    QCOMPARE(mapDocumentRect(document, mapDocumentRect(document, rect)), rect);
    QCOMPARE(mapDocumentPoint(document, mapDocumentPoint(document, {30, 50})), QPoint(30, 50));
    QCOMPARE(mapDocumentDelta(document, mapDocumentDelta(document, {7, -4})), QPoint(7, -4));
    const auto reopened = fromZpl(toZpl(document));
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(notes(*reopened)));
    QCOMPARE(reopened->printMirror, mirror);
    QCOMPARE(reopened->printInverted, inverted);
}

void ExtendedZplTests::changeImportedGlobalsPreservesUnrelatedSource()
{
    const auto source = u"^XA^PW400^LL280^PON^PMN^LRN^FWN,0^FXKeep PM text here^FS"
        u"^FO20,30^A0N,24,16^FH_^FDABC _5EPM^FS^PQ1^XZ"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(notes(*imported)));
    auto edited = *imported;
    edited.originalZpl.clear();
    edited.printMirror = true;
    edited.printInverted = true;
    edited.labelReverse = true;
    const auto result = toZpl(edited);
    QVERIFY(result.contains(u"^FXKeep PM text here^FS"));
    QVERIFY(result.contains(u"_5EPM"));
    QVERIFY(result.contains(u"^PQ1"));
    QVERIFY(result.contains(u"^PMY"));
    QVERIFY(result.contains(u"^POI"));
    QVERIFY(result.contains(u"^LRY"));
    const auto reopened = fromZpl(result);
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(notes(*reopened)));
    QVERIFY(reopened->printMirror && reopened->printInverted && reopened->labelReverse);
    const auto preview = renderPreview(edited);
    const auto reopenedPreview = renderPreview(*reopened);
    QVERIFY(preview.has_value() && reopenedPreview.has_value());
    QCOMPARE(preview->image, reopenedPreview->image);
}

void ExtendedZplTests::multilineGlobalSettingsKeepTokenBoundaries()
{
    const auto source = u"^XA^PW400^LL280^PM\nY\r\n^PO\nN\r\n^FW\nN,0\r\n"
        u"^FXKeep this comment\r\n^FS^FO20,30^A0N,24,16^FDABC^FS^XZ"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(notes(*imported)));
    auto edited = *imported;
    edited.originalZpl.clear();
    edited.printMirror = false;
    edited.printInverted = true;
    const auto result = toZpl(edited);
    QVERIFY(result.contains(u"^FXKeep this comment\r\n^FS"));
    QVERIFY(!result.contains(u"Y\r\n"));
    QVERIFY(!result.contains(u"\nN,0"));
    const auto reopened = fromZpl(result);
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(notes(*reopened)));
    QVERIFY(!reopened->printMirror && reopened->printInverted);
}

void ExtendedZplTests::changingFieldDefaultsRemainLosslessSource()
{
    for (const auto& settings : {u"^FWR"_s, u"^LRY"_s}) {
        const auto source = u"^XA^PW400^LL280^FO20,30^A0N,24,16^FDABC^FS"_s
            + settings + u"^FO200,90^FDDEF^FS^XZ"_s;
        const auto imported = fromZpl(source);
        QVERIFY(imported.has_value());
        QVERIFY(!imported->visualEditable);
        QCOMPARE(toZpl(*imported), source);
    }
}

void ExtendedZplTests::whiteShapesAndDiagonalRemainEditable()
{
    const auto source = u"^XA^PW400^LL280^FO20,30^GB160,100,80,B,0^FS"
        u"^FO40,50^GB80,40,40,W,0^FS^FT280,140,1^GD70,60,4,B,L^FS^XZ"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(notes(*imported)));
    QCOMPARE(imported->elements.size(), 3);
    QVERIFY(imported->elements[1].graphicWhite);
    QCOMPARE(imported->elements[2].type, ElementType::Diagonal);
    QCOMPARE(imported->elements[2].diagonalDirection, QChar(u'L'));
    const auto preview = renderPreview(*imported);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->bounds[2], QRect(210, 80, 70, 60));
    QVERIFY(qGray(preview->image.pixel(60, 60)) > 128);
    QVERIFY(qGray(preview->image.pixel(25, 35)) < 128);
    auto changed = *imported;
    changed.originalZpl.clear();
    changed.elements[2].diagonalDirection = u'R';
    changed.elements[2].x = 300;
    const auto reopened = fromZpl(toZpl(changed));
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(notes(*reopened)));
    QCOMPARE(reopened->elements[2].diagonalDirection, QChar(u'R'));
    QCOMPARE(reopened->elements[2].x, 300);
}

void ExtendedZplTests::extendedFieldsSurviveProjectAndClipboard()
{
    auto document = compactDocument();
    document.defaultOrientation = u'B'; document.defaultJustification = 2;
    document.printMirror = true; document.printInverted = true; document.labelReverse = true;
    auto text = makeElement(ElementType::Text, {200, 100});
    text.text = u"ABC"_s; text.baselineOrigin = true; text.originJustification = 1;
    text.useDefaultOrientation = true; text.textDirection = u'R'; text.characterSpacing = -5;
    auto diagonal = makeElement(ElementType::Diagonal);
    diagonal.graphicWhite = true; diagonal.diagonalDirection = u'L';
    document.elements = {text, diagonal};
    const auto copied = decodeElements(encodeElements(document.elements));
    QVERIFY(copied.has_value());
    QVERIFY(copied->at(0).baselineOrigin && copied->at(0).useDefaultOrientation);
    QCOMPARE(copied->at(0).characterSpacing, -5);
    QVERIFY(copied->at(1).graphicWhite);
    QCOMPARE(copied->at(1).diagonalDirection, QChar(u'L'));
    const auto bytes = encodeProject({document, {}, {}});
    QVERIFY2(bytes.has_value(), bytes ? "" : qPrintable(bytes.error()));
    const auto decoded = decodeProject(*bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->document.defaultOrientation, QChar(u'B'));
    QCOMPARE(decoded->document.defaultJustification, 2);
    QVERIFY(decoded->document.printMirror && decoded->document.printInverted && decoded->document.labelReverse);
    QCOMPARE(decoded->document.elements[0].characterSpacing, -5);
}

void ExtendedZplTests::spacingIsBoundedBeforeRendering()
{
    auto document = compactDocument();
    auto text = makeElement(ElementType::Text);
    text.characterSpacing = 9999; text.fontHeight = 512; text.text = QString(1024, u'W');
    document.elements.append(text);
    QVERIFY(!validateForExport(document).has_value());
    document.elements[0].fontHeight = 24;
    document.elements[0].text = u"AB"_s;
    QVERIFY(validateForExport(document).has_value());
    document.elements[0].characterSpacing = -11;
    QVERIFY(!validateForExport(document).has_value());
}

QTEST_MAIN(ExtendedZplTests)
#include "test_extended_zpl.moc"
