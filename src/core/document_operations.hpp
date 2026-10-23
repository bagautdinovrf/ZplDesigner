#pragma once

#include "design_document.hpp"
#include "document_revision.hpp"

#include <optional>
#include <span>
#include <variant>

namespace Designer {

// Requests contain values in printer dots and stable element IDs. UI adapters
// decide which fields the user changed; application rules do not read widgets.
struct ElementProperties {
    std::optional<int> x, y;
    std::optional<QString> text;
    std::optional<int> fontHeight, fontWidth, thickness, moduleWidth, threshold;
    std::optional<QChar> orientation;
    std::optional<bool> humanReadable, reverse, dither, graphicWhite;
    std::optional<EacStyle> eacStyle;
    std::optional<QChar> diagonalDirection;
    std::optional<bool> imageKeepAspectRatio, gs1;
    std::optional<int> blockWidth, blockMaxLines, blockLineSpacing, blockHangingIndent;
    std::optional<QChar> blockJustification, code128Mode, codabarStart, codabarStop;
    std::optional<bool> interpretationAbove;
    std::optional<QChar> qrErrorCorrection;
    std::optional<int> pdf417RowHeight, pdf417Columns, pdf417Security;
};

struct SelectionPropertyValues {
    std::optional<int> moduleWidth, thickness, fontHeight, fontWidth;
    std::optional<int> blockWidth, blockMaxLines, blockLineSpacing, blockHangingIndent;
};

struct ChangeBarcodeType { QUuid id; ElementType type; };
struct UpdateText {
    QUuid id;
    QString text;
    // The GS1 builder updates mode and payload in the same history operation.
    std::optional<bool> gs1;
    // Preserve the existing GS1 builder's interpretation of imported literals.
    bool preserveLiteralText = false;
};
struct UpdateElementProperties { QUuid id; ElementProperties properties; };
struct UpdateSelectionProperties { QList<QUuid> ids; SelectionPropertyValues values; };
struct ResizeElement {
    QUuid id;
    QSize requestedSize;
    QSize currentSize;
    DocumentRevision geometryRevision;
    QString geometryError;
};
// Dialog adapters return only the edited category's values. The operation
// preserves identity, common layer state and original source metadata.
struct UpdateElementContent { QUuid id; Element values; };
struct UpdateLabelSettings {
    int width = 812;
    int height = 406;
    int dpi = 203;
    QChar defaultOrientation = u'N';
    int defaultJustification = 0;
    bool printMirror = false;
    bool printInverted = false;
    bool labelReverse = false;
};
struct AddElement { Element element; };
struct RenameElement { QUuid id; QString name; };
enum class LayerFlag { Locked, Visible, Printable };
struct SetElementFlags { QList<QUuid> ids; LayerFlag flag; bool value; };
struct ReorderElements { QList<QUuid> ids; int direction; };

using EditOperation = std::variant<ChangeBarcodeType, UpdateText, UpdateElementProperties,
    UpdateSelectionProperties, ResizeElement, UpdateElementContent, UpdateLabelSettings,
    AddElement, RenameElement, SetElementFlags, ReorderElements>;

// These families also drive the barcode selector, so it cannot offer an
// operation different from the one accepted by the application layer.
[[nodiscard]] std::span<const ElementType> availableBarcodeTypes(ElementType current);

// Success(true) changes the supplied value atomically; success(false) is a
// no-op. Failure leaves it untouched. These operations preserve source tokens
// and allow editable intermediate payloads that are not yet printable.
using OperationResult = std::expected<bool, QString>;
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const ChangeBarcodeType&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const UpdateText&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const UpdateElementProperties&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const UpdateSelectionProperties&);
// The session verifies geometryRevision before calling this value transform.
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const ResizeElement&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const UpdateElementContent&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const UpdateLabelSettings&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const AddElement&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const RenameElement&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const SetElementFlags&);
[[nodiscard]] OperationResult applyOperation(DesignDocument&, const ReorderElements&);

} // namespace Designer
