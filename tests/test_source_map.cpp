#include "zpl_source_map.hpp"

#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

class SourceMapTest final : public QObject {
    Q_OBJECT
private slots:
    void generatedFieldsUseUtf16Offsets();
    void importedFieldsKeepDistinctMultilineRanges();
    void tableFieldsAndLayersRetainTheirOwners();
    void dataMatrixMetadataIsNotAnObject();
    void regeneratedImportedFieldsKeepTheirOwners();
    void ambiguousSourcesHaveNoMapping();
};

void SourceMapTest::generatedFieldsUseUtf16Offsets()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text);
    text.text = u"Строка \U0001F642"_s;
    auto duplicate = text;
    duplicate.id = QUuid::createUuid();
    auto baseline = makeElement(ElementType::Text, {10, 120});
    baseline.baselineOrigin = true;
    baseline.text = u"Базовая линия"_s;
    document.elements = {text, duplicate, baseline};

    const auto source = toZpl(document);
    const auto ranges = sourceElementRanges(document, source);
    QCOMPARE(ranges.size(), 3);
    qsizetype previousEnd = 0;
    for (qsizetype index = 0; index < ranges.size(); ++index) {
        const auto& range = ranges[index];
        const auto field = elementZpl(document.elements[index]).trimmed();
        QCOMPARE(range.elementIndex, index);
        QCOMPARE(range.start, source.indexOf(field, previousEnd));
        QCOMPARE(range.length, field.size());
        QCOMPARE(source.sliced(range.start, range.length), field);
        previousEnd = range.start + range.length;
    }
    QVERIFY(ranges[1].start > ranges[0].start);
    QVERIFY(source.sliced(ranges[2].start, 3) == u"^FT");
}

void SourceMapTest::importedFieldsKeepDistinctMultilineRanges()
{
    const auto firstField = u"^FO10,20\r\n^A0N,20,10\r\n^FDИмя \U0001F642 {{name}}^FS"_s;
    const auto secondField = u"^FO10,20^A0N,20,10^FDИмя \U0001F642 {{name}}^FS"_s;
    const auto source = u"^XA\r\n^CI28^PW300^LL180\r\n^FXКомментарий \U0001F642 перед объектами^FS\r\n"_s
        + firstField + secondField + u"\r\n^FXПосле объектов^FS\r\n^XZ\r\n"_s;
    const auto imported = fromZpl(source);
    QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    QCOMPARE(imported->elements.size(), 2);
    QCOMPARE(toZpl(*imported), source);

    const auto ranges = sourceElementRanges(*imported, source);
    QCOMPARE(ranges.size(), 2);
    QCOMPARE(ranges[0].start, source.indexOf(firstField));
    QCOMPARE(ranges[0].length, firstField.size());
    QCOMPARE(ranges[1].start, source.indexOf(secondField));
    QCOMPARE(ranges[1].length, secondField.size());
    QCOMPARE(ranges[0].elementIndex, 0);
    QCOMPARE(ranges[1].elementIndex, 1);
    QCOMPARE(ranges[0].start + ranges[0].length, ranges[1].start);

    auto normalized = source;
    normalized.replace(u"\r\n"_s, u"\n"_s);
    auto normalizedField = firstField;
    normalizedField.replace(u"\r\n"_s, u"\n"_s);
    const auto normalizedRanges = sourceElementRanges(*imported, normalized);
    QCOMPARE(normalizedRanges.size(), 2);
    QCOMPARE(normalizedRanges[0].start, normalized.indexOf(normalizedField));
    QCOMPARE(normalizedRanges[0].length, normalizedField.size());
    QCOMPARE(normalizedRanges[1].start, normalized.indexOf(secondField));
    QVERIFY(normalizedRanges[1].start < ranges[1].start);
}

