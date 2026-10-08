#include "design_document.hpp"
#include "data_matrix_values.hpp"
#include "gs1_data.hpp"
#include "value_placeholders.hpp"

#include <QtTest>
#include <QRegularExpression>
#include <QtZpl/qtzpl.hpp>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
DesignDocument templateDocument(QString text, bool gs1 = true)
{
    DesignDocument document;
    document.width = 500;
    document.height = 300;
    auto element = makeElement(ElementType::DataMatrix, {24, 24});
    element.gs1 = gs1;
    element.moduleWidth = 3;
    element.text = std::move(text);
    document.elements.append(element);
    return document;
}

QString metadata(QStringView text)
{
    return u"^FXQTZPL_DM_TEMPLATE_V1:"_s + QString::fromLatin1(text.toUtf8().toBase64()) + u"^FS\n"_s;
}

// Contract of marker_newtsc::validateGroupAiGsSeparators: the preflight
// examines text before its formatter or the printer decodes any ZPL escapes.
bool passesMarkerPreflight(const QString& source)
{
    const QRegularExpression token(uR"((\d{2})\{\{(\1)\}\})"_s);
    const QStringList variable{u"10"_s, u"21"_s, u"91"_s, u"92"_s, u"93"_s};
    for (const auto& line : source.split(u'\n')) {
        auto matches = token.globalMatch(line);
        QRegularExpressionMatch previous;
        while (matches.hasNext()) {
            const auto current = matches.next();
            if (previous.hasMatch()) {
                const auto gap = line.mid(previous.capturedEnd(), current.capturedStart() - previous.capturedEnd());
                if (gap != QString(QChar(29)) && gap.compare(u"{{GS}}", Qt::CaseInsensitive) != 0
                    && (!gap.isEmpty() || variable.contains(previous.captured(1)))) return false;
            }
            previous = current;
        }
    }
    return true;
}
} // namespace

class DataMatrixTemplateTests final : public QObject {
    Q_OBJECT

private slots:
    void structuredGs1PreservesExactTokens();
    void wholeCodeTemplateRoundTrip();
    void plainTemplatePreservesUnicodeAndTokenNames();
    void mixedDocumentRoundTrip();
    void tamperedMetadataOrDataStaysInSourceMode();
    void externalOrUnsupportedTemplatesStayInSourceMode();
    void concreteEncodingDoesNotInterpretReplacement();
    void previewValuesNeverChangeTemplateExport();
    void previewUsesValidatedValuesWithoutMutatingTemplate();
    void markerTemplateRoundTripAndSubstitution();
    void builtinSeparatorNeedsNoPreviewValue();
    void markerDatesHaveIndependentPreviewValues();
    void allTemplateModesPreserveSubstitutedUnderscores();
    void legacyTemplateEncodingStillOpensVisually();
    void legacyConcreteFieldsInTemplateDocumentsStillOpenVisually();
    void mixedMarkerTemplatesPassPreflightAndRender();
};

void DataMatrixTemplateTests::structuredGs1PreservesExactTokens()
{
    const QString text = u"(01){{gtin_code}}(10)LOT-[[batch.id]](21)SN-{{serial_no}}"_s;
    auto document = templateDocument(text);
    const QString source = toZpl(document);
    QVERIFY(validateForExport(document));
    QVERIFY(source.contains(metadata(text)));
    QVERIFY(source.contains(u"{{gtin_code}}"));
    QVERIFY(source.contains(u"[[batch.id]]"));
    QVERIFY(source.contains(u"{{serial_no}}"));
    QVERIFY(!source.contains(u"gtin_5Fcode"));
    // Initial FNC1 and variable-field GS are printer bytes, separate from
    // untouched external parameter names.
    QVERIFY(source.contains(u"^FD|1\\30\\31{{gtin_code}}"));
    QVERIFY(source.contains(u"[[batch.id]]\\1D\\32\\31"));
    const auto loaded = fromZpl(source);
    QVERIFY2(loaded.has_value(), loaded ? "" : qPrintable(loaded.error()));
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.size(), 1);
    QCOMPARE(loaded->elements.first().text, text);
    QVERIFY(loaded->elements.first().gs1);
    QCOMPARE(toZpl(*loaded), source);
    auto edited = *loaded;
    edited.originalZpl.clear();
    QCOMPARE(toZpl(edited), source);
    edited.elements.first().x += 16;
    const auto reopened = fromZpl(toZpl(edited));
    QVERIFY(reopened);
    QVERIFY(reopened->visualEditable);
    QCOMPARE(reopened->elements.first().text, text);
    QCOMPARE(reopened->elements.first().x, 40);
}

