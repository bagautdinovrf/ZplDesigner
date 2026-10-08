#include "design_document.hpp"
#include "design_internal.hpp"
#include "data_matrix_values.hpp"
#include "preview_pages.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Designer {
using namespace Qt::StringLiterals;
using namespace Internal;
namespace Internal {
QString labelSource(const QtZpl::Document& parsed, const QString& source, int pageIndex)
{
    const auto& commands = parsed.labels()[pageIndex].commands();
    if (commands.isEmpty()) return {};
    return source.mid(commands.first().offset,
                      commands.last().offset + commands.last().source.size() - commands.first().offset);
}

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
    auto result = QtZpl::render(labelSource(*parsed, source, pageIndex), {}, options);
    if (!result) return std::unexpected(result.error().message);
    const auto& commands = parsed->labels()[pageIndex].commands();
    if (!commands.isEmpty())
        for (auto& diagnostic : result->diagnostics)
            if (diagnostic.offset >= 0) diagnostic.offset += commands.first().offset;
    return *result;
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
            const auto error = validateElement(element, false);
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
            || element.type == ElementType::Diagonal || element.type == ElementType::Table) {
            const int height = element.type == ElementType::Circle ? element.width : element.height;
            const int justification = element.originJustification < 0 ? document.defaultJustification : element.originJustification;
            const bool table = element.type == ElementType::Table;
            const QRect logicalBounds(element.x - (!table && justification == 1 ? element.width : 0),
                element.y - (!table && element.baselineOrigin ? height : 0), element.width, height);
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
        const auto individual = checkedRender(printerZpl(single, false), single.dpi);
        QRect bounds;
        if (individual && !individual->labels.isEmpty()) bounds = inkBounds(individual->labels.first());
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
    if (document.visualEditable) {
        const auto valid = validateForExport(document);
        if (!valid) return {{valid.error(), -1}};
    }
    const QString source = toZpl(document);
    if (source.size() > maximumSource || source.toUtf8().size() > maximumSource)
        return {{u"ZPL превышает предел 8 МиБ в UTF-8."_s, -1}};
    const auto parsed = QtZpl::parse(source);
    if (!parsed) return {{parsed.error().message, static_cast<int>(parsed.error().offset)}};
    QList<SourceDiagnostic> diagnostics;
    const auto append = [&diagnostics](const QtZpl::Diagnostic& diagnostic) {
        const auto text = diagnosticText(diagnostic);
        const int offset = static_cast<int>(diagnostic.offset);
        if (std::ranges::none_of(diagnostics, [&](const SourceDiagnostic& existing) {
            return existing.text == text && existing.offset == offset;
        })) diagnostics.append({text, offset});
    };
    for (const auto& diagnostic : parsed->diagnostics()) append(diagnostic);
    qsizetype safetyOffset = -1;
    const QString safety = validateParsed(*parsed, &safetyOffset);
    if (!safety.isEmpty()) {
        diagnostics.append({safety, static_cast<int>(safetyOffset)});
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
    const auto rendered = checkedRenderPage(source, document.dpi, pageIndex);
    if (rendered) {
        for (const auto& diagnostic : rendered->diagnostics) append(diagnostic);
    } else {
        const int offset = pageIndex >= 0 && pageIndex < parsed->labels().size()
            && !parsed->labels()[pageIndex].commands().isEmpty()
            ? static_cast<int>(parsed->labels()[pageIndex].commands().first().offset) : -1;
        diagnostics.append({rendered.error(), offset});
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
                if (error.isEmpty()) {
                    const auto rendered = checkedRender(printerZpl(*resolved, false), document.dpi);
                    if (!rendered) error = rendered.error();
                    else for (const auto& diagnostic : rendered->diagnostics)
                        if (diagnostic.severity == QtZpl::Severity::Error) {
                            error = diagnosticText(diagnostic);
                            break;
                        }
                }
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
