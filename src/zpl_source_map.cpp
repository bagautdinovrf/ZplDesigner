#include "zpl_source_map.hpp"

#include <QtZpl/qtzpl.hpp>

namespace Designer {

QList<SourceElementRange> sourceElementRanges(const DesignDocument& document,
                                            const QString& source)
{
    if (!document.visualEditable || document.elements.isEmpty()) return {};

    QList<int> fieldOwners;
    for (qsizetype index = 0; index < document.elements.size(); ++index) {
        const auto& element = document.elements[index];
        if (!element.printable) continue;
        const auto count = element.type == ElementType::Table ? tableElements(element).size() : 1;
        for (qsizetype field = 0; field < count; ++field) fieldOwners.append(int(index));
    }
    if (fieldOwners.isEmpty()) return {};

    const auto parsed = QtZpl::parse(source);
    if (!parsed || parsed->labels().size() != 1) return {};

    QList<SourceElementRange> ranges;
    ranges.reserve(fieldOwners.size());
    qsizetype fieldStart = -1;
    for (const auto& command : parsed->labels().first().commands()) {
        if (std::holds_alternative<QtZpl::FieldOrigin>(command.payload)
            || std::holds_alternative<QtZpl::FieldTypeset>(command.payload)) {
            if (fieldStart >= 0) return {};
            fieldStart = command.offset;
        } else if (std::holds_alternative<QtZpl::FieldSeparator>(command.payload) && fieldStart >= 0) {
            if (ranges.size() >= fieldOwners.size()) return {};
            ranges.append({fieldStart, command.offset + 3 - fieldStart, fieldOwners[ranges.size()]});
            fieldStart = -1;
        } else if (std::holds_alternative<QtZpl::Comment>(command.payload) && fieldStart >= 0) {
            // Editable documents never contain comments inside a field. Avoid
            // treating an ambiguous/comment-only field as a drawable object.
            return {};
        }
    }
    if (fieldStart >= 0 || ranges.size() != fieldOwners.size()) return {};
    return ranges;
}

} // namespace Designer
