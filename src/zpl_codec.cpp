#include "design_document.hpp"
#include "design_internal.hpp"
#include "gs1_data.hpp"
#include "value_placeholders.hpp"
#include "editor_metadata.hpp"

#include <algorithm>
#include <type_traits>
#include <optional>
#include <initializer_list>
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

bool isByteEscapedField(QStringView source)
{
    const QStringView data = source.sliced(3);
    if (data.isEmpty() || data.size() % 3 != 0) return false;
    for (qsizetype i = 0; i < data.size(); i += 3) {
        if (data[i] != u'_') return false;
        for (int digit = 1; digit <= 2; ++digit) {
            const ushort ch = data[i + digit].toUpper().unicode();
            if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return false;
        }
    }
    return true;
}

QString parameter(const QtZpl::Barcode& code, qsizetype index, QString fallback = {})
{
    return index < code.parameters.size() && !code.parameters[index].isEmpty() ? code.parameters[index] : fallback;
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
    const auto parsed = QtZpl::parse(source);
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
            // Command::source removes CR/LF while offsets refer to the actual
            // input. Keep original trailing whitespace and consume any line
            // breaks inside parameters without leaving their final character.
            qsizetype end = index + 1 < commands.size() ? commands[index + 1].offset : source.size();
            while (end > command.offset + 3 && source[end - 1].isSpace()) --end;
            replacements.append({command.offset, end - command.offset,
                std::holds_alternative<QtZpl::FieldDirection>(command.payload)
                    ? u"^FW%1,%2"_s.arg(document.defaultOrientation).arg(document.defaultJustification) : QString{}});
        }
    }
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it) source.replace(it->offset, it->length, it->text);
    source.insert(parsed->labels().first().commands().first().offset + 3, labelSettings(document, true));
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
    // QtZpl treats normal ^A/^B orientation as inherited ^FW. A local reset
    // gives an explicitly normal object predictable geometry under rotated FW.
    const bool resetDirection = !element.useDefaultOrientation && element.orientation == u'N'
        && defaultOrientation != u'N' && (element.type == ElementType::Text || isBarcodeType(element.type));
    if (resetDirection) result += u"^FWN,%1"_s.arg(defaultJustification);
    if (element.reverse && element.type != ElementType::Image) result += u"^FR"_s;
    switch (element.type) {
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
            result += u"^BK%1,N,%2,N,N"_s.arg(element.orientation).arg(element.height);
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
    if (resetDirection) result += u"^FW%1,%2"_s.arg(defaultOrientation).arg(defaultJustification);
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
            const auto parsed = QtZpl::parse(source);
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
                    if (!replacement.isEmpty()) {
                        qsizetype end = it->offset + 3;
                        while (end < source.size() && source[end] != u'^' && source[end] != u'~') ++end;
                        while (end > it->offset + 3 && source[end - 1].isSpace()) --end;
                        source.replace(it->offset, end - it->offset, replacement);
                    }
                }
                QString missing;
                if (!hasWidth) missing += u"^PW%1"_s.arg(document.width);
                if (!hasHeight) missing += u"^LL%1"_s.arg(document.height);
                source.insert(commands.first().offset + 3, missing);
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
    const auto parsed = QtZpl::parse(source);
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
        return std::holds_alternative<QtZpl::FieldData>(command.payload)
            && (command.source.contains(u"{{") || command.source.contains(u"[["));
    });
    for (const auto& diagnostic : parsed->diagnostics()) raw(diagnosticText(diagnostic));
    if (!source.left(label.commands().first().offset).trimmed().isEmpty()
        || !source.mid(label.commands().last().offset + label.commands().last().source.size()).trimmed().isEmpty())
        raw(u"Команды вне этикетки сохранены в исходном ZPL."_s);
    Element current = makeElement(ElementType::Text, {0, 0});
    QChar currentFont = u'A';
    QChar defaultFont = u'A';
    int defaultFontHeight = 9;
    int defaultFontWidth = 5;
    current.fontHeight = defaultFontHeight;
    current.fontWidth = defaultFontWidth;
    int charset = 0;
    int module = 2;
    double barcodeRatio = 3.0;
    int barcodeHeight = 10;
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
    QString fieldSourceState;
    QString fieldBarcodeState;
    auto append = [&](Element element) {
        if (!haveOrigin || fieldProduced) raw(u"Поля с общим или неявным ^FO оставлены в режиме ZPL."_s);
        element.reverse = reverse;
        element.id = QUuid::createUuid();
        element.name = elementTypeName(element.type);
        if (element.type == ElementType::Text && element.orientation == u'N') {
            if (activeDirection == result.defaultOrientation)
                element.useDefaultOrientation |= activeDirection != u'N';
            else {
                element.orientation = activeDirection;
                element.useDefaultOrientation = false;
            }
        }
        fieldElementIndex = result.elements.size();
        element.sourceState = isBarcodeType(element.type) ? fieldBarcodeState : fieldSourceState;
        result.elements.append(std::move(element));
        fieldProduced = true;
    };
    for (const auto& command : label.commands()) {
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
            && !std::holds_alternative<QtZpl::FieldData>(command.payload))
            raw(u"Метаданные шаблона DataMatrix не соответствуют следующему полю; сохранён исходный ZPL."_s);
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, QtZpl::FormatStart> || std::is_same_v<T, QtZpl::FormatEnd>
                          || std::is_same_v<T, QtZpl::PrintWidth> || std::is_same_v<T, QtZpl::LabelLength>) { }
            else if constexpr (std::is_same_v<T, QtZpl::Comment>) {
                const auto decoded = decodeTemplateMetadata(value.text);
                if (decoded && templateStage == TemplateStage::None && !haveOrigin && !fieldProduced && !barcode) {
                    templateText = *decoded;
                    templateStage = TemplateStage::Separator;
                    fieldStart = command.offset;
                } else if (haveOrigin || fieldProduced || value.text.startsWith(templateMetadataPrefix))
                    raw(u"Комментарий внутри поля или некорректные метаданные сохранены в исходном ZPL."_s);
            }
            else if constexpr (std::is_same_v<T, QtZpl::CharacterSet>) {
                charset = value.id;
                if (charset != 0 && charset != 28) raw(u"Кодировка ^CI требует сохранения исходного ZPL."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::LabelHome>) {
                if (value.x || value.y) raw(u"Смещение ^LH сохранено в режиме ZPL."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::FieldOrigin> || std::is_same_v<T, QtZpl::FieldTypeset>) {
                if (haveOrigin) raw(u"Несколько начал ^FO в одном поле сохранены в режиме ZPL."_s);
                if (source.sliced(previousFieldEnd, command.offset - previousFieldEnd).contains(u"^FH", Qt::CaseInsensitive))
                    raw(u"Экранирование ^FH перед началом поля сохранено в режиме ZPL."_s);
                if (fieldStart < 0) fieldStart = command.offset;
                const auto directionState = u"^FW%1,%2"_s.arg(activeDirection).arg(activeJustification);
                fieldSourceState = directionState + u"^CI%1^A0%2,%3,%4^BY%5,%6,%7"_s.arg(charset).arg(current.orientation)
                    .arg(current.fontHeight).arg(current.fontWidth).arg(module).arg(barcodeRatio, 0, 'g', 8).arg(barcodeHeight);
                fieldBarcodeState = directionState + u"^CI%1^BY%2,%3,%4"_s.arg(charset).arg(module)
                    .arg(barcodeRatio, 0, 'g', 8).arg(barcodeHeight);
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
                    previousFieldEnd = command.offset + 3;
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
                currentFont = defaultFont;
                current.fontHeight = defaultFontHeight;
                current.fontWidth = defaultFontWidth;
                current.orientation = u'N';
                current.useDefaultOrientation = false;
                current.blockWidth = 0;
                current.blockMaxLines = 1;
                current.blockLineSpacing = 0;
                current.blockHangingIndent = 0;
                current.blockJustification = u'L';
                current.textDirection = u'H'; current.characterSpacing = 0;
                fieldStart = -1; fieldElementIndex = -1;
            } else if constexpr (std::is_same_v<T, QtZpl::FieldReverse>) {
                if (!haveOrigin) raw(u"Инверсия ^FR перед началом поля сохранена в режиме ZPL."_s);
                reverse = true;
            } else if constexpr (std::is_same_v<T, QtZpl::FieldBlock>) {
                if (!haveOrigin) raw(u"Блок ^FB перед началом поля сохранён в режиме ZPL."_s);
                current.blockWidth = value.width;
                current.blockMaxLines = value.maxLines;
                current.blockLineSpacing = value.lineSpacing;
                current.blockHangingIndent = value.hangingIndent;
                current.blockJustification = QLatin1Char(static_cast<char>(value.justification));
                if (value.width <= 0) raw(u"Нулевой блок ^FB сохранён в режиме ZPL."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::ScalableFont>) {
                fieldFontSet = true;
                currentFont = value.font;
                current.fontHeight = value.height; current.fontWidth = value.width;
                current.orientation = QLatin1Char(static_cast<char>(value.orientation));
                current.useDefaultOrientation = command.source.size() > 3 && command.source[3] == u',';
                if (value.font != u'0' || command.source.startsWith(u"^A@"))
                    raw(u"Визуальный MVP редактирует текст только встроенным Font 0."_s);
                if (command.source.startsWith(u"^A@"))
                    raw(u"external-font-preview: QtZpl заменяет внешний шрифт ^A@ встроенным Font 0; исходная команда сохранена."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::ChangeFont>) {
                currentFont = defaultFont = value.font;
                current.fontHeight = defaultFontHeight = value.height;
                current.fontWidth = defaultFontWidth = value.width;
                current.orientation = u'N';
                current.useDefaultOrientation = false;
                if (value.font != u'0') raw(u"Визуальный MVP редактирует текст только встроенным Font 0."_s);
            } else if constexpr (std::is_same_v<T, QtZpl::BarcodeDefault>) {
                module = value.moduleWidth; barcodeHeight = value.height;
                barcodeRatio = value.wideToNarrowRatio;
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
                current.textDirection = value.direction;
                current.characterSpacing = value.spacing;
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
                Element element = current;
                element.type = ElementType::Text;
                element.text = value.data;
                if (!barcode && currentFont != u'0')
                    raw(u"Визуальный редактор поддерживает текст только встроенным Font 0; текущий шрифт сохранён в исходном ZPL."_s);
                if (charset != 28 && std::ranges::any_of(value.data, [](QChar ch) { return ch.unicode() > 127; }))
                    raw(u"Не-ASCII текст без ^CI28 сохранён в исходной кодировке."_s);
                if (barcode) {
                    const auto& code = *barcode;
                    element.orientation = QLatin1Char(static_cast<char>(code.orientation));
                    if (element.orientation == u'N') element.orientation = activeDirection;
                    element.useDefaultOrientation = false;
                    element.moduleWidth = module;
                    element.barcodeRatio = barcodeRatio;
                    if (code.symbology == u"BC") {
                        element.type = ElementType::Code128;
                        element.height = parameter(code, 1, QString::number(barcodeHeight)).toInt();
                        element.humanReadable = parameter(code, 2, u"Y"_s) != u"N";
                        if (fieldFontSet)
                            raw(u"Явный шрифт подписи Code 128 сохранён в режиме ZPL."_s);
                        element.interpretationAbove = parameter(code, 3, u"N"_s) == u"Y";
                        element.code128CheckDigit = parameter(code, 4, u"N"_s) == u"Y";
                        const auto mode = parameter(code, 5, u"N"_s);
                        if (mode.size() != 1 || !QStringView(u"NABCDU").contains(mode.front()) || code.parameters.size() > 6)
                            raw(u"Особый режим Code 128 сохранён в исходном ZPL."_s);
                        else element.code128Mode = mode.front();
                    } else if (code.symbology == u"BQ") {
                        element.type = ElementType::QrCode;
                        element.moduleWidth = parameter(code, 2, u"2"_s).toInt();
                        if (parameter(code, 1, u"2"_s) != u"2" || element.text.size() < 3
                            || !QStringView{u"LMQH"}.contains(element.text.front()) || element.text.sliced(1, 2) != u"A,"
                            || barcodeHeight != 0 || code.parameters.size() > 3)
                            raw(u"Параметры QR-кода сохранены в режиме ZPL."_s);
                        else { element.qrErrorCorrection = element.text.front(); element.text.remove(0, 3); }
                    } else if (code.symbology == u"BE" || code.symbology == u"B8" || code.symbology == u"BU"
                               || code.symbology == u"B9" || code.symbology == u"B3" || code.symbology == u"BK"
                               || code.symbology == u"B7" || code.symbology == u"BA" || code.symbology == u"B2" || code.symbology == u"BI") {
                        element.type = code.symbology == u"BE" ? ElementType::Ean13 : code.symbology == u"B8" ? ElementType::Ean8
                            : code.symbology == u"BU" ? ElementType::UpcA : code.symbology == u"B9" ? ElementType::UpcE
                            : code.symbology == u"B3" ? ElementType::Code39 : code.symbology == u"BK" ? ElementType::Codabar
                            : code.symbology == u"BA" ? ElementType::Code93 : code.symbology == u"B2" ? ElementType::Interleaved2of5
                            : code.symbology == u"BI" ? ElementType::Industrial2of5 : ElementType::Pdf417;
                        if (element.type == ElementType::Pdf417) {
                            element.pdf417RowHeight = parameter(code, 1, QString::number(module * 3)).toInt();
                            element.pdf417Security = parameter(code, 2, u"0"_s).toInt();
                            element.pdf417Columns = parameter(code, 3, u"0"_s).toInt();
                            element.pdf417Rows = parameter(code, 4, u"0"_s).toInt();
                            element.pdf417Truncated = parameter(code, 5, u"N"_s) == u"Y";
                            element.humanReadable = false;
                            if ((parameter(code, 5, u"N"_s) != u"N" && parameter(code, 5) != u"Y") || code.parameters.size() > 6)
                                raw(u"Особые параметры PDF417 сохранены в исходном ZPL."_s);
                        } else {
                            const bool extra = element.type == ElementType::Code39 || element.type == ElementType::Codabar;
                            element.height = parameter(code, extra ? 2 : 1, QString::number(barcodeHeight)).toInt();
                            element.humanReadable = parameter(code, extra ? 3 : 2, u"Y"_s) != u"N";
                            element.interpretationAbove = parameter(code, extra ? 4 : 3, u"N"_s) == u"Y";
                            const bool check = element.type == ElementType::Code93 || element.type == ElementType::Interleaved2of5;
                            element.barcodeCheckDigit = check && parameter(code, 4, u"N"_s) == u"Y";
                            if ((extra && parameter(code, 1, u"N"_s) != u"N") || code.parameters.size() > (extra || check ? 5 : 4)
                                || fieldFontSet || ((element.type == ElementType::Codabar || element.type == ElementType::Code39)
                                    && (element.humanReadable || element.interpretationAbove)))
                                raw(u"Особые параметры штрихкода сохранены в исходном ZPL."_s);
                        }
                    } else if (code.symbology == u"BR" || code.symbology == u"BF" || code.symbology == u"BO"
                               || code.symbology == u"B0" || code.symbology == u"BD") {
                        element.humanReadable = false;
                        if (code.symbology == u"BR") {
                            element.type = ElementType::Gs1DataBar;
                            element.dataBarType = parameter(code, 1, u"1"_s).toInt();
                            element.moduleWidth = parameter(code, 2, u"2"_s).toInt();
                            element.dataBarSeparator = parameter(code, 3, u"1"_s).toInt();
                            element.height = parameter(code, 4, u"25"_s).toInt();
                            element.dataBarSegments = parameter(code, 5, u"0"_s).toInt();
                            if (code.parameters.size() > 6) raw(u"Неизвестные параметры GS1 DataBar сохранены в ZPL."_s);
                        } else if (code.symbology == u"BF") {
                            element.type = ElementType::MicroPdf417;
                            element.pdf417RowHeight = parameter(code, 1, u"1"_s).toInt();
                            element.microPdf417Mode = parameter(code, 2, u"0"_s).toInt();
                            if (code.parameters.size() > 3) raw(u"Неизвестные параметры MicroPDF417 сохранены в ZPL."_s);
                        } else if (code.symbology == u"BD") {
                            element.type = ElementType::MaxiCode;
                            element.maxiCodeMode = parameter(code, 0, u"2"_s).toInt();
                            element.maxiCodePosition = parameter(code, 1, u"1"_s).toInt();
                            element.maxiCodeTotal = parameter(code, 2, u"1"_s).toInt();
                            element.width = 201; element.height = 195;
                            if (code.parameters.size() > 3) raw(u"Неизвестные параметры MaxiCode сохранены в ZPL."_s);
                        } else {
                            element.type = ElementType::Aztec;
                            element.moduleWidth = parameter(code, 1, u"2"_s).toInt();
                            element.aztecSize = parameter(code, 3, u"23"_s).toInt();
                            element.aztecReaderInit = parameter(code, 4, u"N"_s) == u"Y";
                            if (parameter(code, 2, u"N"_s) != u"N" || parameter(code, 5, u"1"_s) != u"1" || code.parameters.size() > 6)
                                raw(u"Aztec ECI/structured append не поддерживается; исходный ZPL сохранён."_s);
                        }
                    } else if (code.symbology == u"BX") {
                        element.type = ElementType::DataMatrix;
                        if (charset == 28 && isByteEscapedField(command.source))
                            element.text = QString::fromUtf8(element.text.toLatin1());
                        element.moduleWidth = parameter(code, 1, u"3"_s).toInt();
                        element.dataMatrixColumns = parameter(code, 3, u"0"_s).toInt();
                        element.dataMatrixRows = parameter(code, 4, u"0"_s).toInt();
                        element.dataMatrixRectangular = parameter(code, 7, u"1"_s) == u"2";
                        if (parameter(code, 2, u"200"_s) != u"200"
                            || parameter(code, 5, u"0"_s).toInt() < 0 || parameter(code, 5, u"0"_s).toInt() > 6
                            || (parameter(code, 7, u"1"_s) != u"1" && parameter(code, 7) != u"2") || code.parameters.size() > 8)
                            raw(u"Особый формат DataMatrix сохранён в режиме ZPL."_s);
                        const QString marker = parameter(code, 6, u"~"_s);
                        if (marker.size() != 1) raw(u"Параметры экранирования DataMatrix сохранены в режиме ZPL."_s);
                        else {
                            QString decoded;
                            const bool leadingFnc1 = element.text.startsWith(marker + u'1');
                            const bool libraryGs1 = parameter(code, 5, u"0"_s) == u"1";
                            element.gs1 = leadingFnc1;
                            if (leadingFnc1 != libraryGs1)
                                raw(u"Режим GS1 DataMatrix отличается от поддерживаемого QtZpl; исходный ZPL сохранён."_s);
                            // ^FH decoding can reinterpret an underscore plus
                            // two hex letters inside a placeholder's name. For
                            // our metadata-backed template, compare the raw
                            // serialized field below instead of trusting that
                            // unresolved parser payload as concrete code data.
                            for (qsizetype i = 0; !templateText && i < element.text.size(); ++i) {
                                if (element.text[i] == marker.front() && i + 1 < element.text.size()) {
                                    if (element.text[i + 1] == marker.front()) { decoded += marker; ++i; }
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
                                // renderer equality authorize that lossy import.
                                if (decoded.startsWith(QChar(0x1d)))
                                    raw(u"Повторный начальный FNC1/GS сохранён в исходном ZPL."_s);
                                const auto gs1 = parseGs1(decoded, false);
                                if (!gs1) raw(gs1.error());
                                else element.text = gs1->canonicalText;
                            } else element.text = decoded;
                        }
                    } else raw(u"Этот тип штрихкода пока доступен только в ZPL."_s);
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
                        element.literalText = !command.source.contains(u"{{") && !command.source.contains(u"[[")
                            && !command.source.contains(u"}}") && !command.source.contains(u"]]");
                        if (!element.literalText) {
                            const auto tokens = valuePlaceholders(element.text);
                            if (!tokens) raw(tokens.error());
                            else if (!tokens->isEmpty()) ++visualPlaceholderFields;
                        }
                    }
                }
                append(element);
            } else if constexpr (std::is_same_v<T, QtZpl::GraphicBox> || std::is_same_v<T, QtZpl::GraphicEllipse>) {
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
                append(element);
            } else if constexpr (std::is_same_v<T, QtZpl::GraphicCircle>) {
                Element element = current;
                element.type = ElementType::Circle; element.width = element.height = value.diameter;
                element.thickness = value.thickness;
                element.graphicWhite = value.color == QtZpl::LineColor::White;
                append(element);
            } else if constexpr (std::is_same_v<T, QtZpl::GraphicDiagonal>) {
                Element element = current;
                element.type = ElementType::Diagonal;
                element.width = value.width; element.height = value.height;
                element.thickness = value.thickness;
                element.graphicWhite = value.color == QtZpl::LineColor::White;
                element.diagonalDirection = value.orientation;
                append(element);
            } else if constexpr (std::is_same_v<T, QtZpl::GraphicField>) {
                Element element = current;
                element.type = ElementType::Image;
                element.width = value.bytesPerRow * 8;
                element.height = value.totalBytes / value.bytesPerRow;
                // QtZpl owns graphic decoding (ASCII repeats, Z64 and binary).
                // Render the isolated field through that same library, then
                // retain the original token spelling until the image changes.
                const auto graphic = checkedRender(header(element.width, element.height)
                    + u"^FO0,0"_s + command.source + u"^FS^XZ"_s, result.dpi);
                if (!graphic || graphic->labels.isEmpty()) {
                    raw(graphic ? u"QtZpl не вернул декодированное изображение."_s : graphic.error()); return;
                }
                for (const auto& diagnostic : graphic->diagnostics)
                    if (diagnostic.severity == QtZpl::Severity::Error) { raw(diagnosticText(diagnostic)); return; }
                element.image = graphic->labels.first().convertToFormat(QImage::Format_Grayscale8);
                element.threshold = 128; element.dither = false;
                if (reverse) raw(u"Инверсия ^GF сохранена в режиме ZPL."_s);
                append(element);
            } else raw(u"Команда %1 сохранена в исходном ZPL; визуальное редактирование отключено."_s.arg(command.source.left(3)));
        }, command.payload);
    }
    if (templateStage != TemplateStage::None || templateText)
        raw(u"Незавершённые метаданные шаблона DataMatrix сохранены в исходном ZPL."_s);
    if (fieldProduced || haveOrigin)
        raw(u"Поле без завершающей ^FS сохранено в исходном ZPL."_s);
    if (previousFieldEnd > 0) {
        result.sourceEpilogue = source.sliced(previousFieldEnd);
    } else if (result.elements.isEmpty()) {
        result.sourcePreamble = source.first(label.commands().last().offset);
        result.sourceEpilogue = source.sliced(label.commands().last().offset);
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
            const auto invalid = validateElement(element);
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
