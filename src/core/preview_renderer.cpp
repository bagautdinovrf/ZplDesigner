#include "design_document.hpp"
#include "design_internal.hpp"
#include "data_matrix_values.hpp"
#include "preview_pages.hpp"

#include <QCache>
#include <QMutex>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Designer {
using namespace Qt::StringLiterals;
using namespace Internal;
namespace Internal {
std::expected<QtZpl::Document, QString> checkedParse(const QString& source)
{
    if (source.size() > maximumSource || source.toUtf8().size() > maximumSource)
        return std::unexpected(u"ZPL превышает предел 8 МиБ в UTF-8."_s);
    const auto parsed = QtZpl::parse(source);
    if (!parsed) return std::unexpected(parsed.error().message);
    const QString error = validateParsed(*parsed);
    if (!error.isEmpty()) return std::unexpected(error);
    return *parsed;
}

std::expected<QtZpl::RenderResult, QString> checkedRenderPage(const QString& source, int dpi, int pageIndex)
{
    const auto parsed = checkedParse(source);
    if (!parsed) return std::unexpected(parsed.error());
    if (pageIndex < 0 || pageIndex >= parsed->labels().size())
        return std::unexpected(u"Номер этикетки вне диапазона."_s);
    QtZpl::RenderOptions options;
    options.dpi = dpi;
    // Render the exact parsed snapshot. Slicing normalized command text used
    // to lose spelling/CRLF information and require guessed offset rebasing.
    auto result = QtZpl::renderLabel(*parsed, pageIndex, options);
    if (!result) return std::unexpected(result.error().message);
    return QtZpl::RenderResult{{std::move(result->image)}, std::move(result->diagnostics),
                               std::move(result->fields)};
}

std::expected<QtZpl::RenderResult, QString> checkedRender(const QString& source, int dpi)
{
    return checkedRenderPage(source, dpi, 0);
}

} // namespace Internal
namespace {
QRect inkBounds(const QImage& image)
{
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qGray(row[x]) < 128) {
                left = std::min(left, x); right = std::max(right, x);
                top = std::min(top, y); bottom = y;
            }
    }
    return right >= left ? QRect(QPoint(left, top), QPoint(right, bottom)) : QRect{};
}

// One field rendered alone on an otherwise empty label: its ink, and the first
// error a printer preview would reject it for.
struct IsolatedField {
    QRect ink;
    QString error;
};

// Every preview refresh measures each text, barcode and image field alone. A
// canvas drag changes one origin, so the other fields' isolated sources are
// unchanged; the same source and dpi always render the same, so their results
// are reused instead of repeating one full-label render per field.
IsolatedField renderIsolatedField(const QString& source, int dpi)
{
    static QMutex mutex;
    // Cost is key length: bounded memory even for embedded graphics.
    static QCache<QString, IsolatedField> cache(maximumSource);
    const QString key = QString::number(dpi) + u'\n' + source;
    {
        const QMutexLocker lock(&mutex);
        if (const auto* cached = cache.object(key)) return *cached;
    }
    IsolatedField field;
    const auto rendered = checkedRender(source, dpi);
    if (!rendered) field.error = rendered.error();
    else {
        for (const auto& diagnostic : rendered->diagnostics)
            if (diagnostic.severity == QtZpl::Severity::Error) {
                field.error = diagnosticText(diagnostic);
                break;
            }
        if (!rendered->labels.isEmpty()) field.ink = inkBounds(rendered->labels.first());
    }
    const QMutexLocker lock(&mutex);
    cache.insert(key, new IsolatedField(field), key.size());
    return field;
}

