#pragma once

#include "design_document.hpp"

namespace Designer {

struct SourceElementRange {
    qsizetype start = 0;
    qsizetype length = 0;
    int elementIndex = -1;
};

// Physical ^FO...^FS fields a printable element writes to printer ZPL: one per
// table or EAC part, otherwise exactly one. Fields keep the element order.
qsizetype elementFieldCount(const Element& element);

// Call when an applied document/source changes, then cache for cursor selection.
// source must be the document's toZpl() output, with the editor's newline
// normalization already applied. Offsets use UTF-16 positions in that exact
// string. Each physical field has a separate range; table fields share an
// element index. Comments, settings and non-printing objects have no range.
// Unsupported documents or inconsistent field counts return no ranges.
QList<SourceElementRange> sourceElementRanges(const DesignDocument& document,
                                            const QString& source);

} // namespace Designer
