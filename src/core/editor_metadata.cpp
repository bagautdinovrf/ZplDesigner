#include "editor_metadata.hpp"
#include "element_clipboard.hpp"
#include "zpl_source_map.hpp"
#include "design_internal.hpp"
#include <QtZpl/qtzpl.hpp>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
constexpr QStringView prefix = u"QTZPL_EDITOR_V1:";
constexpr QStringView modernPrefix = u"QTZPL_EDITOR_V2:";

// A metadata comment is written as ^FX...^FS directly after ^XA. Remove
// exactly that text and keep the line break that followed the original ^XA.
qsizetype metadataEnd(const QtZpl::Document& parsed, qsizetype index)
{
    const auto& commands = parsed.labels().first().commands();
    const bool separator = index + 1 < commands.size()
        && std::holds_alternative<QtZpl::FieldSeparator>(commands[index + 1].payload);
    return Internal::commandEnd(parsed, commands[separator ? index + 1 : index]);
}

QString withoutMetadata(QString source)
{
    const auto parsed = Internal::parseZpl(source);
    if (!parsed || parsed->labels().size() != 1) return source;
    const auto& commands = parsed->labels().first().commands();
    for (qsizetype i = commands.size() - 1; i >= 0; --i) {
        const auto* comment = std::get_if<QtZpl::Comment>(&commands[i].payload);
        if (!comment || (!comment->text.startsWith(prefix) && !comment->text.startsWith(modernPrefix))) continue;
        source.remove(commands[i].sourceSpan.start, metadataEnd(*parsed, i) - commands[i].sourceSpan.start);
    }
    return source;
}

QString digest(const QString& source)
{
    return QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
}
}

QString writeEditorMetadata(const DesignDocument& document, QString source)
{
    bool modern = source.contains(modernPrefix);
    for (const auto& element : document.elements)
        modern |= element.type == ElementType::Table || element.type == ElementType::Eac || element.locked || !element.editorVisible || !element.printable
            || element.name != elementTypeName(element.type) || (isBarcodeType(element.type) && element.useDefaultOrientation);
    if (modern) {
        source = withoutMetadata(std::move(source));
        if (document.elements.isEmpty()) return source;
        auto snapshotElements = document.elements;
        for (auto& element : snapshotElements) {
            // ZPL stores the printable monochrome graphic. Original color
            // pixels and conversion settings belong to the project format.
            // Do not replicate large source photos merely to retain a name.
            if (element.type == ElementType::Image && element.printable) {
                element.image = monochromeImage(element);
                element.threshold = 128; element.dither = false; element.reverse = false;
            }
        }
        const auto bytes = encodeElements(snapshotElements);
        // An empty result deliberately fails export validation. Returning the
        // printer body here would silently lose hidden objects and table data.
        if (bytes.isEmpty()) return {};
        const auto snapshot = QJsonDocument::fromJson(bytes).object();
        QJsonObject metadata{{u"snapshot"_s, snapshot}, {u"sha256"_s, digest(source)}};
        if (std::ranges::any_of(document.elements, [](const Element& element) { return element.type == ElementType::Eac; }))
            metadata[u"dpi"_s] = document.dpi;
        const auto encoded = QJsonDocument(metadata).toJson(QJsonDocument::Compact).toBase64();
        const auto parsed = Internal::parseZpl(source);
        if (!parsed || parsed->labels().size() != 1) return source;
        source.insert(Internal::commandEnd(*parsed, parsed->labels().first().commands().first()),
            u"^FX"_s + modernPrefix.toString() + QString::fromLatin1(encoded) + u"^FS"_s);
        return source;
    }
    bool needsMetadata = false;
    for (const auto& element : document.elements)
        needsMetadata |= !element.groupId.isNull() || (element.type == ElementType::Image && !element.imageKeepAspectRatio);
    if (!needsMetadata && !source.contains(prefix)) return source;
    const auto parsed = Internal::parseZpl(source);
    if (!parsed || parsed->labels().size() != 1) return source;
    const auto& commands = parsed->labels().first().commands();
    for (qsizetype i = commands.size() - 1; i >= 0; --i) {
        const auto* comment = std::get_if<QtZpl::Comment>(&commands[i].payload);
        if (!comment || !comment->text.startsWith(prefix)) continue;
        source.remove(commands[i].sourceSpan.start, metadataEnd(*parsed, i) - commands[i].sourceSpan.start);
    }
    QJsonObject groups;
    QJsonArray freeImages;
    for (qsizetype i = 0; i < document.elements.size(); ++i) {
        const auto& element = document.elements[i];
        if (!element.groupId.isNull()) groups[QString::number(i)] = element.groupId.toString(QUuid::WithoutBraces);
        if (element.type == ElementType::Image && !element.imageKeepAspectRatio) freeImages.append(int(i));
    }
    if (groups.isEmpty() && freeImages.isEmpty()) return source;
    const QJsonObject metadata{{u"count"_s, int(document.elements.size())}, {u"groups"_s, groups}, {u"freeImages"_s, freeImages}};
    const auto encoded = QJsonDocument(metadata).toJson(QJsonDocument::Compact).toBase64();
    source.insert(Internal::commandEnd(*parsed, commands.first()),
        u"^FX"_s + prefix.toString() + QString::fromLatin1(encoded) + u"^FS"_s);
    return source;
}

