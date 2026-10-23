#include "design_document.hpp"
#include "design_internal.hpp"
#include "gs1_data.hpp"
#include "value_placeholders.hpp"
#include "editor_metadata.hpp"
#include "eac_mark.hpp"

#include <algorithm>
#include <type_traits>
#include <optional>
#include <initializer_list>
#include <QHash>
#include <QStringDecoder>

namespace Designer {
using namespace Qt::StringLiterals;
using namespace Internal;
namespace {
constexpr QStringView templateMetadataPrefix = u"QTZPL_DM_TEMPLATE_V1:";

QString escapedField(QStringView text)
{
    // Leave Unicode as UTF-8 when the file is written, while ^CI28 declares it.
    // Hex-escape the ZPL delimiters and control bytes, including the FH marker.
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        const ushort value = ch.unicode();
        if (value < 32 || value == 127 || ch == u'^' || ch == u'~' || ch == u'_')
            result += u'_' + QString::number(value, 16).rightJustified(2, u'0').toUpper();
        else
            result += ch;
    }
    return result;
}

QString editableField(QStringView text, bool preservePlaceholders)
{
    if (!preservePlaceholders) {
        auto field = escapedField(text);
        field.replace(u'{', u"_7B"_s).replace(u'[', u"_5B"_s)
            .replace(u'}', u"_7D"_s).replace(u']', u"_5D"_s);
        return u"^FH_^FD"_s + field;
    }
    if (!text.contains(u"{{") && !text.contains(u"[[")) return u"^FH_^FD"_s + escapedField(text);
    const auto tokens = valuePlaceholders(text);
    if (!tokens || tokens->isEmpty()) return u"^FH_^FD"_s + escapedField(text);
    // A placeholder name may contain _AB, which ^FH_ would reinterpret as a
    // hex byte. Use a different indicator and leave only token spellings raw.
    QString result = u"^FH\\^FD"_s;
    qsizetype offset = 0;
    const auto appendLiteral = [&](QStringView literal) {
        for (const QChar ch : literal) {
            if (ch.unicode() < 32 || ch.unicode() == 127 || ch == u'^' || ch == u'~' || ch == u'\\')
                result += u'\\' + QString::number(ch.unicode(), 16).rightJustified(2, u'0').toUpper();
            else result += ch;
        }
    };
    for (const auto& token : *tokens) {
        appendLiteral(text.sliced(offset, token.offset - offset));
        result += text.sliced(token.offset, token.length);
        offset = token.offset + token.length;
    }
    appendLiteral(text.sliced(offset));
    return result;
}

QString header(int width, int height)
{
    return u"^XA\n^CI28\n^PW%1\n^LL%2\n^LH0,0\n"_s.arg(width).arg(height);
}

QString escapedBytes(const QByteArray& bytes, QChar escape = u'_')
{
    QString result;
    result.reserve(bytes.size() * 3);
    for (const unsigned char byte : bytes)
        result += escape + QString::number(byte, 16).rightJustified(2, u'0').toUpper();
    return result;
}

QString concreteDataMatrixField(QStringView data)
{
    // Retain byte-explicit UTF-8 for the existing import/renderer contract.
    // Ordinary ASCII needs no ^FH; leave its payload readable in the source.
    const auto bytes = data.toUtf8();
    if (std::ranges::any_of(bytes, [](unsigned char byte) { return byte >= 128; }))
        return u"^FH_^FD"_s + escapedBytes(bytes);
    const auto needsEscape = [](unsigned char byte) {
        return byte < 32 || byte == 127 || byte == '^' || byte == '~'
            || byte == '{' || byte == '[';
    };
    if (!std::ranges::any_of(bytes, needsEscape)) return u"^FD"_s + data.toString();
    QString field = u"^FH_^FD"_s;
    for (const unsigned char byte : bytes) {
        // Once ^FH is active, literal underscores must also be protected.
        // Escaped opening brackets keep concrete values from becoming tokens
        // for downstream {{...}} / [[...]] substitution.
        if (needsEscape(byte) || byte == '_')
            field += u'_' + QString::number(byte, 16).rightJustified(2, u'0').toUpper();
        else
            field += QLatin1Char(byte);
    }
    return field;
}

QString escapedTemplateBytes(QStringView text, QChar escape = u'_', bool avoidMarkerAiHits = false)
{
    const auto placeholders = valuePlaceholders(text);
    if (!placeholders) return {};
    QString result;
    qsizetype offset = 0;
    for (const auto& token : *placeholders) {
        const auto literal = text.sliced(offset, token.offset - offset).toUtf8();
        result += escapedBytes(literal, escape);
        // Newl2's preflight scans NN{{NN}} without decoding ZPL. Even hex
        // spelling such as \\30{{30}} can look like a literal AI. Use the
        // DataMatrix decimal escape for that last byte to remove ambiguity.
        if (avoidMarkerAiHits && !literal.isEmpty()
            && token.kind == ValuePlaceholderKind::NumericField && result.endsWith(token.name)) {
            result.chop(3);
            result += u"|d%1"_s.arg(static_cast<unsigned char>(literal.back()), 3, 10, u'0');
        }
        // These bytes belong to the downstream template vocabulary, including
        // underscores. Encoding the name would prevent ordinary replacement.
        result += text.sliced(token.offset, token.length);
        offset = token.offset + token.length;
    }
    return result + escapedBytes(text.sliced(offset).toUtf8(), escape);
}

std::optional<QString> decodeTemplateMetadata(QStringView text)
{
    if (!text.startsWith(templateMetadataPrefix)) return std::nullopt;
    const auto encoded = text.sliced(templateMetadataPrefix.size()).toLatin1();
    if (encoded.isEmpty() || encoded.size() > maximumField * 8) return std::nullopt;
    const auto bytes = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!bytes || bytes.decoded.toBase64() != encoded) return std::nullopt;
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder.decode(bytes.decoded);
    if (decoder.hasError() || decoded.size() > maximumField) return std::nullopt;
    const auto tokens = valuePlaceholders(decoded);
    if (!tokens || tokens->isEmpty()) return std::nullopt;
    return decoded;
}

QString labelSettings(const DesignDocument& document, bool includeDefaults = false)
{
    QString result;
    if (includeDefaults || document.defaultOrientation != u'N' || document.defaultJustification != 0)
        result += u"^FW%1,%2"_s.arg(document.defaultOrientation).arg(document.defaultJustification);
    if (includeDefaults || document.printMirror) result += document.printMirror ? u"^PMY"_s : u"^PMN"_s;
    if (includeDefaults || document.printInverted) result += document.printInverted ? u"^POI"_s : u"^PON"_s;
    if (includeDefaults || document.labelReverse) result += document.labelReverse ? u"^LRY"_s : u"^LRN"_s;
    return result;
}

