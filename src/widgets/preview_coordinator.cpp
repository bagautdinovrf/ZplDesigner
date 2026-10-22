#include "preview_coordinator.hpp"

#include <QMutex>

#include <algorithm>
#include <stop_token>
#include <utility>

namespace Designer {

struct PreviewCoordinator::Job {
    DesignDocument document;
    PreviewRequestIdentity request;
    int page = 0;
    std::stop_source stop;
    QMutex mutex;
    // Empty until the worker finishes, and also when it was cancelled.
    std::optional<PreviewSet> result;
};

PreviewCoordinator::PreviewCoordinator(QObject* parent) : QObject(parent)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(90);
    connect(&debounce_, &QTimer::timeout, this, &PreviewCoordinator::startBackground);
    // One render at a time; a newer snapshot cancels the running one first.
    workers_.setMaxThreadCount(1);
}

PreviewCoordinator::~PreviewCoordinator()
{
    // A worker posts its result to this object; finish it before destruction.
    cancelBackground();
    workers_.waitForDone();
}

void PreviewCoordinator::setSnapshot(DesignDocument document, DocumentRevision revision)
{
    if (snapshot_ && snapshot_->revision == revision && snapshot_->document.dpi == document.dpi)
        return;
    const bool newDocument = !snapshot_ || snapshot_->revision.instance != revision.instance;
    snapshot_ = Snapshot{std::move(document), std::move(revision)};
    // Invalidate geometry immediately: an old positional bounds array cannot
    // safely accompany reordered or deleted elements while debounce is pending.
    cancelBackground();
    results_ = {};
    ready_ = false;
    if (newDocument) {
        page_ = 0;
        pageCount_ = 1;
    }
    debounce_.start();
}

void PreviewCoordinator::startBackground()
{
    debounce_.stop();
    if (!snapshot_ || ready_) return;
    const auto request = requestIdentity(PreviewMode::Editor);
    if (job_ && job_->request == request && job_->page == page_) return;
    cancelBackground();
    auto job = std::make_shared<Job>();
    job->document = snapshot_->document;
    job->request = request;
    job->page = page_;
    job_ = job;
    workers_.start([this, job] {
        auto result = buildPreviewSet(job->document, job->page, job->stop.get_token());
        {
            const QMutexLocker lock(&job->mutex);
            job->result = std::move(result);
        }
        QMetaObject::invokeMethod(this, [this, job] { finishBackground(job); }, Qt::QueuedConnection);
    });
}

void PreviewCoordinator::cancelBackground()
{
    if (!job_) return;
    job_->stop.request_stop();
    job_.reset();
}

void PreviewCoordinator::finishBackground(const std::shared_ptr<Job>& job)
{
    if (job != job_) return;
    job_.reset();
    std::optional<PreviewSet> result;
    {
        const QMutexLocker lock(&job->mutex);
        result = std::move(job->result);
    }
    if (result) publish(job->request, job->page, std::move(*result));
}

void PreviewCoordinator::refresh()
{
    debounce_.stop();
    if (!snapshot_ || ready_) return;
    const auto request = requestIdentity(PreviewMode::Editor);
    const int requestedPage = page_;
    std::optional<PreviewSet> result;
    if (job_ && job_->request == request && job_->page == requestedPage) {
        // Callers read bounds right after refresh(). Wait for the same
        // request instead of rendering it a second time.
        const auto job = std::exchange(job_, nullptr);
        workers_.waitForDone();
        const QMutexLocker lock(&job->mutex);
        result = std::move(job->result);
    }
    cancelBackground();
    if (!result) {
        const auto document = snapshot_->document;
        result = buildPreviewSet(document, requestedPage);
    }
    if (result) publish(request, requestedPage, std::move(*result));
}

void PreviewCoordinator::publish(const PreviewRequestIdentity& request, int requestedPage, PreviewSet set)
{
    // The guard also covers reentrant document replacement during a future
    // adapter call. Never publish part of the old request to a new document.
    if (ready_ || !isCurrent(request) || page_ != requestedPage) return;
    page_ = set.page;
    pageCount_ = set.pageCount;
    results_ = std::move(set);
    ready_ = true;
    emit previewsChanged();
}

void PreviewCoordinator::selectPage(int page)
{
    if (!snapshot_) return;
    if (!ready_) refresh();
    const int selected = std::clamp(page, 0, pageCount_ - 1);
    if (selected == page_) return;
    page_ = selected;
    const auto request = requestIdentity(PreviewMode::Print);
    const auto document = snapshot_->document;
    auto result = results_;
    selectPreviewPage(document, result, selected);
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
const Preview& PreviewCoordinator::pagePreview() const { return results_.selectedPage; }
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