void DataMatrixTemplateTests::wholeCodeTemplateRoundTrip()
{
    for (const auto text : {u"{{datamatrix}}"_s, u"[[full_code]]"_s}) {
        const auto document = templateDocument(text);
        const auto source = toZpl(document);
        QVERIFY(validateForExport(document));
        QVERIFY(source.contains(u"^FD|1"_s + text + u"^FS"_s));
        const auto loaded = fromZpl(source);
        QVERIFY(loaded);
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.first().text, text);
        QVERIFY(loaded->elements.first().gs1);
    }
}

void DataMatrixTemplateTests::plainTemplatePreservesUnicodeAndTokenNames()
{
    const QString text = u"Код: {{product_id}} | [[batch-name]] {{_7Cname}} ^FS _00"_s;
    const auto document = templateDocument(text, false);
    const auto source = toZpl(document);
    QVERIFY(validateForExport(document));
    QVERIFY(source.contains(u"{{product_id}}"));
    QVERIFY(source.contains(u"[[batch-name]]"));
    QVERIFY(source.contains(u"{{_7Cname}}"));
    QVERIFY(source.contains(u"\\5E\\46\\53"));
    QVERIFY(!source.contains(u"^FDКод"));
    auto loaded = fromZpl(source);
    QVERIFY(loaded);
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.first().text, text);
    QVERIFY(!loaded->elements.first().gs1);
    loaded->originalZpl.clear();
    QCOMPARE(toZpl(*loaded), source);
}

void DataMatrixTemplateTests::mixedDocumentRoundTrip()
{
    auto document = templateDocument(u"(01){{gtin}}(21)SN-{{serial}}"_s);
    auto plain = makeElement(ElementType::DataMatrix, {240, 24});
    plain.gs1 = false;
    plain.text = u"part:[[part_number]]"_s;
    document.elements.append(plain);
    auto text = makeElement(ElementType::Text, {24, 180});
    text.text = u"Товар 42"_s;
    document.elements.append(text);
    auto box = makeElement(ElementType::Box, {12, 12});
    box.width = 470;
    box.height = 270;
    document.elements.append(box);
    const auto source = toZpl(document);
    auto loaded = fromZpl(source);
    QVERIFY(loaded);
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.size(), 4);
    QCOMPARE(loaded->elements[0].text, document.elements[0].text);
    QCOMPARE(loaded->elements[1].text, document.elements[1].text);
    QCOMPARE(loaded->elements[2].text, document.elements[2].text);
    loaded->originalZpl.clear();
    QCOMPARE(toZpl(*loaded), source);
}

