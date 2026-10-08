#include "data_matrix_values.hpp"
#include "document_session.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
// Synthetic command coverage only. The user's production label is inspected
// explicitly with designer_inspect_zpl and is not part of the test corpus.
QString milkSource()
{
    return u"^XA\r\n"
        u"^FX Synthetic dairy label, preserve this comment\r\n"
        u"^CI28^PW600^LL1200^LH0,0^PON\r\n"
        u"^FO26,510^A0B,26,33^FB650,1,0,C,0^FDМолоко 2,5%\\&^FS\r\n"
        u"^FX Manufacturer\r\n"
        u"^FO95,361^A0B,27,34^FDИзготовитель: пример^FS\r\n"
        u"^BY3\r\n"
        u"^FO252,198^BCB,178,N,N,N,D^FD(01){{gtin}}(11){{production_date_gs1}}(17){{expiration_date_gs1}}(10){{batch}}>8(21){{number}}^FS\r\n"
        u"^FX Aggregation\r\n"
        u"^BY3,2,60\r\n"
        u"^FO10,36^BCB,,Y,N^FD>;>800{{CODE}}^FS\r\n"
        u"^FO434,150^A0B,40,40^FB1104,1,0,C,0^FD(01){{gtin}}(11){{production_date_gs1}}(17){{expiration_date_gs1}}(10){{batch}}(21){{number}}\\&^FS\r\n"
        u"^FO482,725^A0B,25,38^FD{{production_date_short}}^FS\r\n"
        u"^FO76,24^A0I,70,251^FD{{batch}}^FS\r\n"
        u"^FX Synthetic icon\r\n"
        u"^FO488,375^GFA,8,8,1,8142241818244281^FS\r\n"
        u"^PQ1\r\n^XZ\r\n"_s;
}

QMap<QString, QString> demoValues()
{
    return {{u"gtin"_s, u"09506000134352"_s},
            {u"production_date_gs1"_s, u"261007"_s},
            {u"expiration_date_gs1"_s, u"261021"_s},
            {u"batch"_s, u"B42"_s}, {u"number"_s, u"123"_s},
            {u"CODE"_s, u"000123456789012343"_s},
            {u"production_date_short"_s, u"07.10.26"_s}};
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

class MilkImportTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void importsEveryFieldAndPrinterSetting();
    void unresolvedBarcodesLeaveOtherBlocksEditable();
    void visualEditsRetainUnchangedCommands();
    void savesTemplateAndExportsConcreteLabel();
};

void MilkImportTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(u"QtZplTests"_s);
    QCoreApplication::setApplicationName(u"import-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void MilkImportTests::cleanupTestCase()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QDir layouts(QDir(directory).filePath(u"layouts"_s));
    for (const auto& file : layouts.entryList(QDir::Files))
        QVERIFY(QFile::remove(layouts.filePath(file)));
    if (layouts.exists()) QVERIFY(QDir().rmdir(layouts.absolutePath()));
    if (QDir(directory).exists()) QVERIFY(QDir().rmdir(directory));
    QStandardPaths::setTestModeEnabled(false);
}

void MilkImportTests::importsEveryFieldAndPrinterSetting()
{
    const auto source = milkSource();
    const auto parsed = fromZpl(source);
    QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error()));
    QVERIFY2(parsed->visualEditable, qPrintable(parsed->importNotes.join(u'\n')));
    QCOMPARE(parsed->width, 600);
    QCOMPARE(parsed->height, 1200);
    QCOMPARE(parsed->elements.size(), 8);
    QCOMPARE(toZpl(*parsed), source);
    const auto& title = parsed->elements[0];
    QCOMPARE(title.type, ElementType::Text);
    QCOMPARE(QPoint(title.x, title.y), QPoint(26, 510));
    QCOMPARE(title.orientation, u'B');
    QCOMPARE(title.fontHeight, 26);
    QCOMPARE(title.fontWidth, 33);
    QCOMPARE(title.blockWidth, 650);
    QCOMPARE(title.blockMaxLines, 1);
    QCOMPARE(title.blockJustification, u'C');
    QVERIFY(title.text.contains(u"Молоко 2,5%"));
    const auto& gs1 = parsed->elements[2];
    QCOMPARE(gs1.type, ElementType::Code128);
    QCOMPARE(QPoint(gs1.x, gs1.y), QPoint(252, 198));
    QCOMPARE(gs1.orientation, u'B');
    QCOMPARE(gs1.code128Mode, u'D');
    QCOMPARE(gs1.moduleWidth, 3);
    QCOMPARE(gs1.height, 178);
    QVERIFY(!gs1.humanReadable);
    QVERIFY(gs1.text.contains(u"{{batch}}>8(21){{number}}"));
    const auto& aggregation = parsed->elements[3];
    QCOMPARE(aggregation.code128Mode, u'N');
    QCOMPARE(aggregation.moduleWidth, 3);
    QCOMPARE(aggregation.barcodeRatio, 2.0);
    QCOMPARE(aggregation.height, 60);
    QVERIFY(aggregation.humanReadable);
    QCOMPARE(aggregation.text, u">;>800{{CODE}}"_s);
    QCOMPARE(parsed->elements[6].orientation, u'I');
    QCOMPARE(parsed->elements[7].type, ElementType::Image);
    QCOMPARE(parsed->elements[7].image.size(), QSize(8, 8));
    const auto names = dataMatrixParameters(*parsed);
    QVERIFY2(names.has_value(), names ? "" : qPrintable(names.error()));
    QCOMPARE(names->size(), 7);
    const auto values = demoValues();
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        QVERIFY2(names->contains(it.key()), qPrintable(it.key()));
}