QString replaceLabelSettings(QString source, const DesignDocument& document)
{
    const auto parsed = parseZpl(source);
    if (!parsed || parsed->labels().size() != 1) return source;
    struct Replacement { qsizetype offset; qsizetype length; QString text; };
    QList<Replacement> replacements;
    bool inField = false;
    const auto& commands = parsed->labels().first().commands();
    for (qsizetype index = 0; index < commands.size(); ++index) {
        const auto& command = commands[index];
        if (std::holds_alternative<QtZpl::FieldOrigin>(command.payload)
            || std::holds_alternative<QtZpl::FieldTypeset>(command.payload)) inField = true;
        if (std::holds_alternative<QtZpl::FieldSeparator>(command.payload)) inField = false;
        if ((!inField && std::holds_alternative<QtZpl::FieldDirection>(command.payload))
            || std::holds_alternative<QtZpl::PrintMirror>(command.payload)
            || std::holds_alternative<QtZpl::PrintOrientation>(command.payload)
            || std::holds_alternative<QtZpl::LabelReverse>(command.payload)) {
            // Replace the exact spelling, including line breaks inside its
            // parameters, and keep the whitespace before the next command.
            const qsizetype start = command.sourceSpan.start;
            replacements.append({start, commandEnd(*parsed, command) - start,
                std::holds_alternative<QtZpl::FieldDirection>(command.payload)
                    ? u"^FW%1,%2"_s.arg(document.defaultOrientation).arg(document.defaultJustification) : QString{}});
        }
    }
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it) source.replace(it->offset, it->length, it->text);
    source.insert(commandEnd(*parsed, commands.first()), labelSettings(document, true));
    return source;
}
} // namespace
static QString serializeElement(const Element& input, bool preservePlaceholders,
                                bool legacyTemplates = false, bool legacyConcrete = false,
                                QChar defaultOrientation = u'N', int defaultJustification = 0)
{
    using namespace Qt::StringLiterals;
    auto element = input;
    if (element.useDefaultOrientation) element.orientation = defaultOrientation;
    QString result = u"^%1%2,%3"_s.arg(element.baselineOrigin ? u"FT"_s : u"FO"_s).arg(element.x).arg(element.y);
    if (element.originJustification >= 0) result += u',' + QString::number(element.originJustification);
    if (element.reverse && element.type != ElementType::Image) result += u"^FR"_s;
    switch (element.type) {
    case ElementType::Eac: {
        result.clear();
        for (const auto& child : eacElements(element))
            result += serializeElement(child, preservePlaceholders, false, false, defaultOrientation, defaultJustification);
        return result;
    }
    case ElementType::Table: {
        result.clear();
        for (const auto& child : tableElements(element))
            result += serializeElement(child, preservePlaceholders, false, false, defaultOrientation, defaultJustification);
        return result;
    }
    case ElementType::Text:
        result += u"^A0%1,%2,%3"_s.arg(element.useDefaultOrientation ? QString{} : QString(element.orientation))
            .arg(element.fontHeight).arg(element.fontWidth);
        if (element.textDirection != u'H' || element.characterSpacing != 0)
            result += u"^FP%1,%2"_s.arg(element.textDirection).arg(element.characterSpacing);
        if (element.blockWidth > 0)
            result += u"^FB%1,%2,%3,%4,%5"_s.arg(element.blockWidth).arg(element.blockMaxLines)
                .arg(element.blockLineSpacing).arg(element.blockJustification).arg(element.blockHangingIndent);
        result += editableField(element.text, preservePlaceholders && !element.literalText);
        break;
    case ElementType::Box:
    case ElementType::Line:
        result += u"^GB%1,%2,%3,%4,0"_s.arg(element.width).arg(element.height).arg(element.thickness).arg(element.graphicWhite ? u'W' : u'B');
        break;
    case ElementType::Ellipse:
        result += u"^GE%1,%2,%3,%4"_s.arg(element.width).arg(element.height).arg(element.thickness).arg(element.graphicWhite ? u'W' : u'B');
        break;
    case ElementType::Circle:
        result += u"^GC%1,%2,%3"_s.arg(element.width).arg(element.thickness).arg(element.graphicWhite ? u'W' : u'B');
        break;
    case ElementType::Diagonal:
        result += u"^GD%1,%2,%3,%4,%5"_s.arg(element.width).arg(element.height).arg(element.thickness)
            .arg(element.graphicWhite ? u'W' : u'B').arg(element.diagonalDirection);
        break;
    case ElementType::Code128:
        result += u"^BY%1,%2,0^BC%3,%4,%5,%6,%7,%8"_s.arg(element.moduleWidth)
            .arg(element.barcodeRatio, 0, 'g', 8)
            .arg(element.orientation).arg(element.height).arg(element.humanReadable ? u'Y' : u'N')
            .arg(element.interpretationAbove ? u'Y' : u'N').arg(element.code128CheckDigit ? u'Y' : u'N')
            .arg(element.code128Mode);
        result += editableField(element.text, preservePlaceholders && !element.literalText);
        break;
    case ElementType::QrCode:
        result += u"^BY%1,3,0^BQ%2,2,%1"_s.arg(element.moduleWidth).arg(element.orientation);
        result += editableField(QString(element.qrErrorCorrection) + u"A,"_s + element.text, preservePlaceholders && !element.literalText);
        break;
    case ElementType::Ean13:
    case ElementType::Ean8:
    case ElementType::UpcA:
    case ElementType::UpcE:
    case ElementType::Code39:
    case ElementType::Codabar:
    case ElementType::Code93:
    case ElementType::Interleaved2of5:
    case ElementType::Industrial2of5:
    case ElementType::Gs1DataBar:
    case ElementType::MicroPdf417:
    case ElementType::Aztec:
    case ElementType::MaxiCode:
    case ElementType::Pdf417: {
        result += u"^BY%1,%2,0"_s.arg(element.moduleWidth).arg(element.barcodeRatio, 0, 'g', 8);
        if (element.type == ElementType::Pdf417)
            result += u"^B7%1,%2,%3,%4,%5,%6"_s.arg(element.orientation).arg(element.pdf417RowHeight)
                .arg(element.pdf417Security).arg(element.pdf417Columns).arg(element.pdf417Rows).arg(element.pdf417Truncated ? u'Y' : u'N');
        else if (element.type == ElementType::MicroPdf417)
            result += u"^BF%1,%2,%3"_s.arg(element.orientation).arg(element.pdf417RowHeight).arg(element.microPdf417Mode);
        else if (element.type == ElementType::Gs1DataBar)
            result += u"^BR%1,%2,%3,%4,%5,%6"_s.arg(element.orientation).arg(element.dataBarType).arg(element.moduleWidth)
                .arg(element.dataBarSeparator).arg(element.height).arg(element.dataBarSegments);
        else if (element.type == ElementType::Aztec)
            result += u"^BO%1,%2,N,%3,%4,1"_s.arg(element.orientation).arg(element.moduleWidth).arg(element.aztecSize)
                .arg(element.aztecReaderInit ? u'Y' : u'N');
        else if (element.type == ElementType::MaxiCode)
            result += u"^BD%1,%2,%3"_s.arg(element.maxiCodeMode).arg(element.maxiCodePosition).arg(element.maxiCodeTotal);
        else if (element.type == ElementType::Codabar)
            result += u"^BK%1,N,%2,N,N,%3,%4"_s.arg(element.orientation).arg(element.height)
                .arg(element.codabarStart).arg(element.codabarStop);
        else if (element.type == ElementType::Code39)
            result += u"^B3%1,N,%2,%3,%4"_s.arg(element.orientation).arg(element.height)
                .arg(element.humanReadable ? u'Y' : u'N').arg(element.interpretationAbove ? u'Y' : u'N');
        else {
            const QString command = element.type == ElementType::Ean13 ? u"BE"_s
                : element.type == ElementType::Ean8 ? u"B8"_s : element.type == ElementType::UpcA ? u"BU"_s
                : element.type == ElementType::UpcE ? u"B9"_s : element.type == ElementType::Code93 ? u"BA"_s
                : element.type == ElementType::Interleaved2of5 ? u"B2"_s : u"BI"_s;
            result += u"^%1%2,%3,%4,%5"_s.arg(command).arg(element.orientation).arg(element.height)
                .arg(element.humanReadable ? u'Y' : u'N').arg(element.interpretationAbove ? u'Y' : u'N');
            if (element.type == ElementType::Code93 || element.type == ElementType::Interleaved2of5)
                result += u',' + QString(element.barcodeCheckDigit ? u'Y' : u'N');
        }
        result += editableField(element.text, preservePlaceholders && !element.literalText);
        break;
    }
    case ElementType::DataMatrix: {
        const auto command = u"^BX%1,%2,200,%3,%4,%5,|%6"_s.arg(element.orientation).arg(element.moduleWidth)
            .arg(element.dataMatrixColumns).arg(element.dataMatrixRows).arg(element.gs1 ? 1 : 0)
            .arg(element.dataMatrixRectangular ? u",2"_s : QString{});
        QString data = element.text;
        QString templateText;
        bool markerTemplate = false;
        if (element.gs1) {
            const auto gs1 = preservePlaceholders ? parseGs1Template(data) : parseGs1(data);
            if (!gs1) return {};
            data = gs1->elementString;
            if (preservePlaceholders) {
                const auto tokens = valuePlaceholders(gs1->canonicalText);
                if (!tokens) return {};
                if (!tokens->isEmpty()) templateText = gs1->canonicalText;
                markerTemplate = !legacyTemplates && std::ranges::any_of(*tokens, [](const ValuePlaceholder& token) {
                    return token.kind == ValuePlaceholderKind::NumericField
                        || token.kind == ValuePlaceholderKind::GroupSeparator;
                });
                // The marker's textual AI preflight also rejects constant
                // fields or prefixes between two matching AI{{AI}} pairs.
                // Encode mixed fields instead; our GS1 parser validates them.
                if (markerTemplate && std::ranges::any_of(*tokens, [](const ValuePlaceholder& token) {
                        return token.kind == ValuePlaceholderKind::NumericField;
                    }))
                    markerTemplate = !gs1->fields.isEmpty() && std::ranges::all_of(gs1->fields, [](const Gs1Field& field) {
                        return field.ai.size() == 2 && field.value == u"{{%1}}"_s.arg(field.ai);
                    });
            }
            data.replace(u"|"_s, u"||"_s);
            // GS1 permits ASCII GS as an inter-field separator. The leading
            // FNC1 must instead be an explicit ^BX escape sequence. ^FH byte
            // processing precedes ^BX escapes, so |1 survives safely here.
            data.prepend(u"|1"_s);
        } else {
            if (preservePlaceholders) {
                const auto tokens = valuePlaceholders(data);
                if (!tokens) return {};
                if (!tokens->isEmpty()) templateText = data;
            }
            data.replace(u"|"_s, u"||"_s);
        }
        // Zebra ignores format ID f for ECC200. Keep f=1 for GS1 because the
        // existing QtZpl adapter uses it as its GS1 hint; the printer receives
        // its real GS1 indicator from the leading |1 above, independently.
        if (markerTemplate) {
            // marker_newtsc validates the literal AI{{AI}} sequence and then
            // substitutes raw field values. Keep those sequences readable.
            // GS1 X excludes backslash, so ^FH\\ cannot reinterpret a valid
            // serial such as SN_AB; the old ^FH_ would decode its underscore.
            data.replace(QChar(0x1d), u"{{GS}}"_s);
            result += command + u"^FH\\^FD"_s + data;
        } else if (!templateText.isEmpty() && !legacyTemplates) {
            // External formatters insert values literally. A separate hex
            // marker prevents underscores in ordinary/[[additional]] values
            // from accidentally being interpreted as hexadecimal bytes.
            result += command + u"^FH\\^FD"_s + (element.gs1
                    ? u"|1"_s + escapedTemplateBytes(QStringView(data).sliced(2), u'\\', true)
                    : escapedTemplateBytes(data, u'\\', true));
        } else if (templateText.isEmpty() && !legacyTemplates && !legacyConcrete) {
            result += command;
            result += concreteDataMatrixField(data);
        } else {
            result += command + u"^FH_^FD"_s + (templateText.isEmpty() ? escapedBytes(data.toUtf8()) : escapedTemplateBytes(data));
        }
        if (!templateText.isEmpty())
            result.prepend(u"^FX"_s + templateMetadataPrefix.toString()
                + QString::fromLatin1(templateText.toUtf8().toBase64()) + u"^FS\n"_s);
        break;
    }
    case ElementType::Image: {
        const QImage bitmap = monochromeImage(element);
        if (bitmap.isNull()) return {};
        const int rowBytes = (bitmap.width() + 7) / 8;
        QByteArray data(rowBytes * bitmap.height(), char{0});
        for (int y = 0; y < bitmap.height(); ++y)
            for (int x = 0; x < bitmap.width(); ++x)
                if (bitmap.constScanLine(y)[x] < 128)
                    data[y * rowBytes + x / 8] = char(uchar(data[y * rowBytes + x / 8]) | (0x80U >> (x % 8)));
        result += u"^GFA,%1,%1,%2,%3"_s.arg(data.size()).arg(rowBytes).arg(QString::fromLatin1(data.toHex().toUpper()));
        break;
    }
    }
    result += u"^FS\n"_s;
    return result;
}