void DataMatrixTemplateTests::tamperedMetadataOrDataStaysInSourceMode()
{
    const QString text = u"(01){{gtin_code}}(21)SN-{{serial_no}}"_s;
    const auto document = templateDocument(text);
    const QString source = toZpl(document);
    QList<QString> altered;
    auto changed = source;
    changed.replace(u"{{gtin_code}}"_s, u"{{other_code}}"_s);
    altered.append(changed);
    changed = source;
    changed.replace(metadata(text), metadata(u"(01){{other_code}}(21)SN-{{serial_no}}"));
    altered.append(changed);
    changed = source;
    changed.replace(u"^BXN,3,200,0,0,1,|"_s, u"^BXN,3,200,0,0,0,|"_s);
    altered.append(changed);
    changed = source;
    changed.replace(u"^FH\\"_s, u"^FH#"_s);
    altered.append(changed);
    changed = source;
    changed.replace(metadata(text), u"^FXQTZPL_DM_TEMPLATE_V1:invalid!^FS\n"_s);
    altered.append(changed);
    changed = source;
    changed.replace(u"^XZ"_s, metadata(text) + u"^XZ"_s);
    altered.append(changed);
    changed = source;
    changed.replace(metadata(text), u"^FXUser comment must survive^FS\n"_s + metadata(text));
    const auto commented = fromZpl(changed);
    QVERIFY(commented.has_value());
    QVERIFY2(commented->visualEditable, qPrintable(commented->importNotes.join(u'\n')));
    QCOMPARE(toZpl(*commented), changed);
    changed = source;
    changed.replace(metadata(text), metadata(text) + metadata(text));
    altered.append(changed);
    changed = source;
    changed.replace(metadata(text), metadata(text) + u"^FO1,1^GB8,8,1^FS\n"_s);
    altered.append(changed);
    for (const auto& tampered : altered) {
        QVERIFY(tampered != source);
        const auto loaded = fromZpl(tampered);
        QVERIFY2(loaded.has_value(), loaded ? "" : qPrintable(loaded.error()));
        QVERIFY2(!loaded->visualEditable, qPrintable(tampered));
        QVERIFY(loaded->elements.isEmpty());
        QCOMPARE(toZpl(*loaded), tampered);
    }
}

void DataMatrixTemplateTests::externalOrUnsupportedTemplatesStayInSourceMode()
{
    const QString external = u"^XA^PW500^LL300^FO24,24^BXN,3,200,0,0,1,|^FD|101{{gtin}}21SN-{{serial}}^FS^XZ"_s;
    const auto loaded = fromZpl(external);
    QVERIFY(loaded);
    QVERIFY(!loaded->visualEditable);
    QCOMPARE(toZpl(*loaded), external);
    QVERIFY(!renderPreview(*loaded)); // Never encode unresolved raw placeholders as real barcode data.
    auto document = templateDocument(u"(01){{gtin}}(21){{serial}}"_s);
    auto text = makeElement(ElementType::Text, {24, 180});
    text.text = u"{{unsupported_text}}"_s;
    document.elements.append(text);
    const auto source = toZpl(document);
    const auto textTemplate = fromZpl(source);
    QVERIFY(textTemplate);
    QVERIFY2(textTemplate->visualEditable, qPrintable(textTemplate->importNotes.join(u'\n')));
    QCOMPARE(toZpl(*textTemplate), source);
}

void DataMatrixTemplateTests::concreteEncodingDoesNotInterpretReplacement()
{
    const auto resolved = resolveValueTemplate(u"prefix:{{value}}", {{u"value"_s, u"{{literal_token}} _1D | ^FS"_s}});
    QVERIFY(resolved);
    const auto document = templateDocument(*resolved, false);
    const auto source = toZpl(document, false);
    QVERIFY(!source.contains(u"QTZPL_DM_TEMPLATE"));
    QVERIFY(!source.contains(u"{{"));
    QVERIFY(!source.contains(u"[["));
    const auto parsed = QtZpl::parse(source);
    QVERIFY(parsed);
    QString actual;
    for (const auto& command : parsed->labels().first().commands())
        if (const auto* data = std::get_if<QtZpl::FieldData>(&command.payload)) actual = data->data;
    QString expected = *resolved;
    expected.replace(u"|"_s, u"||"_s);
    QCOMPARE(actual.toLatin1(), expected.toUtf8());
}