bool restoreEditorMetadata(DesignDocument& document, const QString& source)
{
    const auto parsed = Internal::parseZpl(source);
    if (!parsed || parsed->labels().size() != 1) return true;
    bool found = false;
    bool fieldSeen = false;
    for (const auto& command : parsed->labels().first().commands()) {
        if (std::holds_alternative<QtZpl::FieldOrigin>(command.payload)
            || std::holds_alternative<QtZpl::FieldTypeset>(command.payload)) fieldSeen = true;
        const auto* comment = std::get_if<QtZpl::Comment>(&command.payload);
        if (!comment || (!comment->text.startsWith(prefix) && !comment->text.startsWith(modernPrefix))) continue;
        if (comment->text.startsWith(modernPrefix)) {
            if (found || fieldSeen || comment->text.size() > 8 * 1024 * 1024) return false;
            found = true;
            const auto bytes = QByteArray::fromBase64(comment->text.sliced(modernPrefix.size()).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            const auto json = QJsonDocument::fromJson(bytes);
            if (!json.isObject()) return false;
            const auto object = json.object();
            const QString clean = withoutMetadata(source);
            if (object.value(u"sha256"_s).toString() != digest(clean) || !object.value(u"snapshot"_s).isObject()) return false;
            const auto decoded = decodeElements(QJsonDocument(object.value(u"snapshot"_s).toObject()).toJson(QJsonDocument::Compact));
            if (!decoded) return false;
            auto restored = document;
            if (object.contains(u"dpi"_s)) {
                const auto value = object.value(u"dpi"_s);
                const int dpi = value.toInt(-1);
                if (!value.isDouble() || value.toDouble() != dpi || (dpi != 203 && dpi != 300 && dpi != 600)) return false;
                restored.dpi = dpi;
            }
            restored.elements.clear(); restored.sourceOrder.clear();
            auto sourceElements = document.elements;
            qsizetype index = 0;
            for (auto element : *decoded) {
                if (element.printable) {
                    const auto count = elementFieldCount(element);
                    if (index + count > sourceElements.size()) return false;
                    const auto& first = sourceElements[index];
                    element.id = first.id; element.sourcePrefix = first.sourcePrefix;
                    element.sourceState = first.sourceState;
                    if (element.type == ElementType::Table || element.type == ElementType::Eac) {
                        QString fields;
                        for (qsizetype field = 0; field < count; ++field) {
                            const auto& part = sourceElements[index + field];
                            if (field) fields += part.sourcePrefix;
                            fields += part.sourceZpl;
                        }
                        const auto signature = elementZpl(element, true, document.defaultOrientation, document.defaultJustification);
                        const auto expected = signature.trimmed();
                        if (!expected.startsWith(fields)) return false;
                        const QString tail = expected.sliced(fields.size());
                        // A final ^FW restoration is after the last cell's
                        // ^FS, and the importer consequently stores it with
                        // the following field/epilogue. Attach that exact tail
                        // to the aggregate table without consuming comments.
                        QString& following = index + count < sourceElements.size()
                            ? sourceElements[index + count].sourcePrefix : restored.sourceEpilogue;
                        if (!following.startsWith(tail)) return false;
                        following.remove(0, tail.size());
                        element.sourceZpl = fields + tail; element.sourceSignature = signature;
                    } else {
                        const bool sameBoxCommand = (element.type == ElementType::Box || element.type == ElementType::Line)
                            && (first.type == ElementType::Box || first.type == ElementType::Line);
                        if (element.type != first.type && !sameBoxCommand) return false;
                        element.sourceZpl = first.sourceZpl; element.sourceSignature = first.sourceSignature;
                    }
                    index += count;
                }
                if (element.printable) restored.sourceOrder.append(element.id);
                restored.elements.append(std::move(element));
            }
            if (index != sourceElements.size()) return false;
            restored.originalZpl.clear();
            // Never let edited metadata silently override the printable body.
            // Table fields must match byte-for-byte; ordinary imported fields
            // retain their existing lossless source fragments and state.
            if (withoutMetadata(toZpl(restored)) != clean) return false;
            restored.originalZpl = source;
            document = std::move(restored);
            continue;
        }
        if (found || fieldSeen || comment->text.size() > 32768) return false;
        found = true;
        const auto bytes = QByteArray::fromBase64(comment->text.sliced(prefix.size()).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        const auto json = QJsonDocument::fromJson(bytes);
        if (!json.isObject()) return false;
        const auto object = json.object();
        if (object.value(u"count"_s).toInt(-1) != document.elements.size()
            || !object.value(u"groups"_s).isObject() || !object.value(u"freeImages"_s).isArray()) return false;
        const auto groups = object.value(u"groups"_s).toObject();
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            bool ok = false;
            const int index = it.key().toInt(&ok);
            const QUuid group(it.value().toString());
            if (!ok || index < 0 || index >= document.elements.size() || group.isNull()) return false;
            document.elements[index].groupId = group;
        }
        for (const auto& value : object.value(u"freeImages"_s).toArray()) {
            const int index = value.toInt(-1);
            if (index < 0 || index >= document.elements.size() || document.elements[index].type != ElementType::Image) return false;
            document.elements[index].imageKeepAspectRatio = false;
        }
    }
    return true;
}
}
