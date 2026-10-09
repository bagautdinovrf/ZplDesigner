#include "data_matrix_values.hpp"
#include "gs1_data.hpp"
#include "value_placeholders.hpp"

namespace Designer {
using namespace Qt::StringLiterals;

bool supportsValueParameters(ElementType type)
{
    return type == ElementType::Text || type == ElementType::Table || isBarcodeType(type);
}

std::expected<QStringList, QString> dataMatrixParameters(const DesignDocument& document)
{
    QStringList parameters;
    for (const auto& element : document.elements) {
        if (!supportsValueParameters(element.type) || element.literalText) continue;
        const auto fields = element.type == ElementType::Table ? element.tableCells : QStringList{element.text};
        for (const auto& field : fields) {
            const auto names = requiredValueParameters(field);
            if (!names) return std::unexpected(names.error());
            for (const auto& name : *names)
                if (!parameters.contains(name)) parameters.append(name);
        }
    }
    return parameters;
}

std::expected<DesignDocument, QString> resolveDataMatrixValues(const DesignDocument& document, bool allowUnresolvedText)
{
    auto resolved = document;
    bool substituted = false;
    for (auto& element : resolved.elements) {
        if (!element.printable) continue;
        if (!supportsValueParameters(element.type) || element.literalText) continue;
        if (element.type == ElementType::Table) {
            // Resolve a table atomically: a partially resolved cell must not
            // become a new template during a later preview pass.
            QStringList cells;
            bool complete = true;
            for (const auto& cell : element.tableCells) {
                const auto value = resolveValueTemplate(cell, document.previewValues);
                if (!value) {
                    if (!allowUnresolvedText) return std::unexpected(value.error());
                    complete = false; break;
                }
                cells.append(*value);
            }
            if (complete) {
                element.tableCells = std::move(cells); element.literalText = true; substituted = true;
            }
            continue;
        }
        const auto tokens = valuePlaceholders(element.text);
        if (!tokens) return std::unexpected(tokens.error());
        // Built-ins such as {{GS}} still need expansion even when no user
        // values are required.
        if (tokens->isEmpty()) continue;
        if (element.type == ElementType::DataMatrix && element.gs1) {
            const auto data = resolveGs1Template(element.text, document.previewValues);
            if (!data) return std::unexpected(data.error());
            element.text = data->canonicalText;
        } else {
            const auto data = resolveValueTemplate(element.text, document.previewValues);
            if (!data) {
                if (allowUnresolvedText && element.type == ElementType::Text) continue;
                return std::unexpected(data.error());
            }
            element.text = *data;
            element.literalText = true;
        }
        substituted = true;
    }
    if (substituted) resolved.originalZpl.clear();
    return resolved;
}
} // namespace Designer
