#include "project_codec.hpp"
#include "element_clipboard.hpp"
#include "eac_mark.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
constexpr qsizetype maximumSourceBytes = 8 * 1024 * 1024;

bool integer(const QJsonValue& value, int minimum, int maximum, int& result)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum || number != std::floor(number))
        return false;
    result = static_cast<int>(number);
    return true;
}

bool string(const QJsonValue& value, QString& result, qsizetype maximum = maximumSourceBytes)
{
    if (!value.isString()) return false;
    result = value.toString();
    return result.size() <= maximum && result.toUtf8().size() <= maximum;
}

QJsonArray integers(const QList<int>& values)
{
    QJsonArray result;
    for (int value : values) result.append(value);
    return result;
}

QJsonObject layoutObject(const CanvasLayoutSettings& layout)
{
    return {{u"gridVisible"_s, layout.gridVisible}, {u"snapEnabled"_s, layout.snapEnabled},
        {u"rulersVisible"_s, layout.rulersVisible}, {u"guidesVisible"_s, layout.guidesVisible},
        {u"marginsVisible"_s, layout.marginsVisible}, {u"gridStep"_s, layout.gridStep},
        {u"margin"_s, layout.margin}, {u"unit"_s, layout.unit == CanvasUnit::Dots ? u"dots"_s : u"mm"_s},
        {u"horizontalGuides"_s, integers(layout.horizontalGuides)},
        {u"verticalGuides"_s, integers(layout.verticalGuides)}};
}

bool readLayout(const QJsonValue& value, CanvasLayoutSettings& layout)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    const auto boolean = [&](const QString& name, bool& target) {
        if (!object.value(name).isBool()) return false;
        target = object.value(name).toBool();
        return true;
    };
    if (!boolean(u"gridVisible"_s, layout.gridVisible) || !boolean(u"snapEnabled"_s, layout.snapEnabled)
        || !boolean(u"rulersVisible"_s, layout.rulersVisible) || !boolean(u"guidesVisible"_s, layout.guidesVisible)
        || !boolean(u"marginsVisible"_s, layout.marginsVisible)
        || !integer(object.value(u"gridStep"_s), 1, 4096, layout.gridStep)
        || !integer(object.value(u"margin"_s), 0, 2048, layout.margin)) return false;
    const auto unit = object.value(u"unit"_s).toString();
    if (unit != u"dots" && unit != u"mm") return false;
    layout.unit = unit == u"dots" ? CanvasUnit::Dots : CanvasUnit::Millimeters;
    const auto guides = [](const QJsonValue& item, QList<int>& target) {
        if (!item.isArray() || item.toArray().size() > 128) return false;
        for (const auto position : item.toArray()) {
            int coordinate = 0;
            if (!integer(position, 0, 4096, coordinate)) return false;
            target.append(coordinate);
        }
        std::ranges::sort(target);
        target.erase(std::unique(target.begin(), target.end()), target.end());
        return true;
    };
    return guides(object.value(u"horizontalGuides"_s), layout.horizontalGuides)
        && guides(object.value(u"verticalGuides"_s), layout.verticalGuides);
}
} // namespace

