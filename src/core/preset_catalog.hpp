#pragma once
#include <expected>
#include <QList>
#include <QString>

namespace Designer {
struct ZplPreset { QString name; QString source; };
// Read-only adapter for the marker plugin's QSettings INI array format.
std::expected<QList<ZplPreset>, QString> readPresetCatalog(const QString& path);
}