std::expected<Preview, QString> renderPreviewImpl(const DesignDocument& templateDocument, bool editor, int pageIndex = 0)
{
    using namespace Qt::StringLiterals;
    if (!templateDocument.visualEditable) {
        const auto parsed = QtZpl::parse(toZpl(templateDocument));
        if (parsed && pageIndex >= 0 && pageIndex < parsed->labels().size()) {
            bool dataMatrix = false;
            for (const auto& command : parsed->labels()[pageIndex].commands()) {
                if (const auto* code = std::get_if<QtZpl::Barcode>(&command.payload)) dataMatrix = code->symbology == u"BX";
                else if (std::holds_alternative<QtZpl::FieldSeparator>(command.payload)) dataMatrix = false;
                else if (const auto* field = std::get_if<QtZpl::FieldData>(&command.payload); field && dataMatrix) {
                    if (field->data.contains(u"{{") || field->data.contains(u"[["))
                        return std::unexpected(u"DataMatrix содержит плейсхолдеры в неподдерживаемом исходном ZPL. Превью без подстановки недоступно; исходник сохранён."_s);
                    dataMatrix = false;
                }
            }
        }
    }
    const auto resolved = resolveDataMatrixValues(templateDocument, editor);
    if (!resolved) return std::unexpected(resolved.error() + u" Откройте «Значения плейсхолдеров…» для заполнения переменных этикетки."_s);
    const auto& document = *resolved;
    if (!safeSize(document.width, document.height))
        return std::unexpected(u"Размер этикетки превышает 4096 × 4096 точек."_s);
    if (document.elements.size() > maximumElements)
        return std::unexpected(u"MVP поддерживает не более 200 элементов."_s);
    if (document.originalZpl.isEmpty())
        for (const auto& element : document.elements) {
            if (!element.printable) continue;
            const auto error = validateElement(element, false, document.dpi);
            if (!error.isEmpty()) return std::unexpected(error);
        }
    const auto rendered = checkedRenderPage(printerZpl(document, false), document.dpi, pageIndex);
    if (!rendered) return std::unexpected(rendered.error());
    if (rendered->labels.isEmpty()) return std::unexpected(u"QtZpl не вернул изображение этикетки."_s);
    Preview preview;
    preview.image = rendered->labels.first();
    preview.diagnostics = document.importNotes;
    const auto names = dataMatrixParameters(templateDocument);
    if (names && !names->isEmpty()) {
        QStringList missing;
        for (const auto& name : *names)
            if (!templateDocument.previewValues.contains(name)) missing.append(name);
        preview.diagnostics.append(missing.isEmpty()
            ? u"Превью использует значения плейсхолдеров текущей сессии: %1. Сохранение ZPL сохраняет шаблон."_s.arg(names->join(u", "_s))
            : u"В тексте показаны незаполненные переменные: %1. Заполните «Значения плейсхолдеров…» для печати и чистого превью."_s.arg(missing.join(u", "_s)));
    }
    for (const auto& diagnostic : rendered->diagnostics) {
        if (diagnostic.severity == QtZpl::Severity::Error)
            return std::unexpected(diagnosticText(diagnostic));
        preview.diagnostics.append(diagnosticText(diagnostic));
    }
    // Source-only labels have no editable element geometry. Their selected
    // page may differ entirely from the first label described by the model.
    if (!document.visualEditable) {
        preview.diagnostics.removeDuplicates();
        return preview;
    }
    for (const auto& element : document.elements) {
        if (!element.printable) {
            preview.bounds.append(mapDocumentRect(document, QRect(element.x, element.y, element.width, element.height)));
            continue;
        }
        if (element.type == ElementType::Box || element.type == ElementType::Line
            || element.type == ElementType::Ellipse || element.type == ElementType::Circle
            || element.type == ElementType::Diagonal || element.type == ElementType::Table || element.type == ElementType::Eac) {
            const int height = element.type == ElementType::Circle ? element.width : element.height;
            const int justification = element.originJustification < 0 ? document.defaultJustification : element.originJustification;
            const bool aggregate = element.type == ElementType::Table || element.type == ElementType::Eac;
            const QRect logicalBounds(element.x - (!aggregate && justification == 1 ? element.width : 0),
                element.y - (!aggregate && element.baselineOrigin ? height : 0), element.width, height);
            const QRect bounds = mapDocumentRect(document, logicalBounds);
            const QRect clipped = bounds.intersected(preview.image.rect());
            preview.bounds.append(clipped.isEmpty() ? bounds : clipped);
            continue;
        }
        Element visible = element;
        visible.reverse = false;
        DesignDocument single;
        single.width = document.width; single.height = document.height; single.dpi = document.dpi;
        single.defaultOrientation = document.defaultOrientation;
        single.defaultJustification = document.defaultJustification;
        single.elements.append(visible);
        QRect bounds = renderIsolatedField(printerZpl(single, false), single.dpi).ink;
        if (bounds.isEmpty()) bounds = QRect(element.x, element.y, std::max(8, element.width), std::max(8, element.height));
        preview.bounds.append(mapDocumentRect(document, bounds));
    }
    preview.diagnostics.removeDuplicates();
    return preview;
}
} // namespace

