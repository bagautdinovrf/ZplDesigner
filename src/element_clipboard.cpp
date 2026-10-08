#include "element_clipboard.hpp"
#include "design_internal.hpp"

#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
constexpr qsizetype maximumClipboard = 32 * 1024 * 1024;
struct IntegerField { const char* name; int Element::*member; };
constexpr IntegerField integers[] = {
    {"x", &Element::x}, {"y", &Element::y}, {"width", &Element::width}, {"height", &Element::height},
    {"thickness", &Element::thickness}, {"fontHeight", &Element::fontHeight}, {"fontWidth", &Element::fontWidth},
    {"blockWidth", &Element::blockWidth}, {"blockMaxLines", &Element::blockMaxLines},
    {"blockLineSpacing", &Element::blockLineSpacing}, {"blockHangingIndent", &Element::blockHangingIndent},
    {"moduleWidth", &Element::moduleWidth}, {"threshold", &Element::threshold}
};
struct BooleanField { const char* name; bool Element::*member; };
constexpr BooleanField booleans[] = {
    {"dither", &Element::dither}, {"reverse", &Element::reverse}, {"humanReadable", &Element::humanReadable},
    {"interpretationAbove", &Element::interpretationAbove}, {"code128CheckDigit", &Element::code128CheckDigit},
    {"gs1", &Element::gs1}, {"literalText", &Element::literalText}, {"imageKeepAspectRatio", &Element::imageKeepAspectRatio}
};
struct CharacterField { const char* name; QChar Element::*member; };
constexpr CharacterField characters[] = {
    {"orientation", &Element::orientation}, {"blockJustification", &Element::blockJustification},
    {"code128Mode", &Element::code128Mode}
};
constexpr IntegerField advancedIntegers[] = {
    {"originJustification", &Element::originJustification}, {"characterSpacing", &Element::characterSpacing}
};
constexpr BooleanField advancedBooleans[] = {
    {"baselineOrigin", &Element::baselineOrigin}, {"useDefaultOrientation", &Element::useDefaultOrientation},
    {"graphicWhite", &Element::graphicWhite}
};
constexpr CharacterField advancedCharacters[] = {
    {"textDirection", &Element::textDirection}, {"diagonalDirection", &Element::diagonalDirection}
};
constexpr IntegerField barcodeIntegers[] = {
    {"pdf417Rows", &Element::pdf417Rows}, {"dataMatrixColumns", &Element::dataMatrixColumns},
    {"dataMatrixRows", &Element::dataMatrixRows}, {"dataBarType", &Element::dataBarType},
    {"dataBarSegments", &Element::dataBarSegments}, {"dataBarSeparator", &Element::dataBarSeparator},
    {"microPdf417Mode", &Element::microPdf417Mode}, {"aztecSize", &Element::aztecSize},
    {"maxiCodeMode", &Element::maxiCodeMode}, {"maxiCodePosition", &Element::maxiCodePosition}, {"maxiCodeTotal", &Element::maxiCodeTotal}
};
constexpr BooleanField barcodeBooleans[] = {
    {"pdf417Truncated", &Element::pdf417Truncated}, {"dataMatrixRectangular", &Element::dataMatrixRectangular},
    {"barcodeCheckDigit", &Element::barcodeCheckDigit}, {"aztecReaderInit", &Element::aztecReaderInit}
};
}
QByteArray encodeElements(const QList<Element>& elements)
{
    if (elements.isEmpty() || elements.size() > 200) return {};
    QJsonArray array;
    qint64 pixels = 0;
    for (const auto& element : elements) {
        QJsonObject object{{u"type"_s, int(element.type)}, {u"name"_s, element.name},
            {u"text"_s, element.text}, {u"group"_s, element.groupId.toString(QUuid::WithoutBraces)},
            {u"barcodeRatio"_s, element.barcodeRatio}};
        for (const auto& field : integers) object[QLatin1StringView(field.name)] = element.*field.member;
        for (const auto& field : booleans) object[QLatin1StringView(field.name)] = element.*field.member;
        for (const auto& field : characters) object[QLatin1StringView(field.name)] = QString(element.*field.member);
        for (const auto& field : advancedIntegers) object[QLatin1StringView(field.name)] = element.*field.member;
        for (const auto& field : advancedBooleans) object[QLatin1StringView(field.name)] = element.*field.member;
        for (const auto& field : advancedCharacters) object[QLatin1StringView(field.name)] = QString(element.*field.member);
        for (const auto& field : barcodeIntegers) object[QLatin1StringView(field.name)] = element.*field.member;
        for (const auto& field : barcodeBooleans) object[QLatin1StringView(field.name)] = element.*field.member;
        object[u"locked"_s] = element.locked;
        object[u"editorVisible"_s] = element.editorVisible;
        object[u"printable"_s] = element.printable;
        object[u"qrErrorCorrection"_s] = QString(element.qrErrorCorrection);
        object[u"pdf417Columns"_s] = element.pdf417Columns;
        object[u"pdf417Security"_s] = element.pdf417Security;
        object[u"pdf417RowHeight"_s] = element.pdf417RowHeight;
        if (element.type == ElementType::Table) {
            QJsonArray columns, rows, cells;
            for (const int size : element.tableColumnWidths) columns.append(size);
            for (const int size : element.tableRowHeights) rows.append(size);
            for (const auto& cell : element.tableCells) cells.append(cell);
            object[u"tableColumns"_s] = columns; object[u"tableRows"_s] = rows; object[u"tableCells"_s] = cells;
            object[u"tableBorders"_s] = element.tableBorders; object[u"tablePadding"_s] = element.tablePadding;
        }
        if (element.type == ElementType::Image) {
            pixels += qint64(element.image.width()) * element.image.height();
            if (pixels > 32 * 1024 * 1024) return {};
            QByteArray png;
            QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            if (!element.image.save(&buffer, "PNG")) return {};
            object[u"image"_s] = QString::fromLatin1(png.toBase64());
        }
        array.append(object);
    }
    const auto result = QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"elements"_s, array}}).toJson(QJsonDocument::Compact);
    return result.size() <= maximumClipboard ? result : QByteArray{};
}

