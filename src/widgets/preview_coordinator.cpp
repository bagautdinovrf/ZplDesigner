#include "preview_coordinator.hpp"

#include <algorithm>
#include <utility>

namespace Designer {

PreviewCoordinator::PreviewCoordinator(QObject* parent) : QObject(parent)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(90);
    connect(&debounce_, &QTimer::timeout, this, &PreviewCoordinator::refresh);
}

void PreviewCoordinator::setSnapshot(DesignDocument document, DocumentRevision revision)
{
    if (snapshot_ && snapshot_->revision == revision && snapshot_->document.dpi == document.dpi)
        return;
    const bool newDocument = !snapshot_ || snapshot_->revision.instance != revision.instance;
    snapshot_ = Snapshot{std::move(document), std::move(revision)};
    // Invalidate geometry immediately: an old positional bounds array cannot
    // safely accompany reordered or deleted elements while debounce is pending.
    results_ = {};
    ready_ = false;
    if (newDocument) {
        page_ = 0;
        pageCount_ = 1;
    }
    debounce_.start();
}

void PreviewCoordinator::refresh()
{
    debounce_.stop();
    if (!snapshot_ || ready_) return;
    const auto snapshot = *snapshot_;
    const auto editorRequest = requestIdentity(PreviewMode::Editor);
    const int requestedPage = page_;
    const auto count = previewPageCount(snapshot.document);
    const int pages = count ? std::max(1, *count) : 1;
    const int selectedPage = std::clamp(page_, 0, pages - 1);
    Results result;
    auto printed = renderPreview(snapshot.document);
    if (printed) result.print = *printed;
    else result.printError = printed.error();
    const bool differentVisibility = std::ranges::any_of(snapshot.document.elements,
        [](const Element& element) { return element.editorVisible != element.printable; });
    if (!printed || differentVisibility) {
        if (auto editor = renderEditorPreview(snapshot.document)) result.editor = std::move(*editor);
        else result.editor.diagnostics.append(editor.error());
        if (!result.printError.isEmpty()) result.editor.diagnostics.append(result.printError);
        result.editor.diagnostics.removeDuplicates();
    } else result.editor = std::move(*printed);
    renderPage(snapshot, result, selectedPage);
    // The guard also covers reentrant document replacement during a future
    // adapter call. Never publish part of the old request to a new document.
    if (!isCurrent(editorRequest) || page_ != requestedPage) return;
    page_ = selectedPage;
    pageCount_ = pages;
    results_ = std::move(result);
    ready_ = true;
    emit previewsChanged();
}

void PreviewCoordinator::renderPage(const Snapshot& snapshot, Results& result, int page) const
{
    result.page = result.print;
    result.pageError = result.printError;
    if (page > 0) {
        auto rendered = renderPreviewPage(snapshot.document, page);
        result.page = rendered ? std::move(*rendered) : Preview{};
        result.pageError = rendered ? QString{} : rendered.error();
    }
    if (!result.pageError.isEmpty() && !result.page.diagnostics.contains(result.pageError))
        result.page.diagnostics.append(result.pageError);
    result.diagnostics = Designer::sourceDiagnostics(snapshot.document, page);
}

void PreviewCoordinator::selectPage(int page)
{
    if (!snapshot_) return;
    if (!ready_) refresh();
    const int selected = std::clamp(page, 0, pageCount_ - 1);
    if (selected == page_) return;
    page_ = selected;
    const auto request = requestIdentity(PreviewMode::Print);
    const auto snapshot = *snapshot_;
    auto result = results_;
    renderPage(snapshot, result, selected);
    if (!isCurrent(request)) return;
    results_ = std::move(result);
    emit previewsChanged();
}

bool PreviewCoordinator::pending() const { return snapshot_.has_value() && !ready_; }
int PreviewCoordinator::page() const { return page_; }
int PreviewCoordinator::pageCount() const { return pageCount_; }
int PreviewCoordinator::dpi() const { return snapshot_ ? snapshot_->document.dpi : 203; }
const Preview& PreviewCoordinator::editorPreview() const { return results_.editor; }
const Preview& PreviewCoordinator::printPreview() const { return results_.print; }
const Preview& PreviewCoordinator::pagePreview() const { return results_.page; }
const QString& PreviewCoordinator::printError() const { return results_.printError; }
const QString& PreviewCoordinator::pageError() const { return results_.pageError; }
const QList<SourceDiagnostic>& PreviewCoordinator::sourceDiagnostics() const { return results_.diagnostics; }

QList<SourceDiagnostic> PreviewCoordinator::sourceDiagnostics(const DesignDocument& source) const
{
    return Designer::sourceDiagnostics(source, page_);
}

PreviewRequestIdentity PreviewCoordinator::requestIdentity(PreviewMode mode) const
{
    return {snapshot_ ? snapshot_->revision : DocumentRevision{}, dpi(),
            mode == PreviewMode::Editor ? 0 : page_, mode};
}

bool PreviewCoordinator::isCurrent(const PreviewRequestIdentity& request) const
{
    return snapshot_.has_value() && request == requestIdentity(request.mode);
}

} // namespace Designer
