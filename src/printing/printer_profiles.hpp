#pragma once

#include "label_printing.hpp"

namespace Designer {
struct PrinterProfile {
    QString name;
    PrintDestination destination;
    LabelPrintMode mode = LabelPrintMode::Image;
    int dpi = 0; // zero means use the document resolution
    PrinterParameters parameters;
};

[[nodiscard]] QString printerProfilesPath();
[[nodiscard]] QString validatePrinterProfile(const PrinterProfile& profile);
// Empty path uses the application-local catalogue; an explicit path supports
// portable import/export and isolated tests. Corrupt files are never overwritten
// implicitly: the UI disables catalogue writes until the problem is resolved.
[[nodiscard]] std::expected<QList<PrinterProfile>, QString> loadPrinterProfiles(const QString& path = {});
[[nodiscard]] std::expected<void, QString> savePrinterProfiles(
    const QList<PrinterProfile>& profiles, const QString& path = {});
} // namespace Designer
