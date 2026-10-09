#pragma once

#include "design_document.hpp"

#include <QByteArray>
#include <QRectF>
#include <optional>

class QPrinter;

namespace Designer {
enum class LabelPrintMode { Image = 0, Zpl = 1, Png = 2 };
enum class PrintConnection { InstalledPrinter, Tcp };

// Missing values leave the device/source setting unchanged. These settings are
// only sent on a ZPL transport; driver raster and direct PNG ignore them.
struct PrinterParameters {
    std::optional<int> darkness;
    std::optional<int> speed;
    QString mediaTracking;
    QString printMethod;
    QString printMode;
    std::optional<int> tearOff;
    std::optional<int> labelTop;
    std::optional<int> labelShift;
    std::optional<int> maximumLength;
    [[nodiscard]] bool isEmpty() const;
    bool operator==(const PrinterParameters&) const = default;
};

struct PrintDestination {
    PrintConnection connection = PrintConnection::InstalledPrinter;
    QString printerName;
    QString host;
    quint16 port = 9100;
    int timeoutMs = 10000;
    bool usePrinterPaper = true;
};

struct PreparedLabelPrint {
    LabelPrintMode mode = LabelPrintMode::Image;
    QByteArray zpl;
    QImage image;
    QSize pageDots;
    int dpi = 203;
    int repeats = 1;
    qint64 labelsPerRepeat = 1;
    PrinterParameters printerParameters;
};

struct PrintSubmission {
    qint64 bytesAccepted = 0;
    quint32 spoolJobId = 0;
    QString message;
};

// Preparation has no printer/network side effects. Repeats never rewrite ^PQ.
[[nodiscard]] std::expected<PreparedLabelPrint, QString> prepareLabelPrint(
    const DesignDocument& document, LabelPrintMode mode, int repeats = 1);
[[nodiscard]] QString validatePrinterParameters(const PrinterParameters& parameters);
// Changes only the immutable print snapshot. Zero DPI keeps the document DPI.
[[nodiscard]] std::expected<PreparedLabelPrint, QString> configureLabelPrint(
    PreparedLabelPrint job, int dpi, const PrinterParameters& parameters);
[[nodiscard]] QSizeF printPageMillimeters(const PreparedLabelPrint& job);
[[nodiscard]] QRectF printImageTargetRect(const PreparedLabelPrint& job, int deviceDpiX, int deviceDpiY);
// Image jobs become a single ZPL image field; Png jobs are complete PNG files.
// PNG DPI metadata follows the configured job DPI without rescaling its pixels.
[[nodiscard]] std::expected<QByteArray, QString> printWireBytes(const PreparedLabelPrint& job);
// Configure and paint an existing QPrinter. Tests can safely inject PdfFormat.
[[nodiscard]] std::expected<PrintSubmission, QString> paintLabelPrint(
    const PreparedLabelPrint& job, QPrinter& printer, bool usePrinterPaper = false);
// Synchronous service; the dialog always calls this on its worker thread.
[[nodiscard]] std::expected<PrintSubmission, QString> submitLabelPrint(
    const PreparedLabelPrint& job, const PrintDestination& destination);
} // namespace Designer
