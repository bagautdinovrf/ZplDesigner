#pragma once

#include <expected>
#include <QImage>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QUuid>

namespace Designer {
enum class ElementType { Text, Box, Line, Ellipse, Circle, Code128, QrCode, DataMatrix, Image,
                         Table, Ean13, Ean8, UpcA, UpcE, Code39, Pdf417, Codabar,
                         Code93, Interleaved2of5, Industrial2of5, Gs1DataBar, MicroPdf417, Aztec, MaxiCode, Diagonal, Eac };
enum class EacStyle { BlackOnWhite, WhiteOnBlack, BlackTransparent, WhiteTransparent };
struct Element {
    QUuid id = QUuid::createUuid();
    QUuid groupId;
    ElementType type = ElementType::Text;
    QString name;
    bool locked = false;
    bool editorVisible = true;
    bool printable = true;
    int x = 40;
    int y = 40;
    int width = 240;
    int height = 80;
    int thickness = 2;
    int fontHeight = 32;
    int fontWidth = 0;
    int blockWidth = 0;
    int blockMaxLines = 1;
    int blockLineSpacing = 0;
    int blockHangingIndent = 0;
    QChar blockJustification = u'L';
    bool baselineOrigin = false;
    int originJustification = -1; // -1 inherits ^FW; 0 left, 1 right, 2 automatic
    bool useDefaultOrientation = false;
    QChar textDirection = u'H';
    int characterSpacing = 0;
    bool graphicWhite = false;
    EacStyle eacStyle = EacStyle::BlackOnWhite;
    QChar diagonalDirection = u'R';
    int moduleWidth = 3;
    double barcodeRatio = 3.0;
    int threshold = 128;
    bool dither = false;
    bool reverse = false;
    bool humanReadable = true;
    bool interpretationAbove = false;
    bool code128CheckDigit = false;
    QChar code128Mode = u'A';
    QChar qrErrorCorrection = u'L';
    int pdf417Columns = 0;
    int pdf417Security = 2;
    int pdf417RowHeight = 6;
    int pdf417Rows = 0;
    bool pdf417Truncated = false;
    int dataMatrixColumns = 0;
    int dataMatrixRows = 0;
    bool dataMatrixRectangular = false;
    bool barcodeCheckDigit = false;
    int dataBarType = 1;
    int dataBarSegments = 0;
    int dataBarSeparator = 1;
    int microPdf417Mode = 0;
    int aztecSize = 23;
    bool aztecReaderInit = false;
    int maxiCodeMode = 4;
    int maxiCodePosition = 1;
    int maxiCodeTotal = 1;
    QList<int> tableColumnWidths{120, 120};
    QList<int> tableRowHeights{48, 48};
    QStringList tableCells{QString{}, QString{}, QString{}, QString{}};
    bool tableBorders = true;
    int tablePadding = 4;
    bool gs1 = false;
    QChar orientation = u'N';
    QString text = QStringLiteral("Text");
    // Escaped braces in a concrete field are printable data, not a template.
    bool literalText = false;
    QImage image;
    bool imageKeepAspectRatio = true;
    // Imported field spelling and its effective persistent printer state.
    // The codec keeps unrelated comments/tokens during visual edits; isolated
    // elementZpl() always serializes the editable values instead of this source.
    QString sourcePrefix;
    QString sourceZpl;
    QString sourceSignature;
    QString sourceState;
};
struct DesignDocument {
    int width = 812;
    int height = 406;
    int dpi = 203;
    QChar defaultOrientation = u'N';
    int defaultJustification = 0;
    bool printMirror = false;
    bool printInverted = false;
    bool labelReverse = false;
    QList<Element> elements;
    bool visualEditable = true;
    QString originalZpl;
    QString sourcePreamble;
    QString sourceEpilogue;
    int sourceWidth = 0;
    int sourceHeight = 0;
    QString sourceSettingsSignature;
    QList<QUuid> sourceOrder;
    QStringList importNotes;
    // Session-only preview/export inputs. Saving a ZPL template keeps tokens.
    QMap<QString, QString> previewValues;
};
struct Preview {
    QImage image;
    QList<QRect> bounds;
    QStringList diagnostics;
    // Editor-only invalid fields, indexed like the original document elements.
    // Their fallback bounds are selectable, but their codes are not rendered.
    QMap<int, QString> elementErrors;
};
QString elementTypeName(ElementType type);
bool isBarcodeType(ElementType type);
bool isLinearBarcodeType(ElementType type);
// Label transforms are involutions. Points use geometric boundaries (width-x),
// rectangles preserve their exact dot extent, and deltas have no translation.
QPoint mapDocumentPoint(const DesignDocument& document, QPoint point);
QPoint mapDocumentDelta(const DesignDocument& document, QPoint delta);
QRect mapDocumentRect(const DesignDocument& document, QRect rect);
void resizeTableGrid(Element& element, int rows, int columns);
void setTableDimensions(Element& element, int width, int height);
QList<Element> tableElements(const Element& table);
Element makeElement(ElementType type, QPoint position = {40, 40});
// Sizes are visible bounds in printer dots, after orientation. Text and barcode
// parameters are adjusted; their raster previews are never stretched.
Element resizedElement(const Element& element, QSize requestedSize, QSize currentRenderedSize);
DesignDocument sampleDocument();
QString elementZpl(const Element& element, bool preservePlaceholders = true,
                   QChar defaultOrientation = u'N', int defaultJustification = 0);
QString toZpl(const DesignDocument& document, bool preservePlaceholders = true);
std::expected<void, QString> validateForExport(const DesignDocument& document, bool preservePlaceholders = true);
// toZpl() after validateForExport(), serialized once for both.
std::expected<QString, QString> exportableZpl(const DesignDocument& document);
std::expected<DesignDocument, QString> fromZpl(const QString& source);
std::expected<Preview, QString> renderPreview(const DesignDocument& document);
// Canvas recovery only. Export and the clean printer preview use renderPreview.
std::expected<Preview, QString> renderEditorPreview(const DesignDocument& document);
std::expected<QImage, QString> loadImage(const QString& path);
QImage monochromeImage(const Element& element);
} // namespace Designer
