#pragma once

#include "design_document.hpp"
#include <QtZpl/model.hpp>

namespace Designer {
// Indices are zero-based. The whole source is safety-checked before a page is
// rendered; only the requested image is allocated.
std::expected<int, QString> previewPageCount(const DesignDocument& document);
std::expected<Preview, QString> renderPreviewPage(const DesignDocument& document, int pageIndex);
struct SourceDiagnostic {
    QString text;
    int offset = -1;
    // Preserve machine-readable severity/code, original UTF-16 span and field
    // ownership. text/offset are presentation conveniences, never policy inputs.
    QtZpl::Diagnostic detail;
};
QList<SourceDiagnostic> sourceDiagnostics(const DesignDocument& document, int pageIndex = 0);
} // namespace Designer
