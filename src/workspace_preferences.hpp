#pragma once

#include <QList>
#include <QString>

namespace Designer {

enum class EditorLayout { Single, CodeDesign, CodePreview };

// Application-local UI state, independent of label documents and their history.
struct WorkspacePreferences {
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
    QList<int> editorSizes{500, 500};
};

[[nodiscard]] QString workspacePreferencesPath();
[[nodiscard]] WorkspacePreferences loadWorkspacePreferences();
[[nodiscard]] bool saveWorkspacePreferences(const WorkspacePreferences& preferences);

} // namespace Designer
