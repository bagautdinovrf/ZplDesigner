#pragma once

#include "design_document.hpp"
#include "document_revision.hpp"
#include "preview_pages.hpp"

#include <QObject>
#include <QThreadPool>
#include <QTimer>

#include <memory>
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

// Owns the preview lifecycle, not QtZpl's rendering implementation. Raster,
// element bounds, errors and diagnostics are published together from one
// copied document. A debounced refresh renders on a worker thread and is
// cancelled by a newer snapshot; refresh() publishes synchronously and reuses a
// matching render already in progress. Views must not retain bounds across
// document revisions.
class PreviewCoordinator final : public QObject {
    Q_OBJECT

public:
    explicit PreviewCoordinator(QObject* parent = nullptr);
    ~PreviewCoordinator() override;

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
    struct Job;

    void startBackground();
    void cancelBackground();
    void finishBackground(const std::shared_ptr<Job>& job);
    void publish(const PreviewRequestIdentity& request, int requestedPage, PreviewSet set);

    QTimer debounce_;
    QThreadPool workers_;
    std::shared_ptr<Job> job_;
    std::optional<Snapshot> snapshot_;
    PreviewSet results_;
    int page_ = 0;
    int pageCount_ = 1;
    bool ready_ = false;
};

} // namespace Designer
