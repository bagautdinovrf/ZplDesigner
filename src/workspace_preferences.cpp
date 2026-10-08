#include "workspace_preferences.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
QList<int> normalizedSizes(const QList<int>& sizes, const QList<int>& defaults = {185, 950, 258})
{
    if (sizes.size() != defaults.size() || std::ranges::any_of(sizes, [](int size) { return size < 50 || size > 10000; }))
        return defaults;
    return sizes;
}

QList<int> readSizes(const QSettings& settings, const QString& key, const QList<int>& defaults = {185, 950, 258})
{
    const auto values = settings.value(key).toStringList();
    if (values.size() != defaults.size()) return defaults;
    QList<int> sizes;
    sizes.reserve(defaults.size());
    for (const auto& value : values) {
        bool valid = false;
        const int size = value.toInt(&valid);
        if (!valid) return defaults;
        sizes.append(size);
    }
    if (sizes == QList<int>{210, 850, 330}) return defaults;
    return normalizedSizes(sizes, defaults);
}

QStringList encodedSizes(const QList<int>& sizes, const QList<int>& defaults = {185, 950, 258})
{
    QStringList values;
    values.reserve(defaults.size());
    for (int size : normalizedSizes(sizes, defaults)) values.append(QString::number(size));
    return values;
}

EditorLayout normalizedEditorLayout(int layout)
{
    switch (layout) {
    case static_cast<int>(EditorLayout::CodeDesign): return EditorLayout::CodeDesign;
    case static_cast<int>(EditorLayout::CodePreview): return EditorLayout::CodePreview;
    default: return EditorLayout::Single;
    }
}

int normalizedCanvasZoomPercent(int zoomPercent)
{
    return zoomPercent >= 10 && zoomPercent <= 800 ? zoomPercent : 0;
}

int readCanvasZoomPercent(const QSettings& settings)
{
    bool valid = false;
    const int zoomPercent = settings.value(u"canvas/zoomPercent"_s, 0).toString().toInt(&valid);
    return valid ? normalizedCanvasZoomPercent(zoomPercent) : 0;
}

bool readCodeOnLeft(const QSettings& settings)
{
    const auto value = settings.value(u"editor/codeOnLeft"_s, true).toString().toLower();
    return value != u"false" && value != u"0";
}
} // namespace

QString workspacePreferencesPath()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString{} : QDir(directory).filePath(u"workspace.ini"_s);
}

WorkspacePreferences loadWorkspacePreferences()
{
    WorkspacePreferences preferences;
    const auto path = workspacePreferencesPath();
    if (path.isEmpty()) return preferences;
    const QSettings settings(path, QSettings::IniFormat);
    preferences.windowGeometry = settings.value(u"window/geometry"_s).toByteArray();
    preferences.focus = settings.value(u"focus"_s, preferences.focus).toBool();
    preferences.dark = settings.value(u"dark"_s, preferences.dark).toBool();
    preferences.balanceLayers = settings.value(u"balance/layers"_s, preferences.balanceLayers).toBool();
    preferences.balanceProperties = settings.value(u"balance/properties"_s, preferences.balanceProperties).toBool();
    preferences.focusLayers = settings.value(u"focus/layers"_s, preferences.focusLayers).toBool();
    preferences.focusProperties = settings.value(u"focus/properties"_s, preferences.focusProperties).toBool();
    preferences.balanceSizes = readSizes(settings, u"balance/sizes"_s);
    preferences.focusSizes = readSizes(settings, u"focus/sizes"_s);
    preferences.editorLayout = normalizedEditorLayout(settings.value(u"editor/layout"_s, 0).toString().toInt());
    preferences.codeOnLeft = readCodeOnLeft(settings);
    preferences.selectionSync = settings.value(u"editor/selectionSync"_s, true).toBool();
    preferences.editorSizes = readSizes(settings, u"editor/sizes"_s, {500, 500});
    preferences.canvasZoomPercent = readCanvasZoomPercent(settings);
    preferences.hiddenToolbarCommands = settings.value(u"toolbar/hiddenCommands"_s).toStringList();
    // Version 1 used the larger platform-style panels. Apply the concept's
    // initial widths once; later operator resizing remains persistent.
    if (settings.value(u"metricsVersion"_s, 0).toInt() < 2) {
        preferences.balanceSizes = {185, 950, 258};
        preferences.focusSizes = {185, 950, 258};
    }
    return settings.status() == QSettings::NoError ? preferences : WorkspacePreferences{};
}

bool saveWorkspacePreferences(const WorkspacePreferences& preferences)
{
    const auto path = workspacePreferencesPath();
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(u"window/geometry"_s, preferences.windowGeometry);
    settings.setValue(u"metricsVersion"_s, 2);
    settings.setValue(u"focus"_s, preferences.focus);
    settings.setValue(u"dark"_s, preferences.dark);
    settings.setValue(u"balance/layers"_s, preferences.balanceLayers);
    settings.setValue(u"balance/properties"_s, preferences.balanceProperties);
    settings.setValue(u"focus/layers"_s, preferences.focusLayers);
    settings.setValue(u"focus/properties"_s, preferences.focusProperties);
    settings.setValue(u"balance/sizes"_s, encodedSizes(preferences.balanceSizes));
    settings.setValue(u"focus/sizes"_s, encodedSizes(preferences.focusSizes));
    settings.setValue(u"editor/layout"_s, static_cast<int>(normalizedEditorLayout(static_cast<int>(preferences.editorLayout))));
    settings.setValue(u"editor/codeOnLeft"_s, preferences.codeOnLeft);
    settings.setValue(u"editor/selectionSync"_s, preferences.selectionSync);
    settings.setValue(u"editor/sizes"_s, encodedSizes(preferences.editorSizes, {500, 500}));
    settings.setValue(u"canvas/zoomPercent"_s, normalizedCanvasZoomPercent(preferences.canvasZoomPercent));
    settings.setValue(u"toolbar/hiddenCommands"_s, preferences.hiddenToolbarCommands);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace Designer
