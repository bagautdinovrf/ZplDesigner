#include "design_document.hpp"
#include "design_internal.hpp"
#include "data_matrix_values.hpp"
#include "preview_pages.hpp"
#include "zpl_source_map.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Designer {
using namespace Qt::StringLiterals;
using namespace Internal;
namespace Internal {
std::expected<QtZpl::Document, QString> checkedParse(const QString& source, std::stop_token stop = {})
{
    if (source.size() > maximumSource || source.toUtf8().size() > maximumSource)
        return std::unexpected(u"ZPL превышает предел 8 МиБ в UTF-8."_s);
    const auto parsed = parseZpl(source, std::move(stop));
    if (!parsed) return std::unexpected(parsed.error().message);
    const QString error = validateParsed(*parsed);
    if (!error.isEmpty()) return std::unexpected(error);
    return *parsed;
}
} // namespace Internal
namespace {
std::expected<QtZpl::LabelRenderResult, QString> checkedRenderPage(const QtZpl::Document& parsed, int dpi,
                                                                  int pageIndex, bool fieldGeometry,
                                                                  std::stop_token stop)
{
    if (pageIndex < 0 || pageIndex >= parsed.labels().size())
        return std::unexpected(u"Номер этикетки вне диапазона."_s);
    // Render the exact parsed snapshot. Slicing normalized command text used
    // to lose spelling/CRLF information and require guessed offset rebasing.
    auto result = QtZpl::renderLabel(parsed, pageIndex, renderOptions(dpi, fieldGeometry, std::move(stop)));
    if (!result) return std::unexpected(result.error().message);
    return std::move(*result);
}
} // namespace

std::expected<QtZpl::RenderResult, QString> Internal::checkedRender(const QString& source, int dpi)
{
    const auto parsed = checkedParse(source);
    if (!parsed) return std::unexpected(parsed.error());
    auto result = checkedRenderPage(*parsed, dpi, 0, false, {});
    if (!result) return std::unexpected(result.error());
    return QtZpl::RenderResult{{std::move(result->image)}, std::move(result->diagnostics),
                               std::move(result->fields)};
}