void DataMatrixTemplateTests::previewValuesNeverChangeTemplateExport()
{
    auto document = templateDocument(u"(01){{gtin}}(21)SN-{{serial}}"_s);
    const auto original = toZpl(document);
    document.previewValues = {{u"gtin"_s, u"09506000134352"_s}, {u"serial"_s, u"SECRET_PREVIEW"_s}};
    QCOMPARE(toZpl(document), original);
    QVERIFY(!toZpl(document).contains(u"SECRET_PREVIEW"));
    auto loaded = fromZpl(original);
    QVERIFY(loaded);
    QVERIFY(loaded->visualEditable);
    QVERIFY(loaded->previewValues.isEmpty());
}

void DataMatrixTemplateTests::previewUsesValidatedValuesWithoutMutatingTemplate()
{
    auto document = templateDocument(u"(01){{gtin}}(21)SN-{{serial}}"_s);
    const auto original = toZpl(document);
    document.previewValues = {{u"gtin"_s, u"09506000134352"_s}, {u"serial"_s, u"123"_s}};
    const auto resolved = resolveGs1Template(document.elements.first().text, document.previewValues);
    QVERIFY(resolved);
    const auto concrete = templateDocument(resolved->canonicalText);
    const auto preview = renderPreview(document);
    const auto expected = renderPreview(concrete);
    QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
    QVERIFY(expected);
    QCOMPARE(preview->image, expected->image);
    QCOMPARE(toZpl(document), original);
    QCOMPARE(document.elements.first().text, u"(01){{gtin}}(21)SN-{{serial}}"_s);
    document.previewValues[u"gtin"_s] = u"123"_s;
    QVERIFY(!renderPreview(document));
    QCOMPARE(toZpl(document), original);
}

void DataMatrixTemplateTests::markerTemplateRoundTripAndSubstitution()
{
    const QString raw = u"01{{01}}21{{21}}{{GS}}91{{91}}{{GS}}92{{92}}"_s;
    const QString human = u"(01){{01}}(21){{21}}(91){{91}}(92){{92}}"_s;
    for (const auto& text : {raw, human}) {
        auto document = templateDocument(text);
        document.previewValues = {{u"01"_s, u"09506000134352"_s}, {u"21"_s, u"SN_AB"_s},
                                  {u"91"_s, u"KEY1"_s}, {u"92"_s, u"SIGN_1D"_s}};
        QVERIFY(validateForExport(document));
        const auto source = toZpl(document);
        QVERIFY(source.contains(u"^FH\\^FD|1"_s + raw + u"^FS"_s));
        QVERIFY(passesMarkerPreflight(source));
        const auto loaded = fromZpl(source);
        QVERIFY(loaded);
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.first().text, text);
        auto edited = *loaded;
        edited.originalZpl.clear();
        QCOMPARE(toZpl(edited), source);

        // Exercise the plugin's raw replacement contract on the actual export,
        // including serials that would be corrupted by ^FH_ hex decoding.
        auto formatted = source;
        formatted.replace(u"{{GS}}"_s, QString(QChar(29)), Qt::CaseInsensitive);
        for (auto it = document.previewValues.cbegin(); it != document.previewValues.cend(); ++it)
            formatted.replace(u"{{%1}}"_s.arg(it.key()), it.value());
        const auto parsed = QtZpl::parse(formatted);
        QVERIFY(parsed);
        QString payload;
        for (const auto& command : parsed->labels().first().commands())
            if (const auto* data = std::get_if<QtZpl::FieldData>(&command.payload)) payload = data->data;
        QCOMPARE(payload, u"|1010950600013435221SN_AB"_s + QChar(29)
                    + u"91KEY1"_s + QChar(29) + u"92SIGN_1D"_s);
        const auto expected = templateDocument(u"(01)09506000134352(21)SN_AB(91)KEY1(92)SIGN_1D"_s);
        const auto preview = renderPreview(document);
        const auto concrete = renderPreview(expected);
        QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
        QVERIFY(concrete);
        QCOMPARE(preview->image, concrete->image);
        QCOMPARE(toZpl(document), source);
    }
}