std::expected<Preview, QString> renderPreview(const DesignDocument& document)
{
    return renderPreviewImpl(document, false);
}

std::expected<int, QString> previewPageCount(const DesignDocument& document)
{
    if (document.visualEditable) return 1;
    const auto parsed = checkedParse(toZpl(document));
    if (!parsed) return std::unexpected(parsed.error());
    return static_cast<int>(parsed->labels().size());
}

std::expected<Preview, QString> renderPreviewPage(const DesignDocument& document, int pageIndex)
{
    return renderPreviewImpl(document, false, pageIndex);
}

QList<SourceDiagnostic> sourceDiagnostics(const DesignDocument& document, int pageIndex)
{
    const auto localDiagnostic = [](const QString& code, const QString& message, int offset = -1) {
        QtZpl::Diagnostic detail;
        detail.severity = QtZpl::Severity::Error;
        detail.code = code;
        detail.message = message;
        detail.offset = offset;
        return SourceDiagnostic{message, offset, std::move(detail)};
    };
    if (document.visualEditable) {
        const auto valid = validateForExport(document);
        if (!valid) return {localDiagnostic(u"designer-export"_s, valid.error())};
    }
    const QString source = toZpl(document);
    if (source.size() > maximumSource || source.toUtf8().size() > maximumSource)
        return {localDiagnostic(u"designer-source-limit"_s, u"ZPL превышает предел 8 МиБ в UTF-8."_s)};
    const auto parsed = QtZpl::parse(source);
    if (!parsed) return {localDiagnostic(parsed.error().code, parsed.error().message,
                                        static_cast<int>(parsed.error().offset))};
    QList<SourceDiagnostic> diagnostics;
    const auto append = [&diagnostics](const QtZpl::Diagnostic& diagnostic) {
        const auto text = diagnosticText(diagnostic);
        const int offset = static_cast<int>(diagnostic.offset);
        if (std::ranges::none_of(diagnostics, [&](const SourceDiagnostic& existing) {
            return existing.detail.code == diagnostic.code && existing.detail.severity == diagnostic.severity
                && existing.detail.message == diagnostic.message && existing.detail.offset == diagnostic.offset
                && existing.detail.labelIndex == diagnostic.labelIndex && existing.detail.fieldId == diagnostic.fieldId
                && existing.detail.sourceSpan == diagnostic.sourceSpan;
        })) diagnostics.append({text, offset, diagnostic});
    };
    for (const auto& diagnostic : parsed->diagnostics()) append(diagnostic);
    qsizetype safetyOffset = -1;
    const QString safety = validateParsed(*parsed, &safetyOffset);
    if (!safety.isEmpty()) {
        diagnostics.append(localDiagnostic(u"designer-source-safety"_s, safety, static_cast<int>(safetyOffset)));
        return diagnostics;
    }
    // The normal preview already supplies render diagnostics for editable
    // templates. Do not render unresolved placeholders or invalid barcodes a
    // second time merely to populate source-navigation links.
    if (document.visualEditable) return diagnostics;
    if (pageIndex >= 0 && pageIndex < parsed->labels().size()) {
        for (const auto& command : parsed->labels()[pageIndex].commands())
            if (const auto* field = std::get_if<QtZpl::FieldData>(&command.payload);
                field && (field->data.contains(u"{{") || field->data.contains(u"[[")))
                return diagnostics;
    }
    // The diagnostic panel refers to the visible source, before placeholders
    // are expanded, so every offset still addresses the editor's exact text.
    QtZpl::RenderOptions options;
    options.dpi = document.dpi;
    const auto rendered = QtZpl::renderLabel(*parsed, pageIndex, options);
    if (rendered) {
        for (const auto& diagnostic : rendered->diagnostics) append(diagnostic);
    } else {
        // RenderError identifies a page, not a source command. Do not invent a
        // clickable position at ^XA for a failure whose location is unknown.
        auto diagnostic = localDiagnostic(rendered.error().code, rendered.error().message);
        if (rendered.error().labelIndex >= 0) diagnostic.detail.labelIndex = rendered.error().labelIndex;
        diagnostics.append(std::move(diagnostic));
    }
    return diagnostics;
}

