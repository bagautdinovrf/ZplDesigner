#pragma once

#include <QUuid>

namespace Designer {

// Revisions are meaningful only inside one document instance. Opening/resetting
// a document changes instance even when its content counters happen to match.
struct DocumentRevision {
    QUuid instance;
    quint64 document = 0;
    quint64 values = 0;

    friend bool operator==(const DocumentRevision&, const DocumentRevision&) = default;
};

} // namespace Designer
