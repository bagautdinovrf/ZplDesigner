#include "zpl_source_map.hpp"
#include "design_internal.hpp"
#include "eac_mark.hpp"

#include <QtZpl/qtzpl.hpp>

#include <algorithm>

namespace Designer {

qsizetype Internal::commandEnd(const QtZpl::Document& document, const QtZpl::Command& command)
{
    const auto text = document.sourceText(command.sourceSpan);
    qsizetype length = text.size();
    while (length > 0 && text[length - 1].isSpace()) --length;
    return command.sourceSpan.start + length;
}

qsizetype elementFieldCount(const Element& element)
{
    return element.type == ElementType::Table ? tableElements(element).size()
        : element.type == ElementType::Eac ? eacElements(element).size() : 1;
}

QList<SourceElementRange> sourceElementRanges(const DesignDocument& document,
                                            const QString& source)
{
    if (!document.visualEditable || document.elements.isEmpty()) return {};
    const auto parsed = Internal::parseZpl(source);
    return parsed ? Internal::elementFieldRanges(document, *parsed) : QList<SourceElementRange>{};
}

QList<SourceElementRange> Internal::elementFieldRanges(const DesignDocument& document,
                                                      const QtZpl::Document& parsed)
{
    if (!document.visualEditable || document.elements.isEmpty() || parsed.labels().size() != 1) return {};

    QList<int> fieldOwners;
    for (qsizetype index = 0; index < document.elements.size(); ++index) {
        const auto& element = document.elements[index];
        if (!element.printable) continue;
        const auto count = elementFieldCount(element);
        for (qsizetype field = 0; field < count; ++field) fieldOwners.append(int(index));
    }
    if (fieldOwners.isEmpty()) return {};

    QList<SourceElementRange> ranges;
    ranges.reserve(fieldOwners.size());
    qsizetype fieldStart = -1;
    for (const auto& command : parsed.labels().first().commands()) {
        if (std::holds_alternative<QtZpl::FieldOrigin>(command.payload)
            || std::holds_alternative<QtZpl::FieldTypeset>(command.payload)) {
            if (fieldStart >= 0) return {};
            fieldStart = command.sourceSpan.start;
        } else if (std::holds_alternative<QtZpl::FieldSeparator>(command.payload) && fieldStart >= 0) {
            if (ranges.size() >= fieldOwners.size()) return {};
            ranges.append({fieldStart, commandEnd(parsed, command) - fieldStart, fieldOwners[ranges.size()]});
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

int Internal::rangeOwner(const QList<SourceElementRange>& ranges, qsizetype offset)
{
    // Ranges are disjoint and ordered by their position in the source.
    const auto range = std::ranges::upper_bound(ranges, offset, {}, &SourceElementRange::start);
    if (range == ranges.cbegin()) return -1;
    const auto& candidate = *std::prev(range);
    return offset < candidate.start + candidate.length ? candidate.elementIndex : -1;
}

} // namespace Designer