std::expected<QByteArray, QString> encodeProject(const ProjectState& project)
{
    const auto& document = project.document;
    QJsonObject elements{{u"version"_s, 1}, {u"elements"_s, QJsonArray{}}};
    if (!document.elements.isEmpty()) {
        const auto encoded = encodeElements(document.elements);
        if (encoded.isEmpty()) return std::unexpected(u"Не удалось сохранить объекты проекта: превышен лимит данных или изображений."_s);
        elements = QJsonDocument::fromJson(encoded).object();
    }
    QJsonArray sourceElements;
    for (const auto& element : document.elements) {
        sourceElements.append(QJsonObject{{u"id"_s, element.id.toString(QUuid::WithoutBraces)},
            {u"prefix"_s, element.sourcePrefix}, {u"zpl"_s, element.sourceZpl},
            {u"signature"_s, element.sourceSignature}, {u"state"_s, element.sourceState}});
    }
    QJsonArray order;
    for (const auto& id : document.sourceOrder) order.append(id.toString(QUuid::WithoutBraces));
    QJsonArray notes;
    for (const auto& note : document.importNotes) notes.append(note);
    QJsonObject root{{u"format"_s, u"QtZplDesignerProject"_s}, {u"version"_s, 1},
        {u"width"_s, document.width}, {u"height"_s, document.height}, {u"dpi"_s, document.dpi},
        {u"defaultOrientation"_s, QString(document.defaultOrientation)},
        {u"defaultJustification"_s, document.defaultJustification},
        {u"printMirror"_s, document.printMirror}, {u"printInverted"_s, document.printInverted},
        {u"labelReverse"_s, document.labelReverse},
        {u"visualEditable"_s, document.visualEditable}, {u"objects"_s, elements},
        {u"layout"_s, layoutObject(project.layout)},
        {u"source"_s, QJsonObject{{u"original"_s, document.originalZpl},
            {u"preamble"_s, document.sourcePreamble}, {u"epilogue"_s, document.sourceEpilogue},
            {u"width"_s, document.sourceWidth}, {u"height"_s, document.sourceHeight},
            {u"settingsSignature"_s, document.sourceSettingsSignature},
            {u"order"_s, order}, {u"elements"_s, sourceElements}, {u"notes"_s, notes}}}};
    if (project.pendingSource) root[u"pendingSource"_s] = *project.pendingSource;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    // The same bounded decoder validates both external files and our writes.
    // Projects may hold incomplete barcode fields without being printable yet.
    auto check = decodeProject(bytes);
    if (!check) return std::unexpected(check.error());
    return bytes;
}