namespace {
// Painted bounds of each element, taken from the render pass that produced the
// raster, in its final coordinates and before clipping to the label. Every
// drawing command belongs to the element whose ^FO...^FS range contains it.
// Returns no bounds when a field lies outside those ranges; callers then use
// model geometry.
QList<QRect> paintedElementBounds(qsizetype elementCount, const QList<SourceElementRange>& ranges,
                                  const QList<QtZpl::FieldGeometry>& fields)
{
    if (ranges.isEmpty()) return {};
    QList<QRect> bounds(elementCount);
    for (const auto& field : fields) {
        const int owner = rangeOwner(ranges, field.field.payloadSpan.start);
        if (owner < 0) return {};
        bounds[owner] |= field.paintBounds;
    }
    return bounds;
}

// Editor mode keeps a code whose own data fails to encode: QtZpl draws nothing
// for it, the error is attributed to its element and the canvas shows a
// placeholder. Every other render error fails the preview, as in print mode.
std::expected<Preview, QString> renderPreviewImpl(const TemplateSource& source, bool editor, int pageIndex,
                                                  std::stop_token stop)
{
    const auto& templateDocument = source.document();
    if (!templateDocument.visualEditable) {
        const auto parsed = parseZpl(source.text(), stop);
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
    const auto parsed = checkedParse(printerZpl(document, false), stop);
    if (!parsed) return std::unexpected(parsed.error());
    const auto rendered = checkedRenderPage(*parsed, document.dpi, pageIndex, document.visualEditable, stop);
    if (!rendered) return std::unexpected(rendered.error());
    if (rendered->image.isNull()) return std::unexpected(u"QtZpl не вернул изображение этикетки."_s);
    Preview preview;
    preview.image = rendered->image;
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
    const auto ranges = elementFieldRanges(document, *parsed);
    for (const auto& diagnostic : rendered->diagnostics) {
        if (diagnostic.severity != QtZpl::Severity::Error) {
            preview.diagnostics.append(diagnosticText(diagnostic));
            continue;
        }
        const int owner = editor && diagnostic.fieldId ? rangeOwner(ranges, *diagnostic.fieldId) : -1;
        if (owner < 0 || !isBarcodeType(document.elements[owner].type))
            return std::unexpected(diagnosticText(diagnostic));
        if (!preview.elementErrors.contains(owner)) preview.elementErrors.insert(owner, diagnosticText(diagnostic));
    }
    // Source-only labels have no editable element geometry. Their selected
    // page may differ entirely from the first label described by the model.
    if (!document.visualEditable) {
        preview.diagnostics.removeDuplicates();
        return preview;
    }
    const auto painted = paintedElementBounds(document.elements.size(), ranges, rendered->fields);
    for (qsizetype index = 0; index < document.elements.size(); ++index) {
        const auto& element = document.elements[index];
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
            preview.bounds.append(mapDocumentRect(document, logicalBounds));
            continue;
        }
        // Text, codes and images: what QtZpl painted, already in output coordinates.
        QRect bounds = painted.value(index);
        if (bounds.isEmpty())
            bounds = mapDocumentRect(document, QRect(element.x, element.y, std::max(8, element.width), std::max(8, element.height)));
        preview.bounds.append(bounds);
    }
    preview.diagnostics.removeDuplicates();
    return preview;
}

std::expected<int, QString> pageCount(const TemplateSource& source, std::stop_token stop)
{
    if (source.document().visualEditable) return 1;
    const auto parsed = checkedParse(source.text(), std::move(stop));
    if (!parsed) return std::unexpected(parsed.error());
    return static_cast<int>(parsed->labels().size());
}

QList<SourceDiagnostic> sourceDiagnosticsImpl(const TemplateSource& source, int pageIndex, std::stop_token stop)
{
    const auto& document = source.document();
    const auto localDiagnostic = [](const QString& code, const QString& message, int offset = -1) {
        QtZpl::Diagnostic detail;
        detail.severity = QtZpl::Severity::Error;
        detail.code = code;
        detail.message = message;
        detail.offset = offset;
        return SourceDiagnostic{message, offset, std::move(detail)};
    };
    if (document.visualEditable) {
        const auto valid = validateTemplate(source);
        if (!valid) return {localDiagnostic(u"designer-export"_s, valid.error())};
    }
    const QString& zpl = source.text();
    if (zpl.size() > maximumSource || zpl.toUtf8().size() > maximumSource)
        return {localDiagnostic(u"designer-source-limit"_s, u"ZPL превышает предел 8 МиБ в UTF-8."_s)};
    const auto parsed = parseZpl(zpl, stop);
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
    const auto rendered = QtZpl::renderLabel(*parsed, pageIndex, renderOptions(document.dpi, false, std::move(stop)));
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

std::expected<Preview, QString> renderEditorPreviewImpl(const TemplateSource& source, std::stop_token stop)
{
    const auto& document = source.document();
    if (!document.visualEditable) return renderPreviewImpl(source, false, 0, std::move(stop));
    if (!safeSize(document.width, document.height))
        return std::unexpected(u"Размер этикетки превышает 4096 × 4096 точек."_s);
    if (document.elements.size() > maximumElements)
        return std::unexpected(u"MVP поддерживает не более 200 элементов."_s);

    // A code excluded from the canvas keeps its model footprint, so it stays
    // selectable and movable while its data is repaired.
    const auto errorBounds = [&document](const Element& element) {
        QRect bounds(element.x, element.y, element.width, element.height);
        if (element.orientation == u'R' || element.orientation == u'B')
            bounds.setSize(QSize(element.height, element.width));
        return mapDocumentRect(document, bounds);
    };
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
            error = resolved ? validateElement(resolved->elements.first(), false) : resolved.error();
        }
        if (!error.isEmpty()) {
            errors.insert(static_cast<int>(index), error);
            bounds[index] = errorBounds(element);
        } else {
            filtered.elements.append(element);
            retainedIndices.append(static_cast<int>(index));
        }
    }
    // Encoding errors come from this single render of all remaining fields;
    // source mode and printer-preview callers never use a partial image.
    auto rendered = renderPreviewImpl(TemplateSource(filtered), true, 0, std::move(stop));
    if (!rendered) return std::unexpected(rendered.error());
    for (qsizetype index = 0; index < retainedIndices.size(); ++index)
        bounds[retainedIndices[index]] = rendered->bounds[index];
    for (auto it = rendered->elementErrors.cbegin(); it != rendered->elementErrors.cend(); ++it) {
        const int original = retainedIndices[it.key()];
        errors.insert(original, it.value());
        bounds[original] = errorBounds(document.elements[original]);
    }
    rendered->bounds = std::move(bounds);
    rendered->elementErrors = std::move(errors);
    for (auto it = rendered->elementErrors.cbegin(); it != rendered->elementErrors.cend(); ++it)
        rendered->diagnostics.append(u"%1, элемент %2: %3 Код не отображается на холсте."_s
            .arg(elementTypeName(document.elements[it.key()].type)).arg(it.key() + 1).arg(it.value()));
    return rendered;
}

