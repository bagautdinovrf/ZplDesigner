#include "source_editor_adapter.hpp"

#include "document_session.hpp"
#include "zpl_source_editor.hpp"

#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>

namespace Designer {
namespace {
QString editorSource(QString source)
{
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source.replace(u'\r', u'\n');
    return source;
}
} // namespace

SourceEditorAdapter::SourceEditorAdapter(DocumentSession& session, ZplSourceEditor& editor, QObject* parent)
    : QObject(parent), session_(session), editor_(editor)
{
    applyTimer_.setSingleShot(true);
    applyTimer_.setInterval(300);
    connect(&applyTimer_, &QTimer::timeout, this, &SourceEditorAdapter::applyScheduled);
    connect(&editor_, &QPlainTextEdit::textChanged, this, &SourceEditorAdapter::textChanged);
    connect(&editor_, &QPlainTextEdit::selectionChanged, this, [this] {
        sourceSelection(selectionSyncEnabled_, editor_.hasFocus());
    });
    connect(&editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (editor_.hasFocus()) sourceSelection(selectionSyncEnabled_, true);
    });
    connect(&editor_, &ZplSourceEditor::sourceCursorActivated, this, [this] {
        sourceSelection(selectionSyncEnabled_, true);
    });
}

void SourceEditorAdapter::syncDocument()
{
    cancelApply();
    const QScopedValueRollback guard(syncingDocument_, true);
    const auto& document = session_.document();
    if (mappedInstance_ != session_.revision().instance) {
        cancelSelection();
        selectedIndices_.clear();
    }
    mappedInstance_ = session_.revision().instance;
    mappedDocumentRevision_ = session_.revision().document;
    // Do not expose incomplete generated ZPL for an intermediate invalid model.
    // The complete visual document remains available while its data is repaired.
    const auto exported = document.visualEditable ? exportableZpl(document)
                                                  : std::expected<QString, QString>(toZpl(document));
    editor_.setReadOnly(!exported && !session_.hasPendingSource());
    editor_.setPlaceholderText(exported ? QString{} : QStringLiteral(
        "ZPL пока недоступен: %1\nИсправьте данные во вкладке «Дизайн». Все элементы сохранены в макете.").arg(exported.error()));
    mappedSource_ = exported ? editorSource(*exported) : QString{};
    const auto source = session_.hasPendingSource() ? editorSource(session_.pendingSource()) : mappedSource_;
    if (editor_.toPlainText() != source) {
        // A visual edit may move field ranges. Preserve the selected objects,
        // without interpreting the old source caret in the new generated text.
        cancelSelection();
        const int oldPosition = editor_.textCursor().position();
        const int oldAnchor = editor_.textCursor().anchor();
        const int vertical = editor_.verticalScrollBar()->value();
        const int horizontal = editor_.horizontalScrollBar()->value();
        editor_.setPlainText(source);
        auto cursor = editor_.textCursor();
        cursor.setPosition(std::min(oldAnchor, static_cast<int>(source.size())));
        cursor.setPosition(std::min(oldPosition, static_cast<int>(source.size())), QTextCursor::KeepAnchor);
        editor_.setTextCursor(cursor);
        editor_.verticalScrollBar()->setValue(vertical);
        editor_.horizontalScrollBar()->setValue(horizontal);
    }
    mappedSourceRevision_ = session_.hasPendingSource() ? -1 : editor_.document()->revision();
    ranges_ = sourceElementRanges(document, mappedSource_);
    updateHighlights();
}

void SourceEditorAdapter::textChanged()
{
    if (syncingDocument_ || editor_.isReadOnly()) return;
    session_.setPendingSource(editor_.toPlainText());
    mappedSourceRevision_ = session_.hasPendingSource() ? -1 : editor_.document()->revision();
    cancelApply();
    // The window refreshes status and may schedule a live apply after this.
    emit sourceEdited();
    sourceSelection(selectionSyncEnabled_, editor_.hasFocus());
    updateHighlights();
}