void SourceMapTest::tableFieldsAndLayersRetainTheirOwners()
{
    DesignDocument document;
    auto nonprinting = makeElement(ElementType::Text);
    nonprinting.printable = false;
    auto hidden = makeElement(ElementType::Text);
    hidden.editorVisible = false;
    hidden.text = u"Скрытый {{hidden}}"_s;
    auto table = makeElement(ElementType::Table);
    table.tableCells = {u"Ячейка {{cell}}"_s, {}, {}, {}};
    auto after = makeElement(ElementType::Box);
    document.elements = {nonprinting, hidden, table, after};

    const auto source = toZpl(document);
    QVERIFY(source.contains(u"^FXQTZPL_EDITOR_V2:"));
    const auto ranges = sourceElementRanges(document, source);
    const auto tableFieldCount = tableElements(table).size();
    QCOMPARE(ranges.size(), tableFieldCount + 2);
    QCOMPARE(ranges.first().elementIndex, 1);
    QCOMPARE(ranges.last().elementIndex, 3);
    for (qsizetype index = 1; index <= tableFieldCount; ++index) {
        QCOMPARE(ranges[index].elementIndex, 2);
        QVERIFY(source.sliced(ranges[index].start, ranges[index].length).startsWith(u"^FO"));
        QVERIFY(!source.sliced(ranges[index].start, ranges[index].length).contains(u"^FX"));
    }
    QVERIFY(ranges.first().start > source.indexOf(u"^FXQTZPL_EDITOR_V2:"));

    const auto imported = fromZpl(source);
    QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    const auto restoredRanges = sourceElementRanges(*imported, toZpl(*imported));
    QCOMPARE(restoredRanges.size(), ranges.size());
    for (qsizetype index = 0; index < ranges.size(); ++index) {
        QCOMPARE(restoredRanges[index].start, ranges[index].start);
        QCOMPARE(restoredRanges[index].length, ranges[index].length);
        QCOMPARE(restoredRanges[index].elementIndex, ranges[index].elementIndex);
    }
}

void SourceMapTest::dataMatrixMetadataIsNotAnObject()
{
    DesignDocument document;
    auto matrix = makeElement(ElementType::DataMatrix);
    matrix.gs1 = false;
    matrix.text = u"{{value}}"_s;
    document.elements = {matrix, makeElement(ElementType::Text)};
    const auto source = toZpl(document);
    const auto ranges = sourceElementRanges(document, source);
    QCOMPARE(ranges.size(), 2);
    const auto metadataStart = source.indexOf(u"^FXQTZPL_DM_TEMPLATE_V1:");
    QVERIFY(metadataStart >= 0);
    const auto metadataEnd = source.indexOf(u"^FS", metadataStart) + 3;
    QVERIFY(ranges[0].start >= metadataEnd);
    QCOMPARE(ranges[0].elementIndex, 0);
    QCOMPARE(ranges[1].elementIndex, 1);
    for (const auto& range : ranges)
        QVERIFY(!source.sliced(range.start, range.length).contains(u"^FX"));
}

void SourceMapTest::regeneratedImportedFieldsKeepTheirOwners()
{
    const auto source = u"^XA^PW300^LL180^CI28^FO10,20^A0N,20,10^FD{{one}}^FS"
        "^FXКомментарий между объектами^FS^FO10,60^A0N,20,10^FD{{two}}^FS^XZ"_s;
    auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY(imported->visualEditable);
    imported->originalZpl.clear();
    imported->elements.swapItemsAt(0, 1);
    imported->elements[0].x = 30;
    imported->width = 420;
    imported->printMirror = true;
    const auto edited = toZpl(*imported);
    QVERIFY(edited.contains(u"^PMY"));
    QVERIFY(edited.contains(u"^PW420"));
    const auto ranges = sourceElementRanges(*imported, edited);
    QCOMPARE(ranges.size(), 2);
    QCOMPARE(ranges[0].elementIndex, 0);
    QCOMPARE(ranges[1].elementIndex, 1);
    QVERIFY(edited.sliced(ranges[0].start, ranges[0].length).contains(u"{{two}}"));
    QVERIFY(edited.sliced(ranges[1].start, ranges[1].length).contains(u"{{one}}"));
    QVERIFY(!edited.sliced(ranges[0].start, ranges[0].length).contains(u"Комментарий"));
}

void SourceMapTest::ambiguousSourcesHaveNoMapping()
{
    DesignDocument document;
    document.elements = {makeElement(ElementType::Text)};
    const auto source = toZpl(document);
    document.visualEditable = false;
    QVERIFY(sourceElementRanges(document, source).isEmpty());
    document.visualEditable = true;
    QVERIFY(sourceElementRanges(document, u"^XA^XZ"_s).isEmpty());
    QVERIFY(sourceElementRanges(document, source + source).isEmpty());
    QVERIFY(sourceElementRanges(document, u"^XA^FO0,0^FO1,1^FDx^FS^XZ"_s).isEmpty());
    QVERIFY(sourceElementRanges(document, u"^XA^FO0,0^FXcomment^FS^XZ"_s).isEmpty());
    QVERIFY(sourceElementRanges(document, u"^XA^FO0,0^FDx^XZ"_s).isEmpty());
    document.elements.append(makeElement(ElementType::Text));
    QVERIFY(sourceElementRanges(document, source).isEmpty());
}

QTEST_MAIN(SourceMapTest)
#include "test_source_map.moc"