QString elementZpl(const Element& element, bool preservePlaceholders, QChar defaultOrientation, int defaultJustification)
{
    return serializeElement(element, preservePlaceholders, false, false, defaultOrientation, defaultJustification);
}

QString Internal::printerZpl(const DesignDocument& document, bool preservePlaceholders)
{
    if (!document.originalZpl.isEmpty()) return document.originalZpl;
    if (!document.sourcePreamble.isEmpty()) {
        // Source fragments contain printer-only statements and comments. The
        // element list still defines field order; deleted fields disappear and
        // duplicated/new fields are serialized normally.
        const bool settingsChanged = labelSettings(document) != document.sourceSettingsSignature;
        bool restoreState = settingsChanged;
        qsizetype sourceIndex = 0;
        for (const auto& element : document.elements) {
            if (!element.printable) continue;
            restoreState |= sourceIndex >= document.sourceOrder.size() || document.sourceOrder.value(sourceIndex) != element.id
                || serializeElement(element, preservePlaceholders, false, false, document.defaultOrientation, document.defaultJustification) != element.sourceSignature;
            ++sourceIndex;
        }
        restoreState |= sourceIndex != document.sourceOrder.size();
        QString source = document.sourcePreamble;
        for (const auto& element : document.elements) {
            source += element.sourcePrefix;
            if (!element.printable) continue;
            const auto field = serializeElement(element, preservePlaceholders, false, false, document.defaultOrientation, document.defaultJustification);
            if (!settingsChanged && !element.sourceZpl.isEmpty() && field == element.sourceSignature) {
                if (restoreState) source += element.sourceState;
                source += element.sourceZpl;
            } else source += u"^CI28"_s + field;
        }
        source += document.sourceEpilogue;
        if (restoreState || document.width != document.sourceWidth || document.height != document.sourceHeight) {
            // Patch only actual parsed dimension commands, never ^PW/^LL words
            // occurring inside a comment or field. Work backward to keep offsets.
            const auto parsed = parseZpl(source);
            bool hasWidth = false;
            bool hasHeight = false;
            if (parsed && parsed->labels().size() == 1) {
                const auto& commands = parsed->labels().first().commands();
                for (auto it = commands.crbegin(); it != commands.crend(); ++it) {
                    QString replacement;
                    if (std::holds_alternative<QtZpl::PrintWidth>(it->payload)) {
                        hasWidth = true; replacement = u"^PW%1"_s.arg(document.width);
                    } else if (std::holds_alternative<QtZpl::LabelLength>(it->payload)) {
                        hasHeight = true; replacement = u"^LL%1"_s.arg(document.height);
                    }
                    // Spans follow the active ^CC/^CT prefixes; never search
                    // for a literal caret or tilde to find the command's end.
                    if (!replacement.isEmpty())
                        source.replace(it->sourceSpan.start, commandEnd(*parsed, *it) - it->sourceSpan.start, replacement);
                }
                QString missing;
                if (!hasWidth) missing += u"^PW%1"_s.arg(document.width);
                if (!hasHeight) missing += u"^LL%1"_s.arg(document.height);
                source.insert(commandEnd(*parsed, commands.first()), missing);
            }
        }
        return settingsChanged ? replaceLabelSettings(std::move(source), document) : source;
    }
    QString source = header(document.width, document.height) + labelSettings(document);
    for (const auto& element : document.elements)
        if (element.printable) source += serializeElement(element, preservePlaceholders, false, false, document.defaultOrientation, document.defaultJustification);
    return source + QStringLiteral("^XZ\n");
}