std::expected<ProjectState, QString> decodeProject(const QByteArray& bytes)
{
    const auto invalid = [] { return std::unexpected(u"Проект повреждён или содержит недопустимые данные."_s); };
    if (bytes.isEmpty() || bytes.size() > maximumProjectBytes)
        return std::unexpected(u"Проект пуст или превышает лимит 64 МиБ."_s);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) return invalid();
    const auto root = json.object();
    if (root.value(u"format"_s).toString() != u"QtZplDesignerProject")
        return std::unexpected(u"Файл не является проектом QtZpl Designer."_s);
    int version = 0;
    if (!integer(root.value(u"version"_s), 1, 1, version))
        return std::unexpected(u"Версия проекта не поддерживается. Откройте его в совместимой версии QtZpl Designer."_s);
    ProjectState result;
    auto& document = result.document;
    if (!integer(root.value(u"width"_s), 1, 4096, document.width)
        || !integer(root.value(u"height"_s), 1, 4096, document.height)
        || !integer(root.value(u"dpi"_s), 203, 600, document.dpi)
        || (document.dpi != 203 && document.dpi != 300 && document.dpi != 600)
        || !root.value(u"visualEditable"_s).isBool()
        || !readLayout(root.value(u"layout"_s), result.layout)) return invalid();
    document.visualEditable = root.value(u"visualEditable"_s).toBool();
    if (root.contains(u"defaultOrientation"_s)) {
        const auto orientation = root.value(u"defaultOrientation"_s).toString();
        if (orientation.size() != 1 || !QStringView{u"NRIB"}.contains(orientation.front())) return invalid();
        document.defaultOrientation = orientation.front();
    }
    if (root.contains(u"defaultJustification"_s)
        && !integer(root.value(u"defaultJustification"_s), 0, 2, document.defaultJustification)) return invalid();
    for (const auto& field : {std::pair{u"printMirror"_s, &DesignDocument::printMirror},
                             {u"printInverted"_s, &DesignDocument::printInverted},
                             {u"labelReverse"_s, &DesignDocument::labelReverse}}) {
        const auto value = root.value(field.first);
        if (value.isUndefined()) continue;
        if (!value.isBool()) return invalid();
        document.*field.second = value.toBool();
    }
    if (!root.value(u"objects"_s).isObject()) return invalid();
    const auto objects = root.value(u"objects"_s).toObject();
    int objectsVersion = 0;
    if (!integer(objects.value(u"version"_s), 1, 1, objectsVersion)
        || !objects.value(u"elements"_s).isArray()) return invalid();
    if (!objects.value(u"elements"_s).toArray().isEmpty()) {
        auto decoded = decodeElements(QJsonDocument(objects).toJson(QJsonDocument::Compact));
        if (!decoded) return std::unexpected(u"Не удалось прочитать объекты проекта: %1"_s.arg(decoded.error()));
        document.elements = std::move(*decoded);
        for (const auto& element : document.elements)
            if (element.type == ElementType::Eac && element.width < eacMinimumSize(document.dpi, element.eacStyle))
                return std::unexpected(u"Базовый размер букв ЕАС в проекте меньше 5 мм при выбранном DPI."_s);
    }
    if (!document.visualEditable && !document.elements.isEmpty()) return invalid();
    if (!root.value(u"source"_s).isObject()) return invalid();
    const auto source = root.value(u"source"_s).toObject();
    if (!string(source.value(u"original"_s), document.originalZpl)
        || !string(source.value(u"preamble"_s), document.sourcePreamble)
        || !string(source.value(u"epilogue"_s), document.sourceEpilogue)
        || !integer(source.value(u"width"_s), 0, 4096, document.sourceWidth)
        || !integer(source.value(u"height"_s), 0, 4096, document.sourceHeight)
        || !source.value(u"elements"_s).isArray() || !source.value(u"order"_s).isArray()
        || !source.value(u"notes"_s).isArray()) return invalid();
    if (!document.visualEditable && document.originalZpl.isEmpty()) return invalid();
    if (source.contains(u"settingsSignature"_s)
        && !string(source.value(u"settingsSignature"_s), document.sourceSettingsSignature, 128)) return invalid();
    const auto sourceElements = source.value(u"elements"_s).toArray();
    if (sourceElements.size() != document.elements.size()) return invalid();
    QSet<QUuid> ids;
    for (qsizetype index = 0; index < sourceElements.size(); ++index) {
        if (!sourceElements[index].isObject()) return invalid();
        const auto elementSource = sourceElements[index].toObject();
        auto& element = document.elements[index];
        element.id = QUuid(elementSource.value(u"id"_s).toString());
        if (element.id.isNull() || ids.contains(element.id)
            || !string(elementSource.value(u"prefix"_s), element.sourcePrefix)
            || !string(elementSource.value(u"zpl"_s), element.sourceZpl)
            || !string(elementSource.value(u"signature"_s), element.sourceSignature)
            || !string(elementSource.value(u"state"_s), element.sourceState)) return invalid();
        ids.insert(element.id);
    }
    // A Codabar element from before 1.10.0 now writes the start and stop it
    // showed as ^BK k and l, so the unedited original no longer prints it.
    if (!document.originalZpl.isEmpty() && document.visualEditable
        && std::ranges::any_of(document.elements, [&](const Element& element) {
            return element.type == ElementType::Codabar && element.printable && !element.sourceSignature.isEmpty()
                && elementZpl(element, true, document.defaultOrientation, document.defaultJustification) != element.sourceSignature;
        }))
        document.originalZpl.clear();
    const auto order = source.value(u"order"_s).toArray();
    if (order.size() > 200) return invalid();
    QSet<QUuid> orderIds;
    for (const auto& item : order) {
        const QUuid id(item.toString());
        if (id.isNull() || orderIds.contains(id)) return invalid();
        orderIds.insert(id);
        document.sourceOrder.append(id);
    }
    const auto notes = source.value(u"notes"_s).toArray();
    if (notes.size() > 4096) return invalid();
    for (const auto& item : notes) {
        QString note;
        if (!string(item, note, 64 * 1024)) return invalid();
        document.importNotes.append(std::move(note));
    }
    if (root.contains(u"pendingSource"_s)) {
        QString pending;
        if (!string(root.value(u"pendingSource"_s), pending)) return invalid();
        result.pendingSource = std::move(pending);
    }
    return result;
}
} // namespace Designer
