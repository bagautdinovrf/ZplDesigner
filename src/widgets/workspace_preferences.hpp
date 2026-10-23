#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace Designer {

enum class EditorLayout { Single, CodeDesign, CodePreview };

// Application-local UI state, independent of label documents and their history.
struct WorkspacePreferences {
    QByteArray windowGeometry;
    bool focus = false;
    bool dark = false;
    bool balanceLayers = true;
    bool balanceProperties = true;
    bool focusLayers = false;
    bool focusProperties = false;
    QList<int> balanceSizes{185, 950, 258};
    QList<int> focusSizes{185, 950, 258};
    EditorLayout editorLayout = EditorLayout::Single;
    bool codeOnLeft = true;
    bool selectionSync = true;
    QList<int> editorSizes{500, 500};
    int canvasZoomPercent = 0; // 0 keeps the label fitted to the canvas.
    bool geometrySizes = true;       // Selection dimension lines on the canvas.
    bool geometryCoordinates = true; // Selection ZPL origin coordinates on the canvas.
    bool rulersDocked = true;        // Rulers at the canvas edges instead of around the label.
    bool scaleLabelContent = true;   // Size and DPI changes scale elements and guides.
    QStringList hiddenToolbarCommands;
};

[[nodiscard]] QString workspacePreferencesPath();
[[nodiscard]] WorkspacePreferences loadWorkspacePreferences();
[[nodiscard]] bool saveWorkspacePreferences(const WorkspacePreferences& preferences);

} // namespace Designer
