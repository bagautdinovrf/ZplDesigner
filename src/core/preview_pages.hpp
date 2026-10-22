#pragma once

#include "design_document.hpp"
#include <QtZpl/model.hpp>

#include <optional>
#include <stop_token>

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

// Everything published for one document revision: the canvas (editor) preview,
// the printer preview, the selected source page and its diagnostics.
struct PreviewSet {
    int pageCount = 1;
    int page = 0;
    Preview editor;
    Preview print;
    Preview selectedPage;
    QString printError;
    QString pageError;
    QList<SourceDiagnostic> diagnostics;
};
// Serializes the document once and parses each distinct generated source once.
// Safe to call on a worker thread. Returns nullopt only when stop is requested.
std::optional<PreviewSet> buildPreviewSet(const DesignDocument& document, int page,
                                          std::stop_token stop = {});
// Replaces the selected page and its diagnostics in a set built for document.
void selectPreviewPage(const DesignDocument& document, PreviewSet& set, int page);

// Labels in a source and how many one print repeat produces (^PQ each).
struct SourceLabels {
    int labels = 0;
    int labelsPerRepeat = 0;
    // A field still spells a {{...}} or [[...]] placeholder.
    bool placeholderFields = false;
};
std::expected<SourceLabels, QString> sourceLabels(const QString& source);
} // namespace Designer
