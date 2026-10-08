#include "preset_catalog.hpp"
#include <QFileInfo>
#include <QSettings>

namespace Designer {
std::expected<QList<ZplPreset>, QString> readPresetCatalog(const QString& path) {
    const QFileInfo file(path);
    if (!file.isFile() || file.size() > 16 * 1024 * 1024)
        return std::unexpected(QStringLiteral("INI не найден или превышает 16 МБ."));
    QSettings settings(path, QSettings::IniFormat);
    const int count = settings.beginReadArray(QStringLiteral("Presets"));
    if (count < 1 || count > 1000) return std::unexpected(QStringLiteral("В INI нет массива Presets (или более 1000 шаблонов)."));
    QList<ZplPreset> presets;
    presets.reserve(count);
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        presets.append({settings.value(QStringLiteral("name"), QStringLiteral("Шаблон %1").arg(index + 1)).toString(),
                        settings.value(QStringLiteral("template")).toString()});
    }
    settings.endArray();
    if (settings.status() != QSettings::NoError)
        return std::unexpected(QStringLiteral("Не удалось прочитать INI."));
    return presets;
}
}