void selectPage(const TemplateSource& source, PreviewSet& set, int page, std::stop_token stop)
{
    set.page = page;
    set.selectedPage = set.print;
    set.pageError = set.printError;
    if (page > 0) {
        auto rendered = renderPreviewImpl(source, false, page, stop);
        set.selectedPage = rendered ? std::move(*rendered) : Preview{};
        set.pageError = rendered ? QString{} : rendered.error();
    }
    if (!set.pageError.isEmpty() && !set.selectedPage.diagnostics.contains(set.pageError))
        set.selectedPage.diagnostics.append(set.pageError);
    set.diagnostics = sourceDiagnosticsImpl(source, page, std::move(stop));
}
} // namespace

std::expected<Preview, QString> renderPreview(const DesignDocument& document)
{
    return renderPreviewImpl(TemplateSource(document), false, 0, {});
}

std::expected<int, QString> previewPageCount(const DesignDocument& document)
{
    return pageCount(TemplateSource(document), {});
}

std::expected<Preview, QString> renderPreviewPage(const DesignDocument& document, int pageIndex)
{
    return renderPreviewImpl(TemplateSource(document), false, pageIndex, {});
}

QList<SourceDiagnostic> sourceDiagnostics(const DesignDocument& document, int pageIndex)
{
    return sourceDiagnosticsImpl(TemplateSource(document), pageIndex, {});
}

std::expected<Preview, QString> renderEditorPreview(const DesignDocument& document)
{
    return renderEditorPreviewImpl(TemplateSource(document), {});
}

std::optional<PreviewSet> buildPreviewSet(const DesignDocument& document, int page, std::stop_token stop)
{
    const TemplateSource source(document);
    PreviewSet set;
    const auto count = pageCount(source, stop);
    set.pageCount = count ? std::max(1, *count) : 1;
    auto printed = renderPreviewImpl(source, false, 0, stop);
    if (printed) set.print = *printed;
    else set.printError = printed.error();
    const bool differentVisibility = std::ranges::any_of(document.elements,
        [](const Element& element) { return element.editorVisible != element.printable; });
    if (!printed || differentVisibility) {
        if (auto editor = renderEditorPreviewImpl(source, stop)) set.editor = std::move(*editor);
        else set.editor.diagnostics.append(editor.error());
        if (!set.printError.isEmpty()) set.editor.diagnostics.append(set.printError);
        set.editor.diagnostics.removeDuplicates();
    } else set.editor = std::move(*printed);
    selectPage(source, set, std::clamp(page, 0, set.pageCount - 1), stop);
    if (stop.stop_requested()) return std::nullopt;
    return set;
}

void selectPreviewPage(const DesignDocument& document, PreviewSet& set, int page)
{
    selectPage(TemplateSource(document), set, page, {});
}

std::expected<SourceLabels, QString> sourceLabels(const QString& source)
{
    const auto parsed = parseZpl(source);
    if (!parsed) return std::unexpected(parsed.error().message);
    SourceLabels result;
    for (const auto& label : parsed->labels()) {
        int quantity = 1;
        for (const auto& command : label.commands()) {
            if (const auto* print = std::get_if<QtZpl::PrintQuantity>(&command.payload)) quantity = print->quantity;
            else if (const auto* field = std::get_if<QtZpl::FieldData>(&command.payload))
                result.placeholderFields |= field->rawData.contains(u"{{") || field->rawData.contains(u"[[");
        }
        ++result.labels;
        result.labelsPerRepeat += std::max(1, quantity);
    }
    return result;
}

} // namespace Designer