std::expected<QList<Element>, QString> decodeElements(const QByteArray& data)
{
    const auto invalid = [] { return std::unexpected(u"Буфер объектов повреждён или превышает допустимые размеры."_s); };
    if (data.isEmpty() || data.size() > maximumClipboard) return invalid();
    const auto json = QJsonDocument::fromJson(data);
    if (!json.isObject()) return invalid();
    const auto root = json.object();
    if (root.value(u"version"_s).toInt(-1) != 1 || !root.value(u"elements"_s).isArray()) return invalid();
    const auto array = root.value(u"elements"_s).toArray();
    if (array.isEmpty() || array.size() > 200) return invalid();
    QList<Element> result;
    qint64 pixels = 0;
    for (const auto& value : array) {
        if (!value.isObject()) return invalid();
        const auto object = value.toObject();
        const auto type = object.value(u"type"_s).toInt(-1);
        if (type < 0 || type > int(ElementType::Diagonal)) return invalid();
        Element element = makeElement(static_cast<ElementType>(type));
        for (const auto& field : integers) {
            const auto number = object.value(QLatin1StringView(field.name));
            if (!number.isDouble() || !std::isfinite(number.toDouble())
                || number.toDouble() < -4096 || number.toDouble() > 4096
                || number.toDouble() != std::floor(number.toDouble())) return invalid();
            element.*field.member = number.toInt();
        }
        for (const auto& field : booleans) {
            const auto boolean = object.value(QLatin1StringView(field.name));
            if (!boolean.isBool()) return invalid();
            element.*field.member = boolean.toBool();
        }
        for (const auto& field : characters) {
            const auto character = object.value(QLatin1StringView(field.name)).toString();
            if (character.size() != 1) return invalid();
            element.*field.member = character.front();
        }
        for (const auto& field : advancedIntegers) {
            const auto number = object.value(QLatin1StringView(field.name));
            if (number.isUndefined()) continue;
            if (!number.isDouble() || number.toDouble() < -10 || number.toDouble() > 9999
                || number.toDouble() != std::floor(number.toDouble())) return invalid();
            element.*field.member = number.toInt();
        }
        for (const auto& field : advancedBooleans) {
            const auto property = object.value(QLatin1StringView(field.name));
            if (property.isUndefined()) continue;
            if (!property.isBool()) return invalid();
            element.*field.member = property.toBool();
        }
        for (const auto& field : advancedCharacters) {
            const auto property = object.value(QLatin1StringView(field.name));
            if (property.isUndefined()) continue;
            const auto text = property.toString();
            if (text.size() != 1) return invalid();
            element.*field.member = text.front();
        }
        for (const auto& field : {BooleanField{"locked", &Element::locked}, {"editorVisible", &Element::editorVisible}, {"printable", &Element::printable}}) {
            const auto boolean = object.value(QLatin1StringView(field.name));
            if (!boolean.isUndefined() && !boolean.isBool()) return invalid();
            if (boolean.isBool()) element.*field.member = boolean.toBool();
        }
        if (object.contains(u"qrErrorCorrection"_s)) {
            const auto ecc = object.value(u"qrErrorCorrection"_s).toString();
            if (ecc.size() != 1 || !QStringView{u"LMQH"}.contains(ecc.front())) return invalid();
            element.qrErrorCorrection = ecc.front();
        }
        for (const auto& field : barcodeIntegers) {
            const auto number = object.value(QLatin1StringView(field.name));
            if (number.isUndefined()) continue;
            if (!number.isDouble() || number.toDouble() != number.toInt(-1)) return invalid();
            element.*field.member = number.toInt();
        }
        for (const auto& field : barcodeBooleans) {
            const auto property = object.value(QLatin1StringView(field.name));
            if (property.isUndefined()) continue;
            if (!property.isBool()) return invalid();
            element.*field.member = property.toBool();
        }
        for (const auto& field : {IntegerField{"pdf417Columns", &Element::pdf417Columns}, {"pdf417Security", &Element::pdf417Security}, {"pdf417RowHeight", &Element::pdf417RowHeight}}) {
            const auto number = object.value(QLatin1StringView(field.name));
            if (number.isUndefined()) continue;
            if (!number.isDouble() || number.toDouble() != number.toInt(-1)) return invalid();
            element.*field.member = number.toInt();
        }
        if (element.type == ElementType::Table) {
            const auto readSizes = [&](QStringView name, QList<int>& values) {
                const auto arrayValue = object.value(name);
                if (!arrayValue.isArray()) return false;
                const auto sizes = arrayValue.toArray();
                if (sizes.isEmpty() || sizes.size() > 12) return false;
                values.clear();
                for (const auto& item : sizes) {
                    const int size = item.toInt(-1);
                    if (!item.isDouble() || item.toDouble() != size || size < 8 || size > 4096) return false;
                    values.append(size);
                }
                return true;
            };
            if (!readSizes(u"tableColumns", element.tableColumnWidths) || !readSizes(u"tableRows", element.tableRowHeights)) return invalid();
            const auto cells = object.value(u"tableCells"_s).toArray();
            if (cells.size() != element.tableColumnWidths.size() * element.tableRowHeights.size()) return invalid();
            element.tableCells.clear();
            for (const auto& cell : cells) {
                if (!cell.isString() || cell.toString().size() > 1024) return invalid();
                element.tableCells.append(cell.toString());
            }
            if (!object.value(u"tableBorders"_s).isBool()) return invalid();
            element.tableBorders = object.value(u"tableBorders"_s).toBool();
            element.tablePadding = object.value(u"tablePadding"_s).toInt(-1);
        }
        if (!object.value(u"text"_s).isString() || !object.value(u"name"_s).isString()
            || !object.value(u"barcodeRatio"_s).isDouble()) return invalid();
        element.text = object.value(u"text"_s).toString();
        element.name = object.value(u"name"_s).toString();
        if (element.text.size() > 1024 || element.name.size() > 1024) return invalid();
        element.barcodeRatio = object.value(u"barcodeRatio"_s).toDouble();
        const auto group = object.value(u"group"_s).toString();
        element.groupId = QUuid(group);
        if (element.groupId.isNull() && group != QUuid{}.toString(QUuid::WithoutBraces)) return invalid();
        if (element.type == ElementType::Image) {
            QByteArray png = QByteArray::fromBase64(object.value(u"image"_s).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            QBuffer buffer(&png); buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PNG");
            const auto size = reader.size();
            if (!Internal::safeSize(size.width(), size.height())) return invalid();
            pixels += qint64(size.width()) * size.height();
            if (pixels > 32 * 1024 * 1024) return invalid();
            element.image = reader.read();
        }
        // Keep incomplete barcode input editable when transferring objects.
        auto geometry = element; geometry.gs1 = false; geometry.literalText = true;
        if (isBarcodeType(geometry.type)) geometry.text = makeElement(geometry.type).text;
        if (!Internal::validateElement(geometry).isEmpty() || element.threshold < 0 || element.threshold > 255) return invalid();
        result.append(std::move(element));
    }
    return result;
}
} // namespace Designer