void DataMatrixTemplateTests::builtinSeparatorNeedsNoPreviewValue()
{
    for (const bool gs1 : {true, false}) {
        const QString text = gs1 ? u"010950600013435221SERIAL{{gS}}91KEY"_s : u"ABC{{gS}}DEF"_s;
        const auto document = templateDocument(text, gs1);
        const auto names = dataMatrixParameters(document);
        QVERIFY(names);
        QVERIFY(names->isEmpty());
        const auto resolved = resolveDataMatrixValues(document);
        QVERIFY2(resolved.has_value(), resolved ? "" : qPrintable(resolved.error()));
        QVERIFY(!resolved->elements.first().text.contains(u"{{"));
        QVERIFY(renderPreview(document));
        const auto source = toZpl(document);
        const auto loaded = fromZpl(source);
        QVERIFY(loaded);
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.first().text, text);
    }
}

void DataMatrixTemplateTests::markerDatesHaveIndependentPreviewValues()
{
    auto document = templateDocument(u"{{11}}|[[11]]|{{17}}|[[17]]|[[BATCH]]|[[batch]]"_s, false);
    document.previewValues = {{u"11"_s, u"261007"_s}, {u"[[11]]"_s, u"07.10.2026"_s},
                              {u"17"_s, u"271007"_s}, {u"[[17]]"_s, u"2027/10/07"_s},
                              {u"batch"_s, u"LOT42"_s}};
    const auto names = dataMatrixParameters(document);
    QVERIFY(names);
    QCOMPARE(names->size(), 5);
    const auto resolved = resolveDataMatrixValues(document);
    QVERIFY(resolved);
    QCOMPARE(resolved->elements.first().text, u"261007|07.10.2026|271007|2027/10/07|LOT42|LOT42"_s);
    QVERIFY(renderPreview(document));
}

void DataMatrixTemplateTests::allTemplateModesPreserveSubstitutedUnderscores()
{
    for (const auto& text : {u"{{21}}"_s, u"[[serial]]"_s, u"(01)[[01]](21)[[serial]]"_s}) {
        const bool gs1 = text.startsWith(u'(');
        const auto document = templateDocument(text, gs1);
        const auto source = toZpl(document);
        QVERIFY(source.contains(u"^FH\\^FD"));
        const auto loaded = fromZpl(source);
        QVERIFY(loaded);
        QVERIFY(loaded->visualEditable);
        QCOMPARE(loaded->elements.first().text, text);
        auto formatted = source;
        formatted.replace(u"{{21}}"_s, u"SN_AB_1D"_s);
        formatted.replace(u"[[serial]]"_s, u"SN_AB_1D"_s);
        formatted.replace(u"[[01]]"_s, u"09506000134352"_s);
        const auto parsed = QtZpl::parse(formatted);
        QVERIFY(parsed);
        QString payload;
        for (const auto& command : parsed->labels().first().commands())
            if (const auto* data = std::get_if<QtZpl::FieldData>(&command.payload)) payload = data->data;
        QCOMPARE(payload, gs1 ? u"|1010950600013435221SN_AB_1D"_s : u"SN_AB_1D"_s);
    }
}