std::expected<Preview, QString> renderEditorPreview(const DesignDocument& document)
{
    if (!document.visualEditable) return renderPreview(document);
    if (!safeSize(document.width, document.height))
        return std::unexpected(u"Размер этикетки превышает 4096 × 4096 точек."_s);
    if (document.elements.size() > maximumElements)
        return std::unexpected(u"MVP поддерживает не более 200 элементов."_s);

    auto filtered = document;
    filtered.originalZpl.clear();
    filtered.elements.clear();
    QList<int> retainedIndices;
    QList<QRect> bounds(document.elements.size());
    QMap<int, QString> errors;
    for (qsizetype index = 0; index < document.elements.size(); ++index) {
        auto element = document.elements[index];
        if (!element.editorVisible) {
            bounds[index] = mapDocumentRect(document, QRect(element.x, element.y, element.width, element.height));
            continue;
        }
        element.printable = true;
        QString error;
        if (isBarcodeType(element.type)) {
            // Excluding an invalid field must not hide unsafe geometry or
            // allocation limits. This copy is validated only, never rendered.
            auto geometry = element;
            geometry.gs1 = false;
            geometry.text = makeElement(element.type).text;
            const auto safetyError = validateElement(geometry, false);
            if (!safetyError.isEmpty()) return std::unexpected(safetyError);
            DesignDocument isolated;
            isolated.width = document.width;
            isolated.height = document.height;
            isolated.dpi = document.dpi;
            isolated.elements.append(element);
            isolated.previewValues = document.previewValues;
            const auto resolved = resolveDataMatrixValues(isolated);
            if (!resolved) error = resolved.error();
            else {
                error = validateElement(resolved->elements.first(), false);
                if (error.isEmpty()) error = renderIsolatedField(printerZpl(*resolved, false), document.dpi).error;
            }
        }
        if (!error.isEmpty()) {
            errors.insert(static_cast<int>(index), error);
            bounds[index] = QRect(element.x, element.y, element.width, element.height);
            if (element.orientation == u'R' || element.orientation == u'B')
                bounds[index].setSize(QSize(element.height, element.width));
            bounds[index] = mapDocumentRect(document, bounds[index]);
        } else {
            filtered.elements.append(element);
            retainedIndices.append(static_cast<int>(index));
        }
    }
    // Preserve the strict result for failures outside recoverable DataMatrix
    // input. Source mode and printer-preview callers never use a partial image.
    auto rendered = renderPreviewImpl(filtered, true);
    if (!rendered) return std::unexpected(rendered.error());
    for (qsizetype index = 0; index < retainedIndices.size(); ++index)
        bounds[retainedIndices[index]] = rendered->bounds[index];
    rendered->bounds = std::move(bounds);
    rendered->elementErrors = std::move(errors);
    for (auto it = rendered->elementErrors.cbegin(); it != rendered->elementErrors.cend(); ++it)
        rendered->diagnostics.append(u"%1, элемент %2: %3 Код не отображается на холсте."_s
            .arg(elementTypeName(document.elements[it.key()].type)).arg(it.key() + 1).arg(it.value()));
    return rendered;
}

} // namespace Designer
