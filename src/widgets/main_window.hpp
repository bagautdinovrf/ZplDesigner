#pragma once

#include "design_document.hpp"
#include "document_operations.hpp"
#include "designer_theme.hpp"
#include "workspace_preferences.hpp"
#include "zpl_source_map.hpp"
#include <QMainWindow>

class QCheckBox;
class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTabWidget;
class QTimer;
class QToolButton;
class QToolBar;
class QUndoStack;

namespace Designer {
class LabelCanvas;
class DocumentSession;
class ElementSelection;
class SourceEditorAdapter;
class LayersList;
class SourceDiagnosticsList;
class PreviewWidget;
class PreviewCoordinator;
class PropertiesPanel;
class DotSpinBox;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    bool openFile(const QString& path);
    bool saveFile(const QString& path);
    const DesignDocument& document() const;
    void setDocument(const DesignDocument& document);
    void addElement(ElementType type, QPoint position = {40, 40});
    bool importImage(const QString& path, QPoint position = {40, 40});
    bool applySource();
    bool openPreset(const QString& path, int index);
    void refreshPreview();
    void showPreview();
    bool smokeTest(const QString& directory);
    void showRecoveryDialog(bool startup = false);
protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
private:
    void buildUi();
    void buildMenus();
    void buildCommandPanels(QMenu* viewMenu);
    void setEditorLayout(EditorLayout layout);
    void updateEditorPresentation();
    void setWorkspaceMode(bool focus);
    void applyWorkspaceLayout();
    void rememberWorkspaceSizes();
    void persistWorkspace();
    void restoreCanvasZoom();
    void refreshFocusSummary();
    void positionWorkspaceOverlays();
    void positionCanvasDimensionsFooter();
    bool eventFilter(QObject* watched, QEvent* event) override;
    void restoreDocument(const DesignDocument& document);
    void refreshProperties();
    void refreshLayers();
    void executeOperation(const EditOperation& operation, const QString& title, int selection = -2);
    void copyElements(bool cut = false);
    void pasteElements();
    void groupSelection(bool ungroup = false);
    void editImage();
    void editTable();
    void editQrPayload();
    void editBarcodeOptions();
    void editAdvancedText();
    void editLabelSettings();
    void refreshPropertyUnits();
    void renameLayer();
    void setLayerFlag(LayerFlag flag, bool value);
    bool selectionLocked() const;
    void exportZpl();
    void refreshCleanPreview();
    void presentPreview();
    void refreshSourceDiagnostics();
    void resizeElement(int index, QSize size);
    [[nodiscard]] QSize elementSize(int index) const;
    void selectElement(int index);
    void syncSelection();
    void syncSourceSelection(bool allowCaret = false);
    void syncDesignSelection(bool reveal = false);
    void showGs1Builder();
    bool showPlaceholderValues();
    void exportResolvedZpl();
    void showPrintDialog();
    void showLabelSetup();
    void showCanvasLayout();
    void refreshCanvasLayout();
    void duplicateElement();
    void deleteElement();
    void moveLayer(int direction);
    void chooseImage(QPoint position = {40, 40});
    void newDocument();
    void chooseOpen();
    void choosePreset();
    bool save(bool saveAs = false);
    bool maybeSave();
    void exportPng();
    void updateTitle();
    void updateHistoryActions();
    void setNotice(const QString& text);

    DocumentSession* session_ = nullptr;
    PropertiesPanel* propertiesPanel_ = nullptr;
    ElementSelection* selection_ = nullptr;
    PreviewWidget* previewWidget_ = nullptr;
    PreviewCoordinator* previews_ = nullptr;
    int selected_ = -1;
    bool updating_ = false;
    QUndoStack* undo_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* previewAction_ = nullptr;
    SourceEditorAdapter* sourceAdapter_ = nullptr;
    LabelCanvas* canvas_ = nullptr;
    QTabWidget* editorTabs_ = nullptr;
    QSplitter* editorSplitter_ = nullptr;
    QWidget* sourcePage_ = nullptr;
    QWidget* sourceSlot_ = nullptr;
    QComboBox* editorLayout_ = nullptr;
    QAction* swapEditorSides_ = nullptr;
    QAction* syncEditorSelection_ = nullptr;
    QLabel* sourceStatus_ = nullptr;
    bool applyingEditorLayout_ = false;
    QTabWidget* inspectorTabs_ = nullptr;
    QSplitter* workspaceSplitter_ = nullptr;
    QWidget* layersPanel_ = nullptr;
    QWidget* diagnosticsPanel_ = nullptr;
    QWidget* focusBar_ = nullptr;
    QWidget* workspaceCenter_ = nullptr;
    QWidget* inspectorSlot_ = nullptr;
    QWidget* inspectorCard_ = nullptr;
    QLabel* documentName_ = nullptr;
    QLabel* layerCount_ = nullptr;
    QLabel* canvasSelection_ = nullptr;
    QWidget* canvasZoomControls_ = nullptr;
    QLabel* canvasZoomTitle_ = nullptr;
    QLabel* canvasDimensionsFooter_ = nullptr;
    QToolButton* fileButton_ = nullptr;
    QToolBar* documentCommandPanel_ = nullptr;
    QToolBar* editCommandPanel_ = nullptr;
    QLabel* focusSummary_ = nullptr;
    QAction* balanceAction_ = nullptr;
    QAction* focusAction_ = nullptr;
    QAction* propertiesAction_ = nullptr;
    QAction* layersAction_ = nullptr;
    QAction* diagnosticsAction_ = nullptr;
    QAction* darkThemeAction_ = nullptr;
    WorkspacePreferences workspacePreferences_;
    bool applyingWorkspace_ = false;
    LayersList* layers_ = nullptr;
    QPlainTextEdit* code_ = nullptr;
    QPlainTextEdit* diagnostics_ = nullptr;
    SourceDiagnosticsList* sourceDiagnostics_ = nullptr;
    QLabel* dimensions_ = nullptr;
    QLabel* notice_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    DotSpinBox* labelWidth_ = nullptr;
    DotSpinBox* labelHeight_ = nullptr;
    QComboBox* dpi_ = nullptr;
    QCheckBox* layerLocked_ = nullptr;
    QCheckBox* layerVisible_ = nullptr;
    QCheckBox* layerPrintable_ = nullptr;
    QCheckBox* grid_ = nullptr;
    QCheckBox* snap_ = nullptr;
    bool alignToLabel_ = false;
};
} // namespace Designer
