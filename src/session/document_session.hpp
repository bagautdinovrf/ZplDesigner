#pragma once

#include "design_document.hpp"
#include "canvas_layout.hpp"
#include "document_revision.hpp"
#include "document_operations.hpp"

#include <QObject>
#include <QDateTime>
#include <QThreadPool>
#include <QTimer>
#include <QUndoStack>

#include <memory>
#include <optional>

class QLockFile;

namespace Designer {

enum class ElementAlignment { Left, HorizontalCenter, Right, Top, VerticalCenter, Bottom };
enum class AlignmentReference { Selection, Label };
enum class SourceStatus { Applied, Pending, Rejected };

struct RecoveryEntry {
    QString path;
    QString originalPath;
    QDateTime modifiedAt;
    QString error;
};

// Application state and persistence without a dependency on any window or view.
// Undo commands keep document snapshots; widgets observe the signals below.
class DocumentSession final : public QObject {
    Q_OBJECT

public:
    explicit DocumentSession(QObject* parent = nullptr);
    ~DocumentSession() override;

    [[nodiscard]] const DesignDocument& document() const;
    [[nodiscard]] QUndoStack* undoStack();
    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool modified() const;
    [[nodiscard]] const CanvasLayoutSettings& layoutSettings() const;
    [[nodiscard]] QString layoutWarning() const;
    // Per-document editor preferences live in the user's local application data,
    // keyed by the absolute ZPL path. This does not create files or directories.
    [[nodiscard]] static QString layoutStoragePath(const QString& zplPath);
    [[nodiscard]] static QString recoveryStorageDirectory();
    [[nodiscard]] static QList<RecoveryEntry> availableRecoveries();
    [[nodiscard]] static std::expected<void, QString> discardRecovery(const QString& path);
    [[nodiscard]] std::expected<void, QString> recover(const QString& path);
    // Explicit close/discard removes this window's snapshot only. Destruction
    // alone retains unsaved recovery, so abnormal application shutdown is safe.
    void discardRecovery();
    // Reserved before its first background write completes.
    [[nodiscard]] QString recoveryFilePath() const;
    // Writes synchronously after any background write; edits schedule those.
    [[nodiscard]] std::expected<void, QString> flushRecovery();
    void setPendingSource(QString source);
    void clearPendingSource();
    [[nodiscard]] bool hasPendingSource() const;
    [[nodiscard]] QString pendingSource() const;
    [[nodiscard]] const DocumentRevision& revision() const { return revision_; }
    [[nodiscard]] quint64 sourceRevision() const { return sourceRevision_; }
    [[nodiscard]] SourceStatus sourceStatus() const;
    [[nodiscard]] QString sourceError() const { return sourceError_; }
    [[nodiscard]] bool canEditVisually() const;
    [[nodiscard]] std::expected<bool, QString> applySource();
    [[nodiscard]] std::expected<bool, QString> applySource(DocumentRevision expectedDocument,
                                                        quint64 expectedSource);

    void reset(DesignDocument document, QString path = {});
    // Edits passing the same non-zero group merge into one undo step while it
    // stays on top of the history, e.g. everything changed in one opening of
    // the canvas quick panel. A merge back to the original settings removes it.
    [[nodiscard]] quint64 newLayoutEditGroup();
    void setLayoutSettings(CanvasLayoutSettings settings, const QString& title = {}, quint64 group = 0);
    void commit(DesignDocument document, const QString& title, bool preserveOriginal = false);
    [[nodiscard]] OperationResult execute(const EditOperation& operation, const QString& title);
    // Each operation is atomic in history. Bounds are indexed like document.elements
    // and describe the rendered footprint, including barcode/text offsets.
    bool moveElements(const QList<int>& indices, QPoint delta);
    bool deleteElements(const QList<int>& indices);
    [[nodiscard]] QList<int> duplicateElements(const QList<int>& indices, QPoint offset = {16, 16});
    [[nodiscard]] QList<int> pasteElements(const QList<Element>& elements, QPoint offset = {16, 16});
    bool groupElements(const QList<int>& indices);
    bool ungroupElements(const QList<int>& indices);
    bool alignElements(const QList<int>& indices, ElementAlignment alignment,
                       AlignmentReference reference, const QList<QRect>& renderedBounds = {});
    bool distributeElements(const QList<int>& indices, Qt::Orientation orientation,
                            const QList<QRect>& renderedBounds = {});
    [[nodiscard]] std::expected<void, QString> open(const QString& path);
    [[nodiscard]] std::expected<void, QString> save(const QString& path);
    [[nodiscard]] std::expected<void, QString> exportZpl(const QString& path) const;
    void setPreviewValues(QMap<QString, QString> values);
    // Export a filled copy without changing the open template path or history.
    [[nodiscard]] std::expected<void, QString> saveResolved(const QString& path) const;

signals:
    void documentChanged();
    void modifiedChanged(bool modified);
    void filePathChanged();
    void layoutSettingsChanged();
    void pendingSourceChanged();
    void recoveryWarning(const QString& message);

private:
    friend class SessionCommand;
    friend class LayoutSettingsCommand;
    void restore(DesignDocument document);
    void restoreLayoutSettings(CanvasLayoutSettings settings);
    void resetState(DesignDocument document, QString path, CanvasLayoutSettings settings, QString warning,
                    std::optional<QString> pending = {}, bool recovered = false);
    void setFilePath(QString path);
    [[nodiscard]] OperationResult scaleLabel(const UpdateLabelSettings& request, const QString& title);
    void scheduleRecovery();
    void writeRecoveryInBackground();
    [[nodiscard]] std::expected<void, QString> reserveRecoveryFile();
    void markSaved();

    DesignDocument document_;
    QUndoStack undo_;
    QString filePath_;
    CanvasLayoutSettings layoutSettings_;
    QString layoutWarning_;
    std::optional<QString> pendingSource_;
    std::optional<QString> savedPendingSource_;
    DocumentRevision revision_{QUuid::createUuid(), 0, 0};
    quint64 sourceRevision_ = 0;
    quint64 layoutEditGroup_ = 0;
    // State before the first of consecutive scaled label edits, and the state
    // the latest of them produced. Any other change starts from the new state.
    struct LabelScaleBase {
        DesignDocument document;
        CanvasLayoutSettings layout;
        DocumentRevision result;
        CanvasLayoutSettings resultLayout;
    };
    std::optional<LabelScaleBase> labelScaleBase_;
    QString sourceError_;
    bool documentNotificationPending_ = false;
    bool layoutNotificationPending_ = false;
    QTimer recoveryDebounce_;
    QTimer recoveryDeadline_;
    QString recoveryPath_;
    std::unique_ptr<QLockFile> recoveryLock_;
    // Debounced recovery snapshots are encoded and written here.
    QThreadPool recoveryWriter_;
    bool recoveryWriting_ = false;
    bool recoveryRequeued_ = false;
};

} // namespace Designer
