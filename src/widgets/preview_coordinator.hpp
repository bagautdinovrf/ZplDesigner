#pragma once

#include "design_document.hpp"
#include "document_revision.hpp"
#include "preview_pages.hpp"

#include <QObject>
#include <QTimer>

#include <optional>

namespace Designer {

enum class PreviewMode { Editor, Print };

struct PreviewRequestIdentity {
    DocumentRevision revision;
    int dpi = 203;
    int page = 0;
    PreviewMode mode = PreviewMode::Editor;

    friend bool operator==(const PreviewRequestIdentity&, const PreviewRequestIdentity&) = default;
};

// Owns the synchronous preview lifecycle, not QtZpl's rendering implementation.
// Raster, element bounds, errors and diagnostics are published together from
// one copied document. Views must not retain bounds across document revisions.
class PreviewCoordinator final : public QObject {
    Q_OBJECT

public:
    explicit PreviewCoordinator(QObject* parent = nullptr);

    void setSnapshot(DesignDocument document, DocumentRevision revision);
    void refresh();
    void selectPage(int page);

    [[nodiscard]] bool pending() const;
    [[nodiscard]] int page() const;
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] int dpi() const;
    [[nodiscard]] const Preview& editorPreview() const;
    [[nodiscard]] const Preview& printPreview() const;
    [[nodiscard]] const Preview& pagePreview() const;
    [[nodiscard]] const QString& printError() const;
    [[nodiscard]] const QString& pageError() const;
    [[nodiscard]] const QList<SourceDiagnostic>& sourceDiagnostics() const;
    // Draft diagnostics address that exact source, independently of the
    // applied-document raster. They never replace the applied snapshot.
    [[nodiscard]] QList<SourceDiagnostic> sourceDiagnostics(const DesignDocument& source) const;
    [[nodiscard]] PreviewRequestIdentity requestIdentity(PreviewMode mode) const;
    [[nodiscard]] bool isCurrent(const PreviewRequestIdentity& request) const;

signals:
    void previewsChanged();

private:
    struct Snapshot {
        DesignDocument document;
        DocumentRevision revision;
    };
    struct Results {
        Preview editor;
        Preview print;
        Preview page;
        QString printError;
        QString pageError;
        QList<SourceDiagnostic> diagnostics;
    };

    void renderPage(const Snapshot& snapshot, Results& result, int page) const;

    QTimer debounce_;
    std::optional<Snapshot> snapshot_;
    Results results_;
    int page_ = 0;
    int pageCount_ = 1;
    bool ready_ = false;
};

} // namespace Designer
