#pragma once

#include "design_document.hpp"
#include "canvas_layout.hpp"

#include <QObject>
#include <QDateTime>
#include <QTimer>
#include <QUndoStack>

#include <memory>
#include <optional>

class QLockFile;

namespace Designer {

enum class ElementAlignment { Left, HorizontalCenter, Right, Top, VerticalCenter, Bottom };
enum class AlignmentReference { Selection, Label };

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
    [[nodiscard]] QString recoveryFilePath() const;
    [[nodiscard]] std::expected<void, QString> flushRecovery();
    void setPendingSource(QString source);
    void clearPendingSource();
    [[nodiscard]] bool hasPendingSource() const;
    [[nodiscard]] QString pendingSource() const;

    void reset(DesignDocument document, QString path = {});
    void setLayoutSettings(CanvasLayoutSettings settings, const QString& title = {});
    void commit(DesignDocument document, const QString& title, bool preserveOriginal = false);
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
    void resetState(DesignDocument document, QString path, CanvasLayoutSettings settings, QString warning);
    void setFilePath(QString path);
    void scheduleRecovery();
    void markSaved();

    DesignDocument document_;
    QUndoStack undo_;
    QString filePath_;
    CanvasLayoutSettings layoutSettings_;
    QString layoutWarning_;
    std::optional<QString> pendingSource_;
    std::optional<QString> savedPendingSource_;
    QTimer recoveryDebounce_;
    QTimer recoveryDeadline_;
    QString recoveryPath_;
    std::unique_ptr<QLockFile> recoveryLock_;
};

} // namespace Designer