bool SourceEditorAdapter::sourceMatchesDocument()
{
    if (session_.hasPendingSource() || !session_.document().visualEditable
        || mappedInstance_ != session_.revision().instance
        || mappedDocumentRevision_ != session_.revision().document) return false;
    const int revision = editor_.document()->revision();
    if (revision != mappedSourceRevision_) {
        // Syntax highlighting can change QTextDocument's revision without
        // changing text. Palette changes must keep a valid source map.
        if (editor_.toPlainText() != mappedSource_) return false;
        mappedSourceRevision_ = revision;
    }
    return true;
}

void SourceEditorAdapter::setSelectionSyncEnabled(bool enabled)
{
    selectionSyncEnabled_ = enabled;
    if (!enabled) cancelSelection();
    updateHighlights();
}

void SourceEditorAdapter::setSelection(const QList<int>& indices, bool reveal)
{
    selectedIndices_ = indices;
    updateHighlights(reveal);
}

void SourceEditorAdapter::updateHighlights(bool reveal)
{
    QList<QPair<int, int>> highlights;
    if (selectionSyncEnabled_ && sourceMatchesDocument()) {
        for (const auto& range : ranges_) {
            if (selectedIndices_.contains(range.elementIndex))
                highlights.append({static_cast<int>(range.start), static_cast<int>(range.length)});
        }
    }
    editor_.setObjectHighlights(highlights, reveal && editor_.isVisible());
}

void SourceEditorAdapter::sourceSelection(bool syncEnabled, bool allowCaret)
{
    selectionSyncEnabled_ = syncEnabled;
    if (syncingDocument_) return;
    if (!selectionSyncEnabled_) {
        cancelSelection();
        return;
    }
    const QScopedValueRollback guard(syncingSelection_, true);
    const auto cursor = editor_.textCursor();
    const bool selectCaret = allowCaret && !cursor.hasSelection();
    if (!sourceMatchesDocument()) {
        sourceSelectionActive_ = cursor.hasSelection() || selectCaret;
        sourceCaretActive_ = selectCaret;
        return;
    }
    if (!cursor.hasSelection() && !selectCaret) {
        if (sourceSelectionActive_) {
            cancelSelection();
            emit selectedIndicesRequested({});
        }
        return;
    }
    sourceSelectionActive_ = true;
    sourceCaretActive_ = selectCaret;
    QList<int> indices;
    if (selectCaret) {
        // Exact field first, even when several fields share a physical line.
        for (const auto& range : ranges_) {
            if (cursor.position() >= range.start && cursor.position() < range.start + range.length) {
                indices.append(range.elementIndex);
                break;
            }
        }
    }
    if (!selectCaret || indices.isEmpty()) {
        const auto block = cursor.block();
        const auto start = selectCaret ? block.position() : cursor.selectionStart();
        const auto end = selectCaret ? block.position() + block.length() - 1 : cursor.selectionEnd();
        for (const auto& range : ranges_) {
            if (range.start < end && range.start + range.length > start && !indices.contains(range.elementIndex))
                indices.append(range.elementIndex);
        }
    }
    emit selectedIndicesRequested(indices);
}

void SourceEditorAdapter::cancelSelection()
{
    sourceSelectionActive_ = false;
    sourceCaretActive_ = false;
}

void SourceEditorAdapter::resumeSelection()
{
    if (sourceSelectionActive_) sourceSelection(selectionSyncEnabled_, sourceCaretActive_);
}

bool SourceEditorAdapter::isSyncingSelection() const
{
    return syncingSelection_;
}

void SourceEditorAdapter::scheduleApply()
{
    cancelApply();
    if (!session_.hasPendingSource()) return;
    request_ = ApplyRequest{session_.revision(), session_.sourceRevision()};
    applyTimer_.start();
}

void SourceEditorAdapter::cancelApply()
{
    applyTimer_.stop();
    request_.reset();
}

std::expected<bool, QString> SourceEditorAdapter::apply()
{
    cancelApply();
    return session_.applySource();
}

void SourceEditorAdapter::applyScheduled()
{
    if (!request_) return;
    const auto request = *request_;
    request_.reset();
    // A reset/open or a newer edit may have happened after scheduling. A stale
    // callback has no authority over that state and must not show an old error.
    if (request.document != session_.revision() || request.source != session_.sourceRevision()) return;
    const auto result = session_.applySource(request.document, request.source);
    emit applyFinished(result.has_value(), result ? QString{} : result.error());
}

} // namespace Designer
