#pragma once

#include "document_revision.hpp"
#include "zpl_source_map.hpp"

#include <QObject>
#include <QTimer>

#include <expected>
#include <optional>

namespace Designer {

class DocumentSession;
class ZplSourceEditor;

// View state for the source editor. The session owns the draft and decides
// whether it can be applied; this adapter owns text mapping, cursor selection
// and the delayed request to apply a particular draft to a particular document.
class SourceEditorAdapter final : public QObject {
    Q_OBJECT
public:
    SourceEditorAdapter(DocumentSession& session, ZplSourceEditor& editor, QObject* parent = nullptr);

    void syncDocument();
    [[nodiscard]] bool sourceMatchesDocument();
    void setSelectionSyncEnabled(bool enabled);
    void setSelection(const QList<int>& indices, bool reveal = false);
    void sourceSelection(bool syncEnabled, bool allowCaret = false);
    void cancelSelection();
    void resumeSelection();
    [[nodiscard]] bool isSyncingSelection() const;

    void scheduleApply();
    void cancelApply();
    [[nodiscard]] std::expected<bool, QString> apply();

signals:
    // Emitted after an editor text change has updated the session draft.
    void sourceEdited();
    void selectedIndicesRequested(const QList<int>& indices);
    // Only delayed requests emit this signal. Explicit apply() returns its result.
    void applyFinished(bool success, const QString& error);

private:
    struct ApplyRequest {
        DocumentRevision document;
        quint64 source = 0;
    };
    void textChanged();
    void applyScheduled();
    void updateHighlights(bool reveal = false);

    DocumentSession& session_;
    ZplSourceEditor& editor_;
    QTimer applyTimer_;
    std::optional<ApplyRequest> request_;
    QUuid mappedInstance_;
    quint64 mappedDocumentRevision_ = 0;
    QString mappedSource_;
    int mappedSourceRevision_ = -1;
    QList<SourceElementRange> ranges_;
    QList<int> selectedIndices_;
    bool selectionSyncEnabled_ = true;
    bool sourceSelectionActive_ = false;
    bool sourceCaretActive_ = false;
    bool syncingSelection_ = false;
    bool syncingDocument_ = false;
};

} // namespace Designer