void MilkImportTests::unresolvedBarcodesLeaveOtherBlocksEditable()
{
    auto parsed = fromZpl(milkSource());
    QVERIFY(parsed);
    const auto canvas = renderEditorPreview(*parsed);
    QVERIFY2(canvas.has_value(), canvas ? "" : qPrintable(canvas.error()));
    QCOMPARE(canvas->image.size(), QSize(600, 1200));
    QCOMPARE(canvas->bounds.size(), parsed->elements.size());
    QCOMPARE(canvas->elementErrors.size(), 2);
    QVERIFY(canvas->elementErrors.contains(2));
    QVERIFY(canvas->elementErrors.contains(3));
    QVERIFY(!canvas->bounds[0].isEmpty());
    QVERIFY(!canvas->bounds[7].isEmpty());
    QVERIFY(!renderPreview(*parsed));
    parsed->previewValues = demoValues();
    const auto preview = renderPreview(*parsed);
    QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
    QCOMPARE(preview->image.size(), QSize(600, 1200));
    QVERIFY(preview->elementErrors.isEmpty());
    QCOMPARE(toZpl(*parsed), milkSource());
}

void MilkImportTests::visualEditsRetainUnchangedCommands()
{
    const auto parsed = fromZpl(milkSource());
    QVERIFY(parsed);
    DocumentSession session;
    session.reset(*parsed);
    auto edited = session.document();
    edited.elements[0].text = u"Новое наименование\\&"_s;
    edited.elements[0].x = 34;
    session.commit(edited, u"Edit imported title"_s);
    const auto changed = toZpl(session.document());
    QVERIFY(changed.contains(u"^FX Synthetic dairy label, preserve this comment"));
    QVERIFY(changed.contains(u"^FX Manufacturer"));
    QVERIFY(changed.contains(u"^FX Aggregation"));
    QVERIFY(changed.contains(u"^FX Synthetic icon"));
    QVERIFY(changed.contains(u"^PQ1"));
    QVERIFY(changed.contains(u"^PON"));
    QVERIFY(changed.contains(u"^FB650,1,0,C,0"));
    QVERIFY(changed.contains(u"^FO10,36^BCB,,Y,N^FD>;>800{{CODE}}^FS"));
    const auto reopened = fromZpl(changed);
    QVERIFY2(reopened.has_value(), reopened ? "" : qPrintable(reopened.error()));
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->elements.size(), 8);
    QCOMPARE(reopened->elements[0].x, 34);
    QCOMPARE(reopened->elements[0].text, edited.elements[0].text);
    QCOMPARE(reopened->elements[0].blockWidth, 650);
    QCOMPARE(reopened->elements[3].height, 60);
    QCOMPARE(reopened->elements[3].barcodeRatio, 2.0);
    session.undoStack()->undo();
    QCOMPARE(toZpl(session.document()), milkSource());
}

void MilkImportTests::savesTemplateAndExportsConcreteLabel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto templatePath = directory.filePath(u"template.zpl"_s);
    const auto filledPath = directory.filePath(u"filled.zpl"_s);
    const auto parsed = fromZpl(milkSource());
    QVERIFY(parsed);
    DocumentSession session;
    session.reset(*parsed);
    const auto saved = session.save(templatePath);
    QVERIFY2(saved.has_value(), saved ? "" : qPrintable(saved.error()));
    QCOMPARE(readBytes(templatePath), milkSource().toUtf8());
    QVERIFY(!QFile::exists(templatePath + u".layout.json"));
    QVERIFY(QFile::exists(DocumentSession::layoutStoragePath(templatePath)));
    session.setPreviewValues(demoValues());
    const auto result = session.saveResolved(filledPath);
    QVERIFY2(result.has_value(), result ? "" : qPrintable(result.error()));
    QVERIFY(!session.modified());
    QCOMPARE(session.filePath(), templatePath);
    QCOMPARE(readBytes(templatePath), milkSource().toUtf8());
    const auto filled = QString::fromUtf8(readBytes(filledPath));
    QVERIFY(!filled.isEmpty());
    QVERIFY(!filled.contains(u"{{"));
    QVERIFY(!filled.contains(u"[["));
    QVERIFY(filled.contains(u"^FX Synthetic dairy label, preserve this comment"));
    QVERIFY(filled.contains(u"^PQ1"));
    const auto reopened = fromZpl(filled);
    QVERIFY2(reopened.has_value(), reopened ? "" : qPrintable(reopened.error()));
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->elements.size(), 8);
    QCOMPARE(reopened->elements[2].text,
             u"(01)09506000134352(11)261007(17)261021(10)B42>8(21)123"_s);
    QCOMPARE(reopened->elements[3].text, u">;>800000123456789012343"_s);
    const auto preview = renderPreview(*reopened);
    QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
    QVERIFY(preview->elementErrors.isEmpty());
    QVERIFY(session.open(templatePath));
    QVERIFY(session.document().previewValues.isEmpty());
    QCOMPARE(toZpl(session.document()), milkSource());
}

QTEST_MAIN(MilkImportTests)
#include "test_milk_import.moc"