void DataMatrixTemplateTests::legacyTemplateEncodingStillOpensVisually()
{
    const auto document = templateDocument(u"(01){{gtin}}(21)[[serial]]"_s);
    const auto current = toZpl(document);
    auto old = current;
    old.replace(u"^FD|1"_s, u"^FD\\7C\\31"_s);
    old.replace(u'\\', u'_');
    QVERIFY(old != current);
    const auto loaded = fromZpl(old);
    QVERIFY(loaded);
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.first().text, document.elements.first().text);
    QCOMPARE(toZpl(*loaded), old);
    auto edited = *loaded;
    edited.originalZpl.clear();
    QCOMPARE(toZpl(edited), old);
    edited.elements.first().x += 16;
    const auto regenerated = toZpl(edited);
    QVERIFY(regenerated.contains(elementZpl(edited.elements.first())));
    QVERIFY(!regenerated.contains(u"^FH_^FD_7C_31"));
    auto reopened = fromZpl(regenerated);
    QVERIFY(reopened);
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->elements.first().text, document.elements.first().text);
    QCOMPARE(reopened->elements.first().x, 40);
    QVERIFY(reopened->elements.first().gs1);
    reopened->previewValues = {{u"gtin"_s, u"09506000134352"_s}, {u"serial"_s, u"SN_AB_1D"_s}};
    const auto resolved = resolveDataMatrixValues(*reopened);
    QVERIFY(resolved);
    QCOMPARE(resolved->elements.first().text, u"(01)09506000134352(21)SN_AB_1D"_s);
    auto expected = document;
    expected.elements.first().x = 40;
    expected.previewValues = reopened->previewValues;
    const auto preview = renderPreview(*reopened);
    const auto expectedPreview = renderPreview(expected);
    QVERIFY(preview);
    QVERIFY(expectedPreview);
    QCOMPARE(preview->image, expectedPreview->image);
}

void DataMatrixTemplateTests::legacyConcreteFieldsInTemplateDocumentsStillOpenVisually()
{
    auto document = templateDocument(u"(01){{gtin}}(21)[[serial]]"_s);
    auto concrete = makeElement(ElementType::DataMatrix, {240, 24});
    concrete.moduleWidth = 6;
    concrete.text = u"(01)09506000134352(21)NL-00001"_s;
    auto plain = makeElement(ElementType::DataMatrix, {240, 210});
    plain.gs1 = false;
    plain.moduleWidth = 2;
    plain.text = u"SN_AB_1D|^FS"_s;
    document.elements.append(concrete);
    document.elements.append(plain);
    const auto oldConcreteField = [](const Element& element) {
        QString data = element.gs1 ? parseGs1(element.text)->elementString : element.text;
        data.replace(u"|"_s, u"||"_s);
        if (element.gs1) data.prepend(u"|1"_s);
        QString encoded;
        for (const unsigned char byte : data.toUtf8())
            encoded += u'_' + QString::number(byte, 16).rightJustified(2, u'0').toUpper();
        return u"^FO%1,%2^BX%3,%4,200,0,0,%5,|^FH_^FD%6^FS\n"_s
            .arg(element.x).arg(element.y).arg(element.orientation).arg(element.moduleWidth)
            .arg(element.gs1 ? 1 : 0).arg(encoded);
    };
    const auto current = toZpl(document);
    auto oldConcrete = current;
    for (const auto& element : {concrete, plain})
        oldConcrete.replace(elementZpl(element), oldConcreteField(element));
    QVERIFY(oldConcrete != current);
    const auto currentTemplate = elementZpl(document.elements.first());
    auto oldTemplate = currentTemplate;
    oldTemplate.replace(u"^FD|1"_s, u"^FD\\7C\\31"_s);
    oldTemplate.replace(u'\\', u'_');
    QVERIFY(oldTemplate != currentTemplate);
    auto allLegacy = oldConcrete;
    allLegacy.replace(currentTemplate, oldTemplate);
    for (const auto& source : {oldConcrete, allLegacy}) {
        auto loaded = fromZpl(source);
        QVERIFY2(loaded.has_value(), loaded ? "" : qPrintable(loaded.error()));
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.size(), document.elements.size());
        for (qsizetype index = 0; index < document.elements.size(); ++index) {
            QCOMPARE(loaded->elements[index].text, document.elements[index].text);
            QCOMPARE(loaded->elements[index].gs1, document.elements[index].gs1);
        }
        QCOMPARE(toZpl(*loaded), source);
        loaded->originalZpl.clear();
        QCOMPARE(toZpl(*loaded), source);
        const QStringList originalFields{source == allLegacy ? oldTemplate : currentTemplate,
                                         oldConcreteField(concrete), oldConcreteField(plain)};
        for (qsizetype changed = 0; changed < loaded->elements.size(); ++changed) {
            auto edited = *loaded;
            edited.elements[changed].x += 8;
            const auto regenerated = toZpl(edited);
            QVERIFY(regenerated.contains(elementZpl(edited.elements[changed])));
            for (qsizetype index = 0; index < originalFields.size(); ++index) {
                if (index != changed) QVERIFY(regenerated.contains(originalFields[index]));
            }
            auto reopened = fromZpl(regenerated);
            QVERIFY(reopened);
            QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
            QCOMPARE(reopened->elements.size(), document.elements.size());
            for (qsizetype index = 0; index < document.elements.size(); ++index) {
                QCOMPARE(reopened->elements[index].text, document.elements[index].text);
                QCOMPARE(reopened->elements[index].gs1, document.elements[index].gs1);
                QCOMPARE(reopened->elements[index].moduleWidth, document.elements[index].moduleWidth);
                QCOMPARE(reopened->elements[index].x,
                         document.elements[index].x + (index == changed ? 8 : 0));
            }
            reopened->previewValues = {{u"gtin"_s, u"09506000134352"_s},
                                       {u"serial"_s, u"SN_AB_1D"_s}};
            const auto resolved = resolveDataMatrixValues(*reopened);
            QVERIFY(resolved);
            QCOMPARE(resolved->elements.first().text, u"(01)09506000134352(21)SN_AB_1D"_s);
            QCOMPARE(resolved->elements[1].text, concrete.text);
            QCOMPARE(resolved->elements[2].text, plain.text);
            auto expected = document;
            expected.elements[changed].x += 8;
            expected.previewValues = reopened->previewValues;
            const auto preview = renderPreview(*reopened);
            const auto expectedPreview = renderPreview(expected);
            QVERIFY(preview);
            QVERIFY(expectedPreview);
            QCOMPARE(preview->image, expectedPreview->image);
        }
    }
}