QString toZpl(const DesignDocument& document, bool preservePlaceholders)
{
    if (!document.originalZpl.isEmpty()) return document.originalZpl;
    return writeEditorMetadata(document, printerZpl(document, preservePlaceholders));
}

std::expected<DesignDocument, QString> fromZpl(const QString& source)
{
    using namespace Qt::StringLiterals;
    if (source.size() > maximumSource || source.toUtf8().size() > maximumSource)
        return std::unexpected(u"ZPL превышает предел 8 МиБ в UTF-8."_s);
    const auto parsed = parseZpl(source);
    if (!parsed) return std::unexpected(parsed.error().message);
    const QString error = validateParsed(*parsed);
    if (!error.isEmpty()) return std::unexpected(error);
    DesignDocument result;
    result.originalZpl = source;
    const auto& label = parsed->labels().first();
    result.width = label.width() > 0 ? label.width() : 812;
    result.height = label.height() > 0 ? label.height() : 1218;
    auto raw = [&](const QString& reason) {
        result.visualEditable = false;
        if (!result.importNotes.contains(reason)) result.importNotes.append(reason);
    };
    if (parsed->labels().size() != 1)
        raw(u"Файл содержит несколько этикеток. Выберите страницу во вкладке «Превью»; весь ZPL сохранён. Редактирование доступно во вкладке ZPL."_s);
    const bool sourceHasPlaceholders = std::ranges::any_of(label.commands(), [](const QtZpl::Command& command) {
        const auto* field = std::get_if<QtZpl::FieldData>(&command.payload);
        return field && (field->rawData.contains(u"{{") || field->rawData.contains(u"[["));
    });
    for (const auto& diagnostic : parsed->diagnostics()) raw(diagnosticText(diagnostic));
    // Label::sourceSpan ends right after ^XZ; its normalized source may still
    // carry ignored trailing text, which is outside the label.
    const auto labelSpan = label.sourceSpan();
    if (!source.first(labelSpan.start).trimmed().isEmpty()
        || !source.sliced(labelSpan.start + labelSpan.length).trimmed().isEmpty())
        raw(u"Команды вне этикетки сохранены в исходном ZPL."_s);
    // Effective font, ^BY, ^FB, ^FP and ^CI state comes from QtZpl's own
    // interpreter at each drawing command. This walk keeps only the structure
    // and the rules that decide whether a field is visually editable.
    const auto analysis = QtZpl::analyzeLabel(*parsed, 0, renderOptions(result.dpi));
    QHash<qsizetype, const QtZpl::FieldSettings*> settingsAt;
    if (analysis) {
        for (const auto& field : analysis->fields) settingsAt.insert(field.commandIndex, &field.settings);
    } else raw(u"QtZpl не проанализировал поля этикетки: %1"_s.arg(analysis.error().message));
    Element current = makeElement(ElementType::Text, {0, 0});
    bool haveOrigin = false;
    bool fieldProduced = false;
    bool reverse = false;
    bool fieldFontSet = false;
    QChar activeDirection = u'N';
    int activeJustification = 0;
    const auto justification = [](QtZpl::Justification value) {
        return value == QtZpl::Justification::Right ? 1 : value == QtZpl::Justification::Auto ? 2 : 0;
    };
    std::optional<QtZpl::Barcode> barcode;
    enum class TemplateStage { None, Separator, Origin, Data };
    TemplateStage templateStage = TemplateStage::None;
    std::optional<QString> templateText;
    int templateFields = 0;
    int visualPlaceholderFields = 0;
    qsizetype fieldStart = -1;
    qsizetype fieldElementIndex = -1;
    qsizetype previousFieldEnd = 0;
    QString directionState;
    auto append = [&](Element element, const QtZpl::FieldSettings& settings) {
        if (!haveOrigin || fieldProduced) raw(u"Поля с общим или неявным ^FO оставлены в режиме ZPL."_s);
        element.reverse = reverse;
        element.id = QUuid::createUuid();
        element.name = elementTypeName(element.type);
        // QtZpl reports whether ^FW supplies the text rotation: the parameter
        // is omitted or a later ^FW replaced it. An explicit rotation wins.
        if (element.type == ElementType::Text && settings.font.orientationOmitted) {
            const QChar direction = QLatin1Char(static_cast<char>(settings.fieldDirection));
            element.orientation = direction;
            if (direction == result.defaultOrientation)
                element.useDefaultOrientation |= direction != u'N';
            else element.useDefaultOrientation = false;
        }
        fieldElementIndex = result.elements.size();
        // Printer state restored before this field when fields are reordered:
        // ^FW as of its ^FO, which sets the default justification, and the rest
        // as QtZpl applied it here. In-field commands repeat after it.
        const auto codeState = u"^BY%1,%2,%3"_s.arg(settings.barcodeDefaults.moduleWidth)
            .arg(settings.barcodeDefaults.wideToNarrowRatio, 0, 'g', 8).arg(settings.barcodeDefaults.height);
        const QString fontOrientation = settings.font.orientationOmitted
            ? QString{} : QString(QLatin1Char(static_cast<char>(settings.font.orientation)));
        element.sourceState = directionState + u"^CI%1"_s.arg(settings.characterSet) + (isBarcodeType(element.type)
            ? codeState : u"^A0%1,%2,%3"_s.arg(fontOrientation).arg(settings.font.height).arg(settings.font.width) + codeState);
        result.elements.append(std::move(element));
        fieldProduced = true;
    };
    for (qsizetype commandIndex = 0; commandIndex < label.commands().size(); ++commandIndex) {
        const auto& command = label.commands()[commandIndex];
        const QtZpl::FieldSettings* settings = settingsAt.value(commandIndex);
        const auto unanalyzed = [&] {
            if (settings) return false;
            raw(u"QtZpl не описал поле в позиции %1; сохранён исходный ZPL."_s.arg(command.sourceSpan.start));
            return true;
        };
        // Metadata belongs to exactly one immediately following field. In
        // particular, an unrelated ^FX must never become a generic permission
        // to discard comments or reinterpret another element's placeholders.
        if (templateStage == TemplateStage::Separator
            && !std::holds_alternative<QtZpl::FieldSeparator>(command.payload))
            raw(u"Нарушена последовательность метаданных DataMatrix; сохранён исходный ZPL."_s);
        if (templateStage == TemplateStage::Origin
            && !std::holds_alternative<QtZpl::FieldOrigin>(command.payload))
            raw(u"После метаданных DataMatrix ожидается начало поля ^FO; сохранён исходный ZPL."_s);
        if (templateStage == TemplateStage::Data
            && !std::holds_alternative<QtZpl::FieldReverse>(command.payload)
            && !std::holds_alternative<QtZpl::FieldDirection>(command.payload)
            && !std::holds_alternative<QtZpl::Barcode>(command.payload)
            && !std::holds_alternative<QtZpl::FieldHex>(command.payload)
            && !std::holds_alternative<QtZpl::FieldData>(command.payload))
            raw(u"Метаданные шаблона DataMatrix не соответствуют следующему полю; сохранён исходный ZPL."_s);
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, QtZpl::FormatStart> || std::is_same_v<T, QtZpl::FormatEnd>
                          || std::is_same_v<T, QtZpl::PrintWidth> || std::is_same_v<T, QtZpl::LabelLength>
                          || std::is_same_v<T, QtZpl::FieldHex> || std::is_same_v<T, QtZpl::BarcodeDefault>) { }
            else if constexpr (std::is_same_v<T, QtZpl::Comment>) {
                const auto decoded = decodeTemplateMetadata(value.text);
                if (decoded && templateStage == TemplateStage::None && !haveOrigin && !fieldProduced && !barcode) {
                    templateText = *decoded;
                    templateStage = TemplateStage::Separator;
                    fieldStart = command.sourceSpan.start;
                } else if (haveOrigin || fieldProduced || value.text.startsWith(templateMetadataPrefix))
                    raw(u"Комментарий внутри поля или некорректные метаданные сохранены в исходном ZPL."_s);
            }
            else if constexpr (std::is_same_v<T, QtZpl::CharacterSet>) {
                if (value.id != 0 && value.id != 28) raw(u"Кодировка ^CI требует сохранения исходного ZPL."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::LabelHome>) {
                if (value.x || value.y) raw(u"Смещение ^LH сохранено в режиме ZPL."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::FieldOrigin> || std::is_same_v<T, QtZpl::FieldTypeset>) {
                if (haveOrigin) raw(u"Несколько начал ^FO в одном поле сохранены в режиме ZPL."_s);
                if (source.sliced(previousFieldEnd, command.sourceSpan.start - previousFieldEnd).contains(u"^FH", Qt::CaseInsensitive))
                    raw(u"Экранирование ^FH перед началом поля сохранено в режиме ZPL."_s);
                if (fieldStart < 0) fieldStart = command.sourceSpan.start;
                directionState = u"^FW%1,%2"_s.arg(activeDirection).arg(activeJustification);
                current.x = value.x; current.y = value.y; haveOrigin = true;
                current.baselineOrigin = std::is_same_v<T, QtZpl::FieldTypeset>;
                if constexpr (std::is_same_v<T, QtZpl::FieldTypeset>)
                    if (value.usePreviousX || value.usePreviousY)
                        raw(u"Неявные координаты ^FT зависят от предыдущего текста; сохранён исходный ZPL."_s);
                current.originJustification = value.useDefaultJustification
                    ? (activeJustification == result.defaultJustification ? -1 : activeJustification)
                    : justification(value.justification);
                if (templateStage == TemplateStage::Origin) templateStage = TemplateStage::Data;
            } else if constexpr (std::is_same_v<T, QtZpl::FieldSeparator>) {
                if (templateStage == TemplateStage::Separator) {
                    templateStage = TemplateStage::Origin;
                    return;
                }
                if (barcode) raw(u"Штрихкод без данных сохранён в режиме ZPL."_s);
                if (fieldElementIndex >= 0 && fieldStart >= 0) {
                    auto& element = result.elements[fieldElementIndex];
                    const auto prefix = source.sliced(previousFieldEnd, fieldStart - previousFieldEnd);
                    if (previousFieldEnd == 0) result.sourcePreamble = prefix;
                    else element.sourcePrefix = prefix;
                    previousFieldEnd = commandEnd(*parsed, command);
                    element.sourceZpl = source.sliced(fieldStart, previousFieldEnd - fieldStart);
                    if ((element.type == ElementType::Image || element.type == ElementType::Box
                         || element.type == ElementType::Line || element.type == ElementType::Ellipse
                         || element.type == ElementType::Circle || element.type == ElementType::Diagonal)
                        && element.sourceZpl.contains(u"^FH", Qt::CaseInsensitive))
                        raw(u"Экранирование ^FH в графическом поле сохранено в режиме ZPL."_s);
                    element.sourceSignature = serializeElement(element, true, false, false, result.defaultOrientation, result.defaultJustification);
                    result.sourceOrder.append(element.id);
                } else if (haveOrigin)
                    raw(u"Пустое поле ^FO сохранено в исходном ZPL."_s);
                haveOrigin = false; fieldProduced = false; reverse = false; fieldFontSet = false; barcode.reset();
                current.orientation = u'N';
                current.useDefaultOrientation = false;
                fieldStart = -1; fieldElementIndex = -1;
            } else if constexpr (std::is_same_v<T, QtZpl::FieldReverse>) {
                if (!haveOrigin) raw(u"Инверсия ^FR перед началом поля сохранена в режиме ZPL."_s);
                reverse = true;
            } else if constexpr (std::is_same_v<T, QtZpl::FieldBlock>) {
                if (!haveOrigin) raw(u"Блок ^FB перед началом поля сохранён в режиме ZPL."_s);
                if (value.width <= 0) raw(u"Нулевой блок ^FB сохранён в режиме ZPL."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::ScalableFont>) {
                fieldFontSet = true;
                current.orientation = QLatin1Char(static_cast<char>(value.orientation));
                current.useDefaultOrientation = value.orientationOmitted;
                if (value.font != u'0' || value.external)
                    raw(u"Визуальный MVP редактирует текст только встроенным Font 0."_s);
                if (value.external)
                    raw(u"external-font-preview: QtZpl заменяет внешний шрифт ^A@ встроенным Font 0; исходная команда сохранена."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::ChangeFont>) {
                current.orientation = u'N';
                current.useDefaultOrientation = false;
                if (value.font != u'0') raw(u"Визуальный MVP редактирует текст только встроенным Font 0."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::PrintOrientation>) {
                if (value.orientation != QtZpl::Orientation::Normal && value.orientation != QtZpl::Orientation::Inverted)
                    raw(u"Поворот ^PO поддерживает 0 или 180 градусов; исходный ZPL сохранён."_s);
                result.printInverted = value.orientation == QtZpl::Orientation::Inverted;
            } else if constexpr (std::is_same_v<T, QtZpl::PrintMirror>) {
                result.printMirror = value.enabled;
            } else if constexpr (std::is_same_v<T, QtZpl::LabelReverse>) {
                if ((haveOrigin || !result.elements.isEmpty()) && value.enabled != result.labelReverse)
                    raw(u"Изменение ^LR внутри этикетки сохранено в режиме исходного ZPL."_s);
                else result.labelReverse = value.enabled;
            } else if constexpr (std::is_same_v<T, QtZpl::FieldDirection>) {
                activeDirection = QLatin1Char(static_cast<char>(value.orientation));
                activeJustification = justification(value.justification);
                if (!haveOrigin) {
                    if (!result.elements.isEmpty() && (activeDirection != result.defaultOrientation || activeJustification != result.defaultJustification))
                        raw(u"Изменение общих значений ^FW между полями сохранено в режиме исходного ZPL."_s);
                    else { result.defaultOrientation = activeDirection; result.defaultJustification = activeJustification; }
                }
            } else if constexpr (std::is_same_v<T, QtZpl::FieldParameter>) {
                if (fieldProduced) raw(u"Команда ^FP после данных поля сохранена в режиме ZPL."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::PrintQuantity>) {
                if (value.quantity != 1 || value.pauseAndCut != 0 || value.replicates > 1 || value.overridePauseCount)
                    raw(u"Особые параметры количества печати ^PQ сохранены в режиме ZPL."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::Barcode>) {
                if (barcode) raw(u"Составная последовательность штрихкодов сохранена в режиме ZPL."_s);
                // Do not let toInt() silently normalize malformed parameters
                // when a template cannot yet be rendered for equivalence.
                const auto numeric = [&](std::initializer_list<int> positions) {
                    for (const int index : positions) {
                        if (index >= value.parameters.size() || value.parameters[index].isEmpty()) continue;
                        bool ok = false; value.parameters[index].toInt(&ok);
                        if (!ok) raw(u"Нечисловой параметр штрихкода сохранён в исходном ZPL."_s);
                    }
                };
                if (value.symbology == u"BX") numeric({1, 2, 3, 4, 5, 7});
                else if (value.symbology == u"BQ" || value.symbology == u"BF") numeric({1, 2});
                else if (value.symbology == u"B3" || value.symbology == u"BK") numeric({2});
                else if (value.symbology == u"B7") numeric({1, 2, 3, 4});
                else if (value.symbology == u"BD") numeric({0, 1, 2});
                else if (value.symbology == u"BR") numeric({1, 2, 3, 4, 5});
                else if (value.symbology == u"BO" || value.symbology == u"B0") numeric({1, 3, 5});
                else numeric({1});
                barcode = value;
            } else if constexpr (std::is_same_v<T, QtZpl::FieldData>) {
                if (unanalyzed()) return;
                Element element = current;
                element.type = ElementType::Text;
                element.text = value.data;
                element.fontHeight = settings->font.height;
                element.fontWidth = settings->font.width;
                if (const auto& block = settings->block) {
                    element.blockWidth = block->width;
                    element.blockMaxLines = block->maxLines;
                    element.blockLineSpacing = block->lineSpacing;
                    element.blockHangingIndent = block->hangingIndent;
                    element.blockJustification = QLatin1Char(static_cast<char>(block->justification));
                }
                element.textDirection = settings->parameter.direction;
                element.characterSpacing = settings->parameter.spacing;
                const auto& codeDefaults = settings->barcodeDefaults;
                if (!barcode && settings->font.font != u'0')
                    raw(u"Визуальный редактор поддерживает текст только встроенным Font 0; текущий шрифт сохранён в исходном ZPL."_s);
                if (settings->characterSet != 28 && std::ranges::any_of(value.data, [](QChar ch) { return ch.unicode() > 127; }))
                    raw(u"Не-ASCII текст без ^CI28 сохранён в исходной кодировке."_s);
                if (barcode) {
                    const auto& code = *barcode;
                    // QtZpl resolves every parameter, ^BY (with the module width a
                    // preceding ^BQ sets) and ^FW once for its renderer and for us.
                    // Designer only decides whether its model keeps the field
                    // without loss; spelling rules use the raw parameters.
                    const auto& resolved = settings->resolvedBarcode;
                    if (!resolved) raw(u"QtZpl не принимает параметры ^%1; исходный ZPL сохранён."_s.arg(code.symbology));
                    else {
                        using Symbology = QtZpl::Symbology;
                        element.orientation = QLatin1Char(static_cast<char>(resolved->orientation));
                        element.useDefaultOrientation = false;
                        // MaxiCode has fixed geometry; keep the ^BY width it is written with.
                        element.moduleWidth = resolved->moduleWidth > 0 ? resolved->moduleWidth : codeDefaults.moduleWidth;
                        element.barcodeRatio = resolved->wideToNarrowRatio;
                        switch (resolved->symbology) {
                        case Symbology::Code128: {
                            const auto& options = std::get<QtZpl::Code128Options>(resolved->options);
                            element.type = ElementType::Code128;
                            element.height = resolved->height;
                            element.humanReadable = resolved->interpretation;
                            element.interpretationAbove = resolved->interpretationAbove;
                            element.code128CheckDigit = options.uccCheckDigit;
                            if (fieldFontSet)
                                raw(u"Явный шрифт подписи Code 128 сохранён в режиме ZPL."_s);
                            if (!QStringView(u"NABCDU").contains(options.mode) || code.parameters.value(5).size() > 1
                                || code.parameters.size() > 6)
                                raw(u"Особый режим Code 128 сохранён в исходном ZPL."_s);
                            else element.code128Mode = options.mode;
                            break;
                        }
                        case Symbology::QrCode:
                            element.type = ElementType::QrCode;
                            // The serializer writes ^BY…,0, which QtZpl applies as a
                            // one-dot ^FO margin, like Labelary.
                            if (element.text.size() < 3 || !QStringView{u"LMQH"}.contains(element.text.front())
                                || element.text.sliced(1, 2) != u"A," || codeDefaults.height != 1 || code.parameters.size() > 3)
                                raw(u"Параметры QR-кода сохранены в режиме ZPL."_s);
                            else { element.qrErrorCorrection = element.text.front(); element.text.remove(0, 3); }
                            break;
                        case Symbology::Ean13: case Symbology::Ean8: case Symbology::UpcA: case Symbology::UpcE:
                        case Symbology::Code39: case Symbology::Codabar: case Symbology::Code93:
                        case Symbology::Interleaved2of5: case Symbology::Industrial2of5: {
                            const auto symbology = resolved->symbology;
                            element.type = symbology == Symbology::Ean13 ? ElementType::Ean13
                                : symbology == Symbology::Ean8 ? ElementType::Ean8 : symbology == Symbology::UpcA ? ElementType::UpcA
                                : symbology == Symbology::UpcE ? ElementType::UpcE : symbology == Symbology::Code39 ? ElementType::Code39
                                : symbology == Symbology::Codabar ? ElementType::Codabar : symbology == Symbology::Code93 ? ElementType::Code93
                                : symbology == Symbology::Interleaved2of5 ? ElementType::Interleaved2of5 : ElementType::Industrial2of5;
                            element.height = resolved->height;
                            element.humanReadable = resolved->interpretation;
                            element.interpretationAbove = resolved->interpretationAbove;
                            const auto& options = std::get<QtZpl::LinearOptions>(resolved->options);
                            const bool codabar = element.type == ElementType::Codabar;
                            const bool extra = element.type == ElementType::Code39 || codabar;
                            const bool check = element.type == ElementType::Code93 || element.type == ElementType::Interleaved2of5;
                            element.barcodeCheckDigit = check && options.checkDigit;
                            // The model has no Code 39/Codabar check digit, and the
                            // serializer omits UPC's e. Captions QtZpl cannot draw
                            // and Codabar data characters a printer drops are
                            // rejected by validateElement below.
                            if ((extra && options.checkDigit) || code.parameters.size() > (codabar ? 7 : extra || check ? 5 : 4) || fieldFontSet)
                                raw(u"Особые параметры штрихкода сохранены в исходном ZPL."_s);
                            if (codabar) {
                                // ^BK k and l in any case, A when omitted. QtZpl also
                                // reads another character as A, as Labelary does;
                                // keep that spelling in source mode.
                                for (const qsizetype index : {5, 6}) {
                                    const auto character = code.parameters.value(index);
                                    if (!character.isEmpty() && (character.size() != 1 || !QStringView{u"ABCD"}.contains(character.front().toUpper())))
                                        raw(u"Недопустимые старт/стоп Codabar в ^BK сохранены в исходном ZPL."_s);
                                }
                                element.codabarStart = options.start;
                                element.codabarStop = options.stop;
                            }
                            break;
                        }
                        case Symbology::Pdf417: {
                            const auto& options = std::get<QtZpl::Pdf417Options>(resolved->options);
                            element.type = ElementType::Pdf417;
                            element.pdf417RowHeight = resolved->height;
                            element.pdf417Security = options.security;
                            element.pdf417Columns = options.columns;
                            element.pdf417Rows = options.rows;
                            element.pdf417Truncated = options.truncated;
                            element.humanReadable = false;
                            if (code.parameters.size() > 6) raw(u"Особые параметры PDF417 сохранены в исходном ZPL."_s);
                            break;
                        }
                        case Symbology::DataBar: {
                            const auto& options = std::get<QtZpl::DataBarOptions>(resolved->options);
                            element.type = ElementType::Gs1DataBar;
                            element.humanReadable = false;
                            element.dataBarType = options.type;
                            element.dataBarSeparator = options.separatorHeight;
                            element.height = resolved->height;
                            element.dataBarSegments = options.segments;
                            if (code.parameters.size() > 6) raw(u"Неизвестные параметры GS1 DataBar сохранены в ZPL."_s);
                            break;
                        }
                        case Symbology::MicroPdf417:
                            element.type = ElementType::MicroPdf417;
                            element.humanReadable = false;
                            element.pdf417RowHeight = resolved->height;
                            element.microPdf417Mode = std::get<QtZpl::MicroPdf417Options>(resolved->options).mode;
                            if (code.parameters.size() > 3) raw(u"Неизвестные параметры MicroPDF417 сохранены в ZPL."_s);
                            break;
                        case Symbology::MaxiCode: {
                            const auto& options = std::get<QtZpl::MaxiCodeOptions>(resolved->options);
                            element.type = ElementType::MaxiCode;
                            element.humanReadable = false;
                            element.maxiCodeMode = options.mode;
                            element.maxiCodePosition = options.position;
                            element.maxiCodeTotal = options.total;
                            element.width = 201; element.height = 195;
                            if (code.parameters.size() > 3) raw(u"Неизвестные параметры MaxiCode сохранены в ZPL."_s);
                            break;
                        }
                        case Symbology::Aztec: {
                            const auto& options = std::get<QtZpl::AztecOptions>(resolved->options);
                            element.type = ElementType::Aztec;
                            element.humanReadable = false;
                            element.aztecSize = options.size;
                            element.aztecReaderInit = options.readerInitialization;
                            if (options.eci || options.structuredAppend != 1 || code.parameters.size() > 6)
                                raw(u"Aztec ECI/structured append не поддерживается; исходный ZPL сохранён."_s);
                            break;
                        }
                        case Symbology::DataMatrix: {
                            const auto& options = std::get<QtZpl::DataMatrixOptions>(resolved->options);
                            element.type = ElementType::DataMatrix;
                            element.dataMatrixColumns = options.columns;
                            element.dataMatrixRows = options.rows;
                            element.dataMatrixRectangular = options.rectangular;
                            if (options.format < 0 || options.format > 6 || code.parameters.size() > 8)
                                raw(u"Особый формат DataMatrix сохранён в режиме ZPL."_s);
                            if (code.parameters.value(6).size() > 1) raw(u"Параметры экранирования DataMatrix сохранены в режиме ZPL."_s);
                            else {
                                const QChar marker = options.escape;
                                QString decoded;
                                const bool leadingFnc1 = element.text.startsWith(QString(marker) + u'1');
                                element.gs1 = leadingFnc1;
                                if (leadingFnc1 != options.gs1)
                                    raw(u"Режим GS1 DataMatrix отличается от поддерживаемого QtZpl; исходный ZPL сохранён."_s);
                                // ^FH decoding can reinterpret an underscore plus
                                // two hex letters inside a placeholder's name. For
                                // our metadata-backed template, compare the raw
                                // serialized field below instead of trusting that
                                // unresolved parser payload as concrete code data.
                                for (qsizetype i = 0; !templateText && i < element.text.size(); ++i) {
                                    if (element.text[i] == marker && i + 1 < element.text.size()) {
                                        if (element.text[i + 1] == marker) { decoded += marker; ++i; }
                                        else if (element.gs1 && element.text[i + 1] == u'1') {
                                            if (i != 0) decoded += QChar(0x1d);
                                            ++i;
                                        }
                                        else if (element.gs1 && QStringView(element.text).sliced(i + 1).startsWith(u"d029")) {
                                            decoded += QChar(0x1d); i += 4;
                                        }
                                        else { raw(u"Специальные последовательности DataMatrix сохранены в режиме ZPL."_s); decoded += element.text[i]; }
                                    } else decoded += element.text[i];
                                }
                                if (templateText && templateStage == TemplateStage::Data) {
                                    element.text = *templateText;
                                    if (element.gs1) {
                                        const auto gs1 = parseGs1Template(element.text);
                                        const auto tokens = valuePlaceholders(element.text);
                                        if (!gs1 || !tokens || tokens->isEmpty())
                                            raw(u"Некорректные метаданные шаблона GS1 DataMatrix; сохранён исходный ZPL."_s);
                                        else element.text = gs1->canonicalText;
                                    }
                                    ++templateFields;
                                } else if (element.gs1) {
                                    // QtZpl suppresses repeated leading |1 escapes,
                                    // but a printer can encode each FNC1. Do not let
                                    // renderer equality authorize that lossy import
                                    // (canary: repeatedLeadingFnc1StillCollapses).
                                    if (decoded.startsWith(QChar(0x1d)))
                                        raw(u"Повторный начальный FNC1/GS сохранён в исходном ZPL."_s);
                                    const auto gs1 = parseGs1(decoded, false);
                                    if (!gs1) raw(gs1.error());
                                    else element.text = gs1->canonicalText;
                                } else element.text = decoded;
                            }
                            break;
                        }
                        case Symbology::Unknown:
                            raw(u"Этот тип штрихкода пока доступен только в ZPL."_s);
                            break;
                        }
                    }
                    barcode.reset();
                }
                if (templateText) {
                    if (templateStage != TemplateStage::Data || element.type != ElementType::DataMatrix)
                        raw(u"Метаданные шаблона относятся только к следующему DataMatrix; сохранён исходный ZPL."_s);
                    templateText.reset();
                    templateStage = TemplateStage::None;
                } else if (element.text.contains(u"{{") || element.text.contains(u"[[")
                           || element.text.contains(u"}}") || element.text.contains(u"]]")) {
                    if (element.type == ElementType::DataMatrix)
                        raw(u"Неоднозначные переменные DataMatrix сохранены в режиме исходного ZPL."_s);
                    else {
                        element.literalText = !value.rawData.contains(u"{{") && !value.rawData.contains(u"[[")
                            && !value.rawData.contains(u"}}") && !value.rawData.contains(u"]]");
                        if (!element.literalText) {
                            const auto tokens = valuePlaceholders(element.text);
                            if (!tokens) raw(tokens.error());
                            else if (!tokens->isEmpty()) ++visualPlaceholderFields;
                        }
                    }
                }
                append(element, *settings);
            } else if constexpr (std::is_same_v<T, QtZpl::GraphicBox> || std::is_same_v<T, QtZpl::GraphicEllipse>) {
                if (unanalyzed()) return;
                Element element = current;
                element.type = std::is_same_v<T, QtZpl::GraphicBox> ? ElementType::Box : ElementType::Ellipse;
                element.width = std::max(value.width, value.thickness);
                element.height = std::max(value.height, value.thickness);
                element.thickness = value.thickness;
                if constexpr (std::is_same_v<T, QtZpl::GraphicBox>) {
                    if (element.width <= element.thickness || element.height <= element.thickness) element.type = ElementType::Line;
                    if (value.cornerRounding != 0) raw(u"Скругление прямоугольника сохранено в режиме ZPL."_s);
                }
                element.graphicWhite = value.color == QtZpl::LineColor::White;
                append(element, *settings);
            } else if constexpr (std::is_same_v<T, QtZpl::GraphicCircle>) {
                if (unanalyzed()) return;
                Element element = current;
                element.type = ElementType::Circle; element.width = element.height = value.diameter;
                element.thickness = value.thickness;
                element.graphicWhite = value.color == QtZpl::LineColor::White;
                append(element, *settings);
            } else if constexpr (std::is_same_v<T, QtZpl::GraphicDiagonal>) {
                if (unanalyzed()) return;
                Element element = current;
                element.type = ElementType::Diagonal;
                element.width = value.width; element.height = value.height;
                element.thickness = value.thickness;
                element.graphicWhite = value.color == QtZpl::LineColor::White;
                element.diagonalDirection = value.orientation;
                append(element, *settings);
            } else if constexpr (std::is_same_v<T, QtZpl::GraphicField>) {
                if (unanalyzed()) return;
                Element element = current;
                element.type = ElementType::Image;
                // QtZpl owns graphic decoding (ASCII repeats, Z64 and binary),
                // shared with its renderer. Decode the parsed payload directly
                // and retain the original token spelling until the image changes.
                QtZpl::GraphicDecodeOptions options;
                options.maxDecodedBytes = maximumPixels / 8;
                const auto decoded = QtZpl::decodeGraphic(value, options);
                if (!decoded) {
                    raw(u"%1: %2"_s.arg(decoded.error().code, decoded.error().message)); return;
                }
                QImage image(decoded->size, QImage::Format_Grayscale8);
                image.fill(255);
                const auto* bits = reinterpret_cast<const uchar*>(decoded->bytes.constData());
                for (int y = 0; y < image.height(); ++y) {
                    const uchar* row = bits + qsizetype(y) * decoded->bytesPerRow;
                    uchar* pixels = image.scanLine(y);
                    for (int x = 0; x < image.width(); ++x)
                        if (row[x / 8] & (0x80U >> (x % 8))) pixels[x] = 0;
                }
                element.width = image.width();
                element.height = image.height();
                element.image = std::move(image);
                element.threshold = 128; element.dither = false;
                if (reverse) raw(u"Инверсия ^GF сохранена в режиме ZPL."_s);
                append(element, *settings);
            } else raw(u"Команда %1 сохранена в исходном ZPL; визуальное редактирование отключено."_s
                .arg(parsed->sourceText(command.sourceSpan).left(3)));
        }, command.payload);
    }
    if (templateStage != TemplateStage::None || templateText)
        raw(u"Незавершённые метаданные шаблона DataMatrix сохранены в исходном ZPL."_s);
    if (fieldProduced || haveOrigin)
        raw(u"Поле без завершающей ^FS сохранено в исходном ZPL."_s);
    if (previousFieldEnd > 0) {
        result.sourceEpilogue = source.sliced(previousFieldEnd);
    } else if (result.elements.isEmpty()) {
        const qsizetype end = label.commands().last().sourceSpan.start;
        result.sourcePreamble = source.first(end);
        result.sourceEpilogue = source.sliced(end);
    }
    result.sourceWidth = result.width;
    result.sourceHeight = result.height;
    result.sourceSettingsSignature = labelSettings(result);
    if (!restoreEditorMetadata(result, source))
        raw(u"Метаданные объектов повреждены или не соответствуют ZPL. Файл сохранён в режиме исходного ZPL."_s);
    if (result.elements.size() > maximumElements)
        raw(u"Файл содержит более 200 объектов; сохранён исходный ZPL."_s);
    if (sourceHasPlaceholders && templateFields == 0 && (visualPlaceholderFields == 0 || !result.visualEditable))
        raw(u"Шаблон содержит неподдерживаемые переменные {{…}} / [[…]]; сохранён исходный ZPL."_s);
    if (result.visualEditable) {
        for (const auto& element : result.elements) {
            // Non-printing objects were structurally checked by the snapshot
            // decoder; incomplete barcode input must remain editable there.
            if (!element.printable) continue;
            const auto invalid = validateElement(element, true, result.dpi);
            if (!invalid.isEmpty()) raw(invalid);
        }
    }
    // Never expose a lossy visual import. Keep even known commands in raw mode
    // if their original state lifetime differs from the editable model.
    if (result.visualEditable) {
        auto normalized = result;
        normalized.originalZpl.clear();
        normalized.sourcePreamble.clear();
        if (templateFields > 0) {
            // Metadata must describe this exact field, including its printer
            // data. Comments and unrelated fields elsewhere may now be retained
            // verbatim, so an entire-file serializer match is too restrictive.
            for (const auto& element : result.elements) {
                if (!element.sourceZpl.startsWith(u"^FX"_s + templateMetadataPrefix.toString())) continue;
                bool match = false;
                for (const bool legacyTemplates : {false, true})
                    for (const bool legacyConcrete : {false, true}) {
                        auto candidate = serializeElement(element, true, legacyTemplates, legacyConcrete, result.defaultOrientation, result.defaultJustification);
                        if (candidate.endsWith(u'\n')) candidate.chop(1);
                        match |= candidate == element.sourceZpl;
                    }
                if (!match)
                    raw(u"Данные или структура шаблона DataMatrix отличаются от метаданных. Файл оставлен в режиме ZPL без потерь."_s);
            }
        } else if (visualPlaceholderFields == 0) {
            const auto original = checkedRender(source, result.dpi);
            const auto regenerated = checkedRender(toZpl(normalized), result.dpi);
            if (!original || !regenerated || original->labels.isEmpty() || regenerated->labels.isEmpty()
                || original->labels.first() != regenerated->labels.first())
                raw(u"Результат визуального импорта отличается от исходника. Файл оставлен в режиме ZPL без потерь."_s);
            else for (const auto& diagnostic : original->diagnostics)
                if (diagnostic.severity == QtZpl::Severity::Error) raw(diagnosticText(diagnostic));
        }
    }
    if (!result.visualEditable) result.elements.clear();
    return result;
}

} // namespace Designer