void DataMatrixTemplateTests::mixedMarkerTemplatesPassPreflightAndRender()
{
    for (const auto& text : {
             u"(01){{01}}(17)271231(21){{21}}"_s,
             u"(01){{01}}(21)SN-{{21}}(91){{91}}"_s,
             u"(00){{00}}(02)09506000134352(37){{37}}(30){{30}}"_s}) {
        auto document = templateDocument(text);
        document.previewValues = {{u"00"_s, u"000000000000000000"_s}, {u"01"_s, u"09506000134352"_s},
                                  {u"21"_s, u"A_AB"_s}, {u"91"_s, u"KEY"_s},
                                  {u"37"_s, u"5"_s}, {u"30"_s, u"7"_s}};
        const auto source = toZpl(document);
        QVERIFY2(passesMarkerPreflight(source), qPrintable(source));
        const auto loaded = fromZpl(source);
        QVERIFY(loaded);
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        QCOMPARE(loaded->elements.first().text, text);
        auto formatted = source;
        formatted.replace(u"{{GS}}"_s, QString(QChar(29)), Qt::CaseInsensitive);
        for (auto it = document.previewValues.cbegin(); it != document.previewValues.cend(); ++it)
            formatted.replace(u"{{%1}}"_s.arg(it.key()), it.value());
        const auto printed = QtZpl::render(formatted, {}, {});
        const auto preview = renderPreview(document);
        QVERIFY(printed);
        QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
        QCOMPARE(printed->labels.first(), preview->image);
    }
}

QTEST_MAIN(DataMatrixTemplateTests)
#include "test_datamatrix_templates.moc"
