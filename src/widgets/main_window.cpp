#include "main_window.hpp"
#include "command_icons.hpp"
#include "label_canvas.hpp"
#include "canvas_quick_panel.hpp"
#include "designer_icons.hpp"
#include "guide_list_dialog.hpp"
#include "document_session.hpp"
#include "preset_catalog.hpp"
#include "preview_widget.hpp"
#include "preview_pages.hpp"
#include "preview_coordinator.hpp"
#include "document_operations.hpp"
#include "element_selection.hpp"
#include "source_editor_adapter.hpp"
#include "layers_list.hpp"
#include "diagnostics_list.hpp"
#include "element_presentation.hpp"
#include "eac_mark.hpp"
#include "properties_panel.hpp"
#include "workspace_widgets.hpp"
#include "zpl_source_editor.hpp"
#include "element_clipboard.hpp"
#include "selection_properties.hpp"
#include "image_edit_dialog.hpp"
#include "gs1_data.hpp"
#include "gs1_dialog.hpp"
#include "data_matrix_values.hpp"
#include "placeholder_values_dialog.hpp"
#include "print_dialog.hpp"
#include "value_placeholders.hpp"
#include "dot_spin_box.hpp"
#include "table_dialog.hpp"
#include "qr_payload_dialog.hpp"
#include "barcode_options_dialog.hpp"
#include "advanced_text_dialog.hpp"
#include "label_settings_dialog.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QInputDialog>
#include <QIconEngine>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegion>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSet>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTabBar>
#include <QTextBlock>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>
#include <array>
#include <span>
#include <tuple>

namespace Designer {
namespace {
UpdateLabelSettings labelSettings(const DesignDocument& document) {
    return {document.width, document.height, document.dpi, document.defaultOrientation,
        document.defaultJustification, document.printMirror, document.printInverted, document.labelReverse};
}

QString applicationTitle() {
    const auto version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QStringLiteral("QtZpl Designer")
        : QStringLiteral("QtZpl Designer %1").arg(version);
}

// The HTML concept uses painted flat surfaces rather than platform chrome.
class InspectorTabs final : public QTabWidget {
public:
    using QTabWidget::QTabWidget;

    void setRoundedCorners(bool rounded) {
        if (rounded_ == rounded) return;
        rounded_ = rounded;
        updateCornerMask();
    }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QTabWidget::resizeEvent(event);
        if (rounded_) updateCornerMask();
    }
private:
    void updateCornerMask() {
        if (!rounded_) { clearMask(); return; }
        // Clip opaque tab/page backgrounds inside the floating card's border.
        // The one-pixel inset leaves its antialiased outer radius visible.
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 6, 6);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    bool rounded_ = false;
};

class SelectionSyncIconEngine final : public QIconEngine {
public:
    QIconEngine* clone() const override { return new SelectionSyncIconEngine; }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.center());
        const auto scale = std::min(rect.width(), rect.height()) / 18.0;
        painter->scale(scale, scale);
        painter->rotate(-35);
        const auto role = state == QIcon::On || mode == QIcon::Selected ? QPalette::Link : QPalette::Text;
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        painter->setPen(QPen(qApp->palette().color(group, role), 1.6, Qt::SolidLine, Qt::RoundCap));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(-8, -3, 10, 6), 3, 3);
        painter->drawRoundedRect(QRectF(-2, -3, 10, 6), 3, 3);
        painter->restore();
    }
};

class WorkspaceStatus final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setFont(font());
        p.setPen(designerColor(text().startsWith(QStringLiteral("✓")) ? DesignerColor::Success : DesignerColor::Muted));
        p.drawText(rect().adjusted(11, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(text(), Qt::ElideRight, std::max(0, width() - 15)));
    }
};

class ElidedCaption final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), alignment(),
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

class SymbolIconEngine final : public QIconEngine {
public:
    explicit SymbolIconEngine(QString symbol) : symbol_(std::move(symbol)) {}
    QIconEngine* clone() const override { return new SymbolIconEngine(symbol_); }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        p->save(); auto font = QApplication::font(); font.setPixelSize(15); p->setFont(font);
        p->setPen(designerColor(mode == QIcon::Selected ? DesignerColor::Accent : DesignerColor::Muted));
        p->drawText(rect, Qt::AlignCenter, symbol_); p->restore();
    }
private:
    QString symbol_;
};









class PaletteDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text.clear();
        option->features.setFlag(QStyleOptionViewItem::HasDisplay, false);
        option->displayAlignment = Qt::AlignCenter;
        option->decorationAlignment = Qt::AlignCenter;
        option->decorationPosition = QStyleOptionViewItem::Top;
    }
};

class ElementPalette final : public QListWidget {
public:
    explicit ElementPalette(QWidget* parent) : QListWidget(parent) {
        setObjectName(QStringLiteral("elementPalette"));
        setFrameShape(QFrame::NoFrame);
        setIconSize({20, 20});
        setSpacing(4);
        setFixedWidth(43);
        setContentsMargins(3, 9, 3, 4);
        setItemDelegate(new PaletteDelegate(this));
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setAccessibleName(QStringLiteral("Добавление элементов"));
        setDragEnabled(true);
        viewport()->setBackgroundRole(QPalette::Window);
        auto* pointer = new QListWidgetItem(QIcon(new SymbolIconEngine(QStringLiteral("↖"))), QStringLiteral("Выбор объектов"), this);
        pointer->setData(Qt::UserRole, -1); pointer->setSizeHint({31, 32}); pointer->setToolTip(QStringLiteral("Выбор и перемещение объектов"));
        for (auto type : {ElementType::Text, ElementType::Box, ElementType::Line, ElementType::Circle,
                         ElementType::Ellipse, ElementType::Image, ElementType::Code128,
                         ElementType::DataMatrix, ElementType::Table, ElementType::Diagonal, ElementType::Eac}) {
            const auto name = type == ElementType::Code128 ? QStringLiteral("1D-код")
                : type == ElementType::DataMatrix ? QStringLiteral("2D-код") : elementTypeName(type);
            auto* item = new QListWidgetItem(elementIcon(type), name, this);
            item->setData(Qt::UserRole, static_cast<int>(type));
            item->setSizeHint({31, 32});
            item->setToolTip(type == ElementType::Eac
                ? QStringLiteral("Единый знак обращения EAC\nПеретащите на этикетку или нажмите дважды. Цвет и фон выбираются в свойствах.")
                : isBarcodeType(type)
                ? name + QStringLiteral("\nПеретащите на этикетку или нажмите дважды. Тип кода выбирается в свойствах.")
                : name + QStringLiteral("\nПеретащите на этикетку или нажмите дважды"));
        }
        setCurrentRow(0);
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        QListWidget::paintEvent(event);
        if (viewport()->height() < 590) return;
        QPainter p(viewport()); auto font = p.font(); font.setPixelSize(9); p.setFont(font);
        p.setPen(designerColor(DesignerColor::Muted)); p.translate(width() / 2.0, viewport()->height() - 88); p.rotate(90);
        p.drawText(0, 0, QStringLiteral("ИНСТРУМЕНТЫ"));
    }
    void startDrag(Qt::DropActions) override {
        if (!currentItem() || currentItem()->data(Qt::UserRole).toInt() < 0) return;
        auto* drag = new QDrag(this);
        auto* mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-qtzpl-element"), QByteArray::number(currentItem()->data(Qt::UserRole).toInt()));
        drag->setMimeData(mime);
        drag->setPixmap(currentItem()->icon().pixmap(32, 32));
        drag->exec(Qt::CopyAction);
    }
};

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("designerWindow"));
    resize(1440, 900);
    setMinimumSize(1050, 680);
    workspacePreferences_ = loadWorkspacePreferences();
    setDesignerTheme(workspacePreferences_.dark ? DesignerTheme::Dark : DesignerTheme::Light);
    session_ = new DocumentSession(this);
    selection_ = new ElementSelection(this);
    undo_ = session_->undoStack();
    previews_ = new PreviewCoordinator(this);
    connect(previews_, &PreviewCoordinator::previewsChanged, this, &MainWindow::presentPreview);
    buildUi();
    buildMenus();
    setEditorLayout(workspacePreferences_.editorLayout);
    applyWorkspaceLayout();
    connect(session_, &DocumentSession::documentChanged, this, [this] { restoreDocument(session_->document()); });
    connect(session_, &DocumentSession::layoutSettingsChanged, this, &MainWindow::refreshCanvasLayout);
    connect(session_, &DocumentSession::filePathChanged, this, [this] { updateTitle(); });
    connect(session_, &DocumentSession::modifiedChanged, this, [this] { updateTitle(); });
    connect(session_, &DocumentSession::recoveryWarning, this, &MainWindow::setNotice);
    connect(undo_, &QUndoStack::cleanChanged, this, [this] { updateTitle(); });
    setDocument(sampleDocument());
    selectElement(1);
    // Qt retains the normal rectangle and maximized state, and adjusts the
    // saved placement if its screen is no longer available.
    if (!workspacePreferences_.windowGeometry.isEmpty())
        restoreGeometry(workspacePreferences_.windowGeometry);
    // Initial layout/document fitting must not overwrite the saved choice.
    restoreCanvasZoom();
    connect(canvas_, &LabelCanvas::zoomChanged, this, [this](int percent) {
        const int choice = canvas_->isFitMode() ? 0 : percent;
        if (workspacePreferences_.canvasZoomPercent == choice) return;
        workspacePreferences_.canvasZoomPercent = choice;
        persistWorkspace();
    });
    QTimer::singleShot(0, this, &MainWindow::restoreCanvasZoom);
}

MainWindow::~MainWindow() {
    // Reparented overlay pages can change focus/selection while QWidget tears
    // down the window, after the MainWindow part has already been destroyed.
    if (workspaceCenter_) workspaceCenter_->removeEventFilter(this);
    if (canvas_) canvas_->viewport()->removeEventFilter(this);
    QObject::disconnect(qApp, nullptr, this, nullptr);
    // The session's stack is a value member, not a QObject child. It emits
    // history signals as it clears commands during session destruction.
    QObject::disconnect(undo_, nullptr, this, nullptr);
    for (auto* sender : findChildren<QObject*>()) QObject::disconnect(sender, nullptr, this, nullptr);
}

void MainWindow::buildUi() {
    auto* modes = new QActionGroup(this);
    modes->setExclusive(true);
    balanceAction_ = modes->addAction(QStringLiteral("Баланс"));
    balanceAction_->setObjectName(QStringLiteral("workspaceBalance"));
    balanceAction_->setCheckable(true);
    balanceAction_->setToolTip(QStringLiteral("Слои слева, свойства справа"));
    focusAction_ = modes->addAction(QStringLiteral("Фокус"));
    focusAction_->setObjectName(QStringLiteral("workspaceFocus"));
    focusAction_->setCheckable(true);
    focusAction_->setToolTip(QStringLiteral("Свободный холст; панели открываются по запросу (Ctrl+Shift+F)"));
    connect(balanceAction_, &QAction::triggered, this, [this] { setWorkspaceMode(false); });
    connect(focusAction_, &QAction::triggered, this, [this] { setWorkspaceMode(true); });
    auto* toggleFocus = new QAction(this);
    toggleFocus->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    addAction(toggleFocus);
    connect(toggleFocus, &QAction::triggered, this, [this] { setWorkspaceMode(!workspacePreferences_.focus); });
    propertiesAction_ = new QAction(QStringLiteral("Свойства"), this);
    propertiesAction_->setObjectName(QStringLiteral("workspaceProperties"));
    propertiesAction_->setCheckable(true);
    propertiesAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+P")));
    layersAction_ = new QAction(QStringLiteral("Слои"), this);
    layersAction_->setObjectName(QStringLiteral("workspaceLayers"));
    layersAction_->setCheckable(true);
    layersAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+L")));
    diagnosticsAction_ = new QAction(QStringLiteral("Проверка"), this);
    diagnosticsAction_->setObjectName(QStringLiteral("workspaceDiagnostics"));
    diagnosticsAction_->setCheckable(true);
    darkThemeAction_ = new QAction(QStringLiteral("Тёмная тема"), this);
    darkThemeAction_->setObjectName(QStringLiteral("darkTheme"));
    darkThemeAction_->setCheckable(true);
    darkThemeAction_->setChecked(designerTheme() == DesignerTheme::Dark);
    connect(propertiesAction_, &QAction::toggled, this, [this](bool visible) {
        rememberWorkspaceSizes();
        (workspacePreferences_.focus ? workspacePreferences_.focusProperties : workspacePreferences_.balanceProperties) = visible;
        applyWorkspaceLayout(); persistWorkspace();
    });
    connect(layersAction_, &QAction::toggled, this, [this](bool visible) {
        rememberWorkspaceSizes();
        (workspacePreferences_.focus ? workspacePreferences_.focusLayers : workspacePreferences_.balanceLayers) = visible;
        applyWorkspaceLayout(); persistWorkspace();
    });
    connect(diagnosticsAction_, &QAction::toggled, this, [this](bool visible) { diagnosticsPanel_->setVisible(visible); });
    connect(darkThemeAction_, &QAction::toggled, this, [this](bool dark) {
        workspacePreferences_.dark = dark;
        setDesignerTheme(dark ? DesignerTheme::Dark : DesignerTheme::Light);
        persistWorkspace();
    });
    // Keep command actions shared by menus and the compact icon panels.
    auto* fresh = new QAction(QStringLiteral("Новая"), this); fresh->setObjectName(QStringLiteral("newDocumentAction"));
    fresh->setShortcut(QKeySequence::New); addAction(fresh);
    connect(fresh, &QAction::triggered, this, [this] { newDocument(); });
    auto* open = new QAction(QStringLiteral("Открыть"), this); open->setShortcut(QKeySequence::Open); addAction(open);
    connect(open, &QAction::triggered, this, [this] { chooseOpen(); });
    auto* saveAction = new QAction(QStringLiteral("Сохранить"), this); saveAction->setObjectName(QStringLiteral("saveAction"));
    saveAction->setShortcut(QKeySequence::Save); addAction(saveAction);
    connect(saveAction, &QAction::triggered, this, [this] { save(); });
    undoAction_ = new QAction(QStringLiteral("Отменить"), this); undoAction_->setObjectName(QStringLiteral("undoAction"));
    undoAction_->setShortcut(QKeySequence::Undo); undoAction_->setToolTip(QStringLiteral("Отменить · Ctrl+Z")); addAction(undoAction_);
    redoAction_ = new QAction(QStringLiteral("Повторить"), this); redoAction_->setObjectName(QStringLiteral("redoAction"));
    redoAction_->setShortcut(QKeySequence::Redo); redoAction_->setToolTip(QStringLiteral("Повторить · Ctrl+Y")); addAction(redoAction_);
    connect(undoAction_, &QAction::triggered, this, [this] {
        if (code_->hasFocus()) code_->undo();
        else if (!session_->hasPendingSource()) undo_->undo();
    });
    connect(redoAction_, &QAction::triggered, this, [this] {
        if (code_->hasFocus()) code_->redo();
        else if (!session_->hasPendingSource()) undo_->redo();
    });
    connect(undo_, &QUndoStack::canUndoChanged, this, &MainWindow::updateHistoryActions);
    connect(undo_, &QUndoStack::canRedoChanged, this, &MainWindow::updateHistoryActions);
    auto* valuesAction = new QAction(QStringLiteral("Значения плейсхолдеров…"), this); valuesAction->setObjectName(QStringLiteral("placeholderValuesAction"));
    connect(valuesAction, &QAction::triggered, this, [this] { showPlaceholderValues(); });
    previewAction_ = new QAction(QStringLiteral("Превью"), this); previewAction_->setObjectName(QStringLiteral("previewAction"));
    previewAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P"))); addAction(previewAction_);
    connect(previewAction_, &QAction::triggered, this, &MainWindow::showPreview);
    auto* print = new QAction(QStringLiteral("Печать…"), this); print->setObjectName(QStringLiteral("printAction"));
    print->setShortcut(QKeySequence::Print); addAction(print);
    connect(print, &QAction::triggered, this, &MainWindow::showPrintDialog);
    for (auto* action : {propertiesAction_, layersAction_, diagnosticsAction_, darkThemeAction_}) addAction(action);

    auto* central = new WorkspaceSurface(this);
    auto* outer = new QVBoxLayout(central); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    auto* title = new WorkspaceSurface(central, true); title->setObjectName(QStringLiteral("workspaceTitle")); title->setFixedHeight(39);
    auto* titleRow = new QHBoxLayout(title); titleRow->setContentsMargins(15, 0, 15, 0); titleRow->setSpacing(9);
    auto* badge = new ObjectBadge(QStringLiteral("ZPL"), title); badge->setProperty("outline", true); badge->setAlignment(Qt::AlignCenter); badge->setFixedSize(24, 22);
    auto badgeFont = badge->font(); badgeFont.setPixelSize(11); badgeFont.setBold(true); badge->setFont(badgeFont);
    badge->setForegroundRole(QPalette::Link); titleRow->addWidget(badge);
    auto* brand = heading(QStringLiteral("QtZpl Designer"), title); brand->setObjectName(QStringLiteral("applicationBrand"));
    titleRow->addWidget(brand);
    titleRow->addSpacing(15); // Together with the row's 9 px spacing: 24 px after Designer.
    documentCommandPanel_ = new QToolBar(title);
    documentCommandPanel_->setObjectName(QStringLiteral("documentCommandPanel"));
    titleRow->addWidget(documentCommandPanel_);
    titleRow->addSpacing(11);
    documentName_ = caption(QString(), title, 11); titleRow->addWidget(documentName_); titleRow->addStretch();
    auto addTool = [](QHBoxLayout* row, QAction* action, bool compact = false, bool flat = false) {
        auto* button = new QToolButton(row->parentWidget()); button->setDefaultAction(action);
        button->setProperty("compact", compact); button->setProperty("flat", flat); button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        row->addWidget(button); return button;
    };
    auto* modeGroup = new QWidget(title); auto* modeRow = new QHBoxLayout(modeGroup); modeRow->setContentsMargins(2, 2, 2, 2); modeRow->setSpacing(2);
    for (auto* action : {balanceAction_, focusAction_}) { auto* button = addTool(modeRow, action, true); button->setProperty("designerControl", "segment"); }
    titleRow->addWidget(modeGroup); addTool(titleRow, darkThemeAction_, true, true);
    outer->addWidget(title);

    auto* formatBar = new WorkspaceSurface(central); formatBar->setObjectName(QStringLiteral("workspaceToolbar")); formatBar->setFixedHeight(45);
    auto* commandRow = new QHBoxLayout(formatBar); commandRow->setContentsMargins(12, 4, 12, 4); commandRow->setSpacing(6);
    fileButton_ = new QToolButton(formatBar); fileButton_->setObjectName(QStringLiteral("fileMenuButton")); fileButton_->setText(QStringLiteral("Файл"));
    fileButton_->setProperty("flat", true); fileButton_->setPopupMode(QToolButton::InstantPopup); commandRow->addWidget(fileButton_);
    fileButton_->setMinimumWidth(60);
    auto* divider = new QFrame(formatBar); divider->setFrameShape(QFrame::VLine); divider->setFixedHeight(22); commandRow->addWidget(divider);
    auto* viewTabs = new QTabBar(formatBar); viewTabs->setObjectName(QStringLiteral("workspaceViewTabs")); viewTabs->setDrawBase(false);
    viewTabs->setExpanding(false); viewTabs->setFixedHeight(37);
    viewTabs->addTab(QStringLiteral("Дизайн")); viewTabs->addTab(QStringLiteral("Код ZPL")); viewTabs->addTab(QStringLiteral("Превью")); commandRow->addWidget(viewTabs);
    commandRow->addSpacing(12);
    editCommandPanel_ = new QToolBar(formatBar);
    editCommandPanel_->setObjectName(QStringLiteral("editCommandPanel"));
    commandRow->addWidget(editCommandPanel_, 1);
    dimensions_ = caption(QString(), this, 11);
    statusBar()->addPermanentWidget(dimensions_);
    addTool(commandRow, layersAction_, true);
    auto* labelSetup = new QPushButton(QStringLiteral("Этикетка"), formatBar); labelSetup->setObjectName(QStringLiteral("labelSetup")); labelSetup->setProperty("compact", true);
    commandRow->addWidget(labelSetup); connect(labelSetup, &QPushButton::clicked, this, [this] {
        propertiesAction_->setChecked(true); inspectorTabs_->setCurrentIndex(1);
    });
    addTool(commandRow, diagnosticsAction_, true); outer->addWidget(formatBar);
    auto* labelPage = new WorkspaceSurface(central, false, false, false);
    auto* labelLayout = new QVBoxLayout(labelPage); labelLayout->setContentsMargins(0, 0, 0, 0); labelLayout->setSpacing(0);
    auto* labelHeading = heading(QStringLiteral("Формат этикетки"), labelPage); labelHeading->setContentsMargins(13, 12, 13, 12); labelLayout->addWidget(labelHeading);
    auto* formatSection = new WorkspaceSurface(labelPage); auto* formatLayout = new QVBoxLayout(formatSection); formatLayout->setContentsMargins(13, 12, 13, 12); formatLayout->setSpacing(10);
    auto* formatGrid = new QGridLayout; formatGrid->setSpacing(10); formatLayout->addLayout(formatGrid);
    labelWidth_ = static_cast<DotSpinBox*>(dotSpin(QStringLiteral("labelWidth"), 32, 4096, labelPage));
    formatGrid->addWidget(propertyField(QStringLiteral("Ширина"), labelWidth_, formatSection), 0, 0);
    labelHeight_ = static_cast<DotSpinBox*>(dotSpin(QStringLiteral("labelHeight"), 32, 4096, labelPage));
    formatGrid->addWidget(propertyField(QStringLiteral("Высота"), labelHeight_, formatSection), 0, 1);
    // Units follow the «Объект» selector, which is not visible on this page,
    // so the fields always name their unit.
    for (auto* field : {labelWidth_, labelHeight_}) field->setProperty("hideUnitSuffix", false);
    dpi_ = new QComboBox(labelPage); dpi_->setObjectName(QStringLiteral("dpi"));
    for (int dpi : {203, 300, 600}) dpi_->addItem(QStringLiteral("%1 dpi").arg(dpi), dpi);
    formatGrid->addWidget(propertyField(QStringLiteral("Разрешение"), dpi_, formatSection), 1, 0, 1, 2);
    auto* formatHint = caption(QStringLiteral("Геометрия хранится в точках принтера. Единицы — как на вкладке «Объект».\nМасштаб не меняет размер этикетки."), formatSection);
    formatHint->setWordWrap(true); formatLayout->addWidget(formatHint);
    auto* fullFormat = new QPushButton(QStringLiteral("Формат…"), labelPage);
    fullFormat->setToolTip(QStringLiteral("Размеры в миллиметрах и стандартные форматы этикеток"));
    fullFormat->setObjectName(QStringLiteral("labelFormatSetup"));
    fullFormat->setProperty("compact", true); formatLayout->addWidget(fullFormat); labelLayout->addWidget(formatSection);
    connect(fullFormat, &QPushButton::clicked, this, &MainWindow::showLabelSetup);
    auto* guidesSection = new WorkspaceSurface(labelPage); auto* guidesLayout = new QVBoxLayout(guidesSection); guidesLayout->setContentsMargins(13, 12, 13, 12); guidesLayout->setSpacing(12);
    guidesLayout->addWidget(sectionHeading(QStringLiteral("Рабочие ориентиры"), guidesSection));
    auto* labelGrid = new QCheckBox(QStringLiteral("Сетка"), guidesSection); labelGrid->setChecked(true); guidesLayout->addWidget(labelGrid);
    auto* guidesHint = caption(QStringLiteral("Сетка и рамки выделения не попадают в печать."), guidesSection); guidesHint->setWordWrap(true); guidesLayout->addWidget(guidesHint);
    labelLayout->addWidget(guidesSection); labelLayout->addStretch();

    auto* workspaceRow = new QHBoxLayout; workspaceRow->setContentsMargins(0, 0, 0, 0); workspaceRow->setSpacing(0);
    auto* palette = new ElementPalette(central); workspaceRow->addWidget(palette);
    auto* splitter = new QSplitter(Qt::Horizontal, central); splitter->setChildrenCollapsible(false);
    workspaceSplitter_ = splitter; splitter->setObjectName(QStringLiteral("workspaceSplitter"));
    splitter->setHandleWidth(1);
    connect(palette, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item->data(Qt::UserRole).toInt() < 0) { canvas_->setFocus(); return; }
        addElement(static_cast<ElementType>(item->data(Qt::UserRole).toInt()));
    });

    auto* center = new WorkspaceSurface(splitter); workspaceCenter_ = center; center->installEventFilter(this);
    auto* centerLayout = new QVBoxLayout(center); centerLayout->setContentsMargins(0, 0, 0, 0); centerLayout->setSpacing(0);
    editorTabs_ = new QTabWidget(center); editorTabs_->setObjectName(QStringLiteral("editorTabs"));
    editorTabs_->setDocumentMode(true);
    editorTabs_->tabBar()->hide();
    connect(viewTabs, &QTabBar::currentChanged, this, [this, viewTabs](int index) {
        if (workspacePreferences_.editorLayout != EditorLayout::Single && index == 1) {
            const QSignalBlocker blocker(viewTabs);
            viewTabs->setCurrentIndex(editorTabs_->currentIndex());
            code_->setFocus();
            return;
        }
        editorTabs_->setCurrentIndex(index);
    });
    connect(editorTabs_, &QTabWidget::currentChanged, viewTabs, &QTabBar::setCurrentIndex);
    auto* canvasHeader = new WorkspaceSurface(center, true); canvasHeader->setFixedHeight(37);
    auto* canvasHeaderRow = new QHBoxLayout(canvasHeader); canvasHeaderRow->setContentsMargins(12, 4, 12, 4); canvasHeaderRow->setSpacing(8);
    editorLayout_ = new QComboBox(canvasHeader);
    editorLayout_->setObjectName(QStringLiteral("editorLayout"));
    editorLayout_->addItems({QStringLiteral("Один вид"), QStringLiteral("ZPL + Дизайн"), QStringLiteral("ZPL + Превью")});
    editorLayout_->setMinimumWidth(editorLayout_->fontMetrics().horizontalAdvance(QStringLiteral("ZPL + Превью")) + 48);
    editorLayout_->setToolTip(QStringLiteral("Показать код рядом с дизайном или чистым превью; обновление при вводе"));
    editorLayout_->setAccessibleName(QStringLiteral("Расположение редакторов"));
    canvasHeaderRow->addWidget(editorLayout_);
    connect(editorLayout_, &QComboBox::currentIndexChanged, this, [this](int index) {
        setEditorLayout(static_cast<EditorLayout>(index));
        persistWorkspace();
    });
    swapEditorSides_ = new QAction(QStringLiteral("⇄"), this);
    swapEditorSides_->setObjectName(QStringLiteral("swapEditorSides"));
    swapEditorSides_->setToolTip(QStringLiteral("Поменять местами код и дизайн / превью"));
    addTool(canvasHeaderRow, swapEditorSides_, true);
    connect(swapEditorSides_, &QAction::triggered, this, [this] {
        if (workspacePreferences_.editorLayout == EditorLayout::Single) return;
        workspacePreferences_.codeOnLeft = !workspacePreferences_.codeOnLeft;
        workspacePreferences_.editorSizes = {workspacePreferences_.editorSizes[1], workspacePreferences_.editorSizes[0]};
        setEditorLayout(workspacePreferences_.editorLayout);
        persistWorkspace();
    });
    syncEditorSelection_ = new QAction(QIcon(new SelectionSyncIconEngine), QStringLiteral("Связать выделение ZPL и дизайна"), this);
    syncEditorSelection_->setObjectName(QStringLiteral("syncEditorSelection"));
    syncEditorSelection_->setCheckable(true);
    syncEditorSelection_->setChecked(workspacePreferences_.selectionSync);
    auto* syncButton = addTool(canvasHeaderRow, syncEditorSelection_, true);
    syncButton->setObjectName(QStringLiteral("sourceSelectionSyncButton"));
    syncButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    syncButton->setIconSize(QSize(18, 18));
    syncButton->setFixedSize(28, 28);
    syncButton->setFocusPolicy(Qt::NoFocus);
    syncButton->setAccessibleName(syncEditorSelection_->text());
    const auto updateSelectionToolTip = [this] {
        syncEditorSelection_->setToolTip(workspacePreferences_.selectionSync
            ? QStringLiteral("Связь выделения ZPL ↔ дизайн включена. Нажмите, чтобы отключить.")
            : QStringLiteral("Связь выделения ZPL ↔ дизайн выключена. Нажмите, чтобы включить."));
    };
    updateSelectionToolTip();
    connect(syncEditorSelection_, &QAction::toggled, this, [this, updateSelectionToolTip](bool enabled) {
        workspacePreferences_.selectionSync = enabled;
        sourceAdapter_->cancelSelection();
        updateSelectionToolTip();
        if (enabled && code_->hasFocus()) syncSourceSelection(true);
        syncDesignSelection();
        persistWorkspace();
    });
    canvasHeaderRow->addStretch();
    grid_ = new QCheckBox(QStringLiteral("Сетка"), canvasHeader); grid_->setObjectName(QStringLiteral("showGrid")); grid_->setChecked(true); grid_->hide();
    connect(labelGrid, &QCheckBox::toggled, grid_, &QCheckBox::setChecked); connect(grid_, &QCheckBox::toggled, labelGrid, &QCheckBox::setChecked);
    snap_ = new QCheckBox(QStringLiteral("Привязка"), canvasHeader); snap_->setObjectName(QStringLiteral("snapEnabled")); snap_->setChecked(true); snap_->hide();
    // Canvas toggles of the canvas-guides concept: 26 px tall, 10 px padding,
    // a 14 px icon 6 px before the caption; tinted with accent ink when on.
    auto* canvasTools = new QHBoxLayout; canvasTools->setSpacing(6); canvasHeaderRow->addLayout(canvasTools);
    QFont toolFont = font(); toolFont.setPixelSize(11);
    for (const auto& [check, icon, name] : {std::tuple{grid_, DesignerIcon::Grid, QStringLiteral("gridToggle")},
                                             std::tuple{snap_, DesignerIcon::Magnet, QStringLiteral("snapToggle")}}) {
        auto* button = new QPushButton(designerIcon(icon), check->text(), canvasHeader); button->setObjectName(name);
        button->setCheckable(true); button->setChecked(check->isChecked()); button->setProperty("compact", true); button->setIconSize({14, 14});
        button->setFixedSize(qCeil(QFontMetricsF(toolFont).horizontalAdvance(check->text())) + 14 + 6 + 20, 26);
        canvasTools->addWidget(button); connect(button, &QPushButton::toggled, check, &QCheckBox::setChecked); connect(check, &QCheckBox::toggled, button, &QPushButton::setChecked);
    }
    canvasSetup_ = new QPushButton(designerIcon(DesignerIcon::More), QString(), canvasHeader); canvasSetup_->setObjectName(QStringLiteral("canvasSetup"));
    canvasSetup_->setCheckable(true); canvasSetup_->setIconSize({14, 14}); canvasSetup_->setFixedSize(30, 26);
    canvasSetup_->setToolTip(QStringLiteral("Холст и направляющие")); canvasSetup_->setAccessibleName(canvasSetup_->toolTip()); canvasTools->addWidget(canvasSetup_);
    connect(canvasSetup_, &QPushButton::clicked, this, &MainWindow::showCanvasLayout);
    auto* propertiesTool = addTool(canvasHeaderRow, propertiesAction_, true);
    propertiesTool->setToolTip(QStringLiteral("Показать или скрыть свойства · Ctrl+Alt+P"));
    centerLayout->addWidget(canvasHeader);
    canvas_ = new LabelCanvas(editorTabs_); canvas_->setObjectName(QStringLiteral("labelCanvas"));
    canvas_->viewport()->installEventFilter(this);
    canvas_->setGeometryLabelsVisible(workspacePreferences_.geometrySizes, workspacePreferences_.geometryCoordinates);
    canvas_->setRulersDocked(workspacePreferences_.rulersDocked);
    editorTabs_->addTab(canvas_, QStringLiteral("  Дизайн  "));
    sourceSlot_ = new QWidget(editorTabs_);
    auto* sourceSlotLayout = new QVBoxLayout(sourceSlot_);
    sourceSlotLayout->setContentsMargins(0, 0, 0, 0);
    auto* sourcePage = new QWidget(sourceSlot_); sourcePage_ = sourcePage;
    sourcePage->setObjectName(QStringLiteral("sourcePage"));
    sourcePage->setMinimumWidth(200);
    sourceSlotLayout->addWidget(sourcePage);
    auto* sourceLayout = new QVBoxLayout(sourcePage);
    auto* sourceBar = new QHBoxLayout;
    sourceBar->addWidget(heading(QStringLiteral("Код ZPL"), sourcePage), 1);
    sourceLayout->addLayout(sourceBar);
    code_ = new ZplSourceEditor(sourcePage); code_->setObjectName(QStringLiteral("zplEditor"));
    sourceAdapter_ = new SourceEditorAdapter(*session_, *static_cast<ZplSourceEditor*>(code_), this);
    sourceAdapter_->setSelectionSyncEnabled(workspacePreferences_.selectionSync);
    connect(sourceAdapter_, &SourceEditorAdapter::selectedIndicesRequested, this, [this](const QList<int>& indices) {
        canvas_->setSelectedIndices(indices);
        if (canvas_->signalsBlocked()) syncSelection();
    });
    connect(sourceAdapter_, &SourceEditorAdapter::applyFinished, this, [this](bool success, const QString& error) {
        if (success) refreshPreview();
        else {
            sourceStatus_->setText(QStringLiteral("ZPL не применён: %1 Последний применённый макет сохранён.").arg(error));
            setNotice(sourceStatus_->text());
            refreshSourceDiagnostics();
            previewWidget_->setPreview({}, session_->document().dpi, sourceStatus_->text());
        }
    });
    connect(code_, &QPlainTextEdit::undoAvailable, this, &MainWindow::updateHistoryActions);
    connect(code_, &QPlainTextEdit::redoAvailable, this, &MainWindow::updateHistoryActions);
    connect(qApp, &QApplication::focusChanged, this, [this] { updateHistoryActions(); });
    auto* findSource = new QPushButton(QStringLiteral("Поиск…"), sourcePage);
    auto* replaceSource = new QPushButton(QStringLiteral("Заменить…"), sourcePage);
    sourceBar->insertWidget(1, findSource); sourceBar->insertWidget(2, replaceSource);
    sourceStatus_ = new QLabel(sourcePage);
    sourceStatus_->setObjectName(QStringLiteral("sourceStatus"));
    sourceStatus_->setWordWrap(true);
    sourceStatus_->setTextFormat(Qt::PlainText);
    sourceStatus_->setMinimumWidth(0);
    sourceStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    sourceLayout->addWidget(sourceStatus_);
    connect(findSource, &QPushButton::clicked, static_cast<ZplSourceEditor*>(code_), &ZplSourceEditor::showFind);
    connect(replaceSource, &QPushButton::clicked, static_cast<ZplSourceEditor*>(code_), &ZplSourceEditor::showReplace);
    code_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    code_->setLineWrapMode(QPlainTextEdit::NoWrap); sourceLayout->addWidget(code_);
    editorTabs_->addTab(sourceSlot_, QStringLiteral("  Код ZPL  "));
    previewWidget_ = new PreviewWidget(editorTabs_);
    editorTabs_->addTab(previewWidget_, QStringLiteral("  Превью  "));
    connect(previewWidget_, &PreviewWidget::exportRequested, this, &MainWindow::exportPng);
    connect(previewWidget_, &PreviewWidget::pageRequested, this, [this](int page) {
        previews_->selectPage(page);
    });
    editorSplitter_ = new QSplitter(Qt::Horizontal, center);
    editorSplitter_->setObjectName(QStringLiteral("editorSplitter"));
    editorSplitter_->installEventFilter(this);
    editorSplitter_->setChildrenCollapsible(false);
    editorSplitter_->setHandleWidth(5);
    editorSplitter_->addWidget(editorTabs_);
    editorSplitter_->setStretchFactor(0, 1);
    centerLayout->addWidget(editorSplitter_, 1);
    connect(editorSplitter_, &QSplitter::splitterMoved, this, [this] {
        if (applyingEditorLayout_ || workspacePreferences_.editorLayout == EditorLayout::Single) return;
        workspacePreferences_.editorSizes = editorSplitter_->sizes();
        persistWorkspace();
    });
    notice_ = new WorkspaceStatus(central); notice_->setObjectName(QStringLiteral("workspaceStatus")); notice_->setMinimumWidth(0);
    notice_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto statusFont = notice_->font(); statusFont.setPixelSize(10); notice_->setFont(statusFont);
    focusBar_ = new WorkspaceSurface(center, false, true); focusBar_->setObjectName(QStringLiteral("focusContextBar"));
    auto* focusRow = new QHBoxLayout(focusBar_); focusRow->setContentsMargins(7, 7, 7, 7); focusRow->setSpacing(5);
    focusSummary_ = new QLabel(focusBar_); focusSummary_->setObjectName(QStringLiteral("focusSelectionSummary"));
    focusSummary_->setTextFormat(Qt::PlainText); focusSummary_->setMinimumWidth(0);
    auto focusFont = focusSummary_->font(); focusFont.setPixelSize(11); focusSummary_->setFont(focusFont);
    focusRow->addWidget(focusSummary_);
    addTool(focusRow, propertiesAction_, true);
    auto* focusDuplicate = new QPushButton(QStringLiteral("Дублировать"), focusBar_); focusDuplicate->setProperty("compact", true); focusRow->addWidget(focusDuplicate);
    connect(focusDuplicate, &QPushButton::clicked, this, &MainWindow::duplicateElement);
    auto* focusShadow = new QGraphicsDropShadowEffect(focusBar_); focusShadow->setBlurRadius(15); focusShadow->setOffset(0, 3); focusShadow->setColor(QColor(0, 0, 0, 18)); focusBar_->setGraphicsEffect(focusShadow);
    auto* zoomControls = new WorkspaceSurface(center, true); zoomControls->setFixedHeight(34);
    canvasZoomControls_ = zoomControls;
    zoomControls->setObjectName(QStringLiteral("canvasZoomControls"));
    zoomControls->installEventFilter(this);
    auto* zoomRow = new QHBoxLayout(zoomControls); zoomRow->setContentsMargins(11, 4, 11, 4); zoomRow->setSpacing(9);
    canvasSelection_ = new ElidedCaption(zoomControls);
    auto selectionFont = canvasSelection_->font(); selectionFont.setPixelSize(10); canvasSelection_->setFont(selectionFont);
    canvasSelection_->setForegroundRole(QPalette::PlaceholderText);
    canvasSelection_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    canvasSelection_->setMaximumWidth(140);
    zoomRow->addWidget(canvasSelection_); zoomRow->addStretch();
    canvasZoomTitle_ = caption(QStringLiteral("Масштаб"), zoomControls);
    zoomRow->addWidget(canvasZoomTitle_);
    auto* zoom = new QComboBox(zoomControls); zoom->setObjectName(QStringLiteral("canvasZoom")); zoom->setProperty("compact", true); zoom->setFixedWidth(85); zoom->setFixedHeight(25);
    zoom->setEditable(true);
    zoom->setInsertPolicy(QComboBox::NoInsert);
    zoom->lineEdit()->setReadOnly(true);
    zoom->lineEdit()->setAttribute(Qt::WA_TransparentForMouseEvents);
    zoom->setProperty("popupOnWholeControl", true);
    zoom->setCompleter(nullptr);
    auto zoomFont = zoom->font(); zoomFont.setPixelSize(11); zoom->lineEdit()->setFont(zoomFont);
    zoom->addItem(QStringLiteral("Вписать"), 0);
    for (int percent : {10, 25, 50, 75, 100, 125, 150, 200, 300, 400, 600, 800})
        zoom->addItem(QStringLiteral("%1%").arg(percent), percent);
    zoom->setMaxVisibleItems(zoom->count());
    zoomRow->addWidget(zoom);
    canvasDimensionsFooter_ = new ElidedCaption(zoomControls);
    canvasDimensionsFooter_->setObjectName(QStringLiteral("canvasDimensionsFooter"));
    canvasDimensionsFooter_->setAccessibleName(QStringLiteral("Размеры этикетки"));
    canvasDimensionsFooter_->setTextFormat(Qt::PlainText);
    canvasDimensionsFooter_->setFont(canvasSelection_->font());
    canvasDimensionsFooter_->setForegroundRole(QPalette::PlaceholderText);
    canvasDimensionsFooter_->setAlignment(Qt::AlignCenter);
    canvasDimensionsFooter_->setAttribute(Qt::WA_TransparentForMouseEvents);
    canvasDimensionsFooter_->setText(canvas_->dimensionsFooterText());
    connect(canvas_, &LabelCanvas::dimensionsFooterChanged, this, [this](const QString& text) {
        canvasDimensionsFooter_->setText(text);
        canvasDimensionsFooter_->setToolTip(text);
        canvasZoomControls_->setToolTip(text);
        positionCanvasDimensionsFooter();
    });
    connect(zoom, &QComboBox::activated, this, [this, zoom](int index) {
        if (index < 0) return;
        const int percent = zoom->itemData(index).toInt();
        if (!percent) canvas_->fitLabel();
        else canvas_->setZoomPercent(percent);
    });
    zoomLabel_ = new QLabel(QStringLiteral("100%"), zoomControls); zoomLabel_->hide();
    connect(canvas_, &LabelCanvas::zoomChanged, zoom, [this, zoom](int percent) {
        const QSignalBlocker block(zoom);
        const int index = canvas_->isFitMode() ? 0 : zoom->findData(percent);
        zoom->setCurrentIndex(index);
        if (index < 0) zoom->setEditText(QStringLiteral("%1%").arg(percent));
    });
    centerLayout->addWidget(zoomControls);

    inspectorSlot_ = new QWidget(splitter); inspectorSlot_->setObjectName(QStringLiteral("inspectorSlot"));
    inspectorSlot_->setMinimumWidth(236); inspectorSlot_->setMaximumWidth(430);
    auto* inspectorSlotLayout = new QVBoxLayout(inspectorSlot_); inspectorSlotLayout->setContentsMargins(0, 0, 0, 0);
    inspectorCard_ = new WorkspaceSurface(center, false, true); inspectorCard_->setObjectName(QStringLiteral("focusInspectorCard"));
    auto* cardLayout = new QVBoxLayout(inspectorCard_); cardLayout->setContentsMargins(1, 1, 1, 1);
    inspectorCard_->hide();
    inspectorTabs_ = new InspectorTabs(inspectorSlot_); inspectorTabs_->setMinimumWidth(0); inspectorSlotLayout->addWidget(inspectorTabs_);
    inspectorTabs_->setObjectName(QStringLiteral("inspectorTabs"));
    inspectorTabs_->tabBar()->setExpanding(false); inspectorTabs_->tabBar()->setProperty("compact", true);
    auto inspectorFont = inspectorTabs_->tabBar()->font(); inspectorFont.setPixelSize(11); inspectorTabs_->tabBar()->setFont(inspectorFont);
    auto* scroll = new QScrollArea(inspectorTabs_); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("propertiesScrollArea"));
    // Page backgrounds do not need a section separator: the floating card owns
    // the outer border, including its rounded bottom corners.
    propertiesPanel_ = new PropertiesPanel(session_, previews_, selection_, scroll);
    scroll->setWidget(propertiesPanel_);
    connect(propertiesPanel_, &PropertiesPanel::editRequested, this, [this](const EditOperation& operation, const QString& title) {
        executeOperation(operation, title);
    });
    connect(propertiesPanel_, &PropertiesPanel::resizeRequested, this, [this](QSize size) { resizeElement(selected_, size); });
    connect(propertiesPanel_, &PropertiesPanel::unitsRequested, this, [this](CanvasUnit unit) {
        auto settings = session_->layoutSettings(); settings.unit = unit;
        session_->setLayoutSettings(std::move(settings), QStringLiteral("Единицы измерения"));
    });
    connect(propertiesPanel_, &PropertiesPanel::noticeRequested, this, &MainWindow::setNotice);
    connect(propertiesPanel_, &PropertiesPanel::alignmentRequested, this, [this](const QString& name) {
        if (auto* action = findChild<QAction*>(name)) action->trigger();
    });
    connect(propertiesPanel_, &PropertiesPanel::imageRequested, this, &MainWindow::editImage);
    connect(propertiesPanel_, &PropertiesPanel::tableRequested, this, &MainWindow::editTable);
    connect(propertiesPanel_, &PropertiesPanel::qrRequested, this, &MainWindow::editQrPayload);
    connect(propertiesPanel_, &PropertiesPanel::barcodeRequested, this, &MainWindow::editBarcodeOptions);
    connect(propertiesPanel_, &PropertiesPanel::textRequested, this, &MainWindow::editAdvancedText);
    connect(propertiesPanel_, &PropertiesPanel::gs1Requested, this, &MainWindow::showGs1Builder);
    connect(propertiesPanel_, &PropertiesPanel::valuesRequested, this, &MainWindow::showPlaceholderValues);
    connect(propertiesPanel_, &PropertiesPanel::duplicateRequested, this, &MainWindow::duplicateElement);
    connect(propertiesPanel_, &PropertiesPanel::deleteRequested, this, &MainWindow::deleteElement);
    inspectorTabs_->addTab(scroll, QStringLiteral("Объект"));
    inspectorTabs_->addTab(labelPage, QStringLiteral("Этикетка"));
    auto* layerPage = new WorkspaceSurface(splitter); auto* layerLayout = new QVBoxLayout(layerPage);
    layersPanel_ = layerPage; layerPage->setObjectName(QStringLiteral("layersPanel"));
    layerPage->setMinimumWidth(150); layerPage->setMaximumWidth(320);
    layerLayout->setContentsMargins(6, 0, 6, 0); layerLayout->setSpacing(0);
    auto* layerHeader = new QWidget(layerPage); layerHeader->setFixedHeight(37); auto* layerHeaderRow = new QHBoxLayout(layerHeader); layerHeaderRow->setContentsMargins(7, 0, 7, 0);
    layerHeaderRow->addWidget(heading(QStringLiteral("Слои"), layerHeader)); layerHeaderRow->addStretch(); layerCount_ = caption(QString(), layerHeader); layerHeaderRow->addWidget(layerCount_); layerLayout->addWidget(layerHeader);
    layers_ = new LayersList(layerPage); layers_->setObjectName(QStringLiteral("layerList")); layerLayout->addWidget(layers_);
    layers_->setFrameShape(QFrame::NoFrame); layers_->setMouseTracking(true); layers_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layers_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto* layerOrderHint = caption(QStringLiteral("В приложении:\nCtrl + клик — несколько\nF2 — переименовать"), layerPage); layerOrderHint->setContentsMargins(7, 10, 7, 10); layerLayout->addWidget(layerOrderHint);
    auto* orderTitle = caption(QStringLiteral("Порядок объектов"), layerPage, 11); orderTitle->setContentsMargins(7, 10, 7, 8); layerLayout->addWidget(orderTitle);
    auto* layerButtons = new QHBoxLayout;
    auto* front = new QPushButton(QStringLiteral("Выше"), layerPage); auto* back = new QPushButton(QStringLiteral("Ниже"), layerPage);
    front->setProperty("compact", true); back->setProperty("compact", true); layerButtons->setContentsMargins(7, 0, 7, 8); layerButtons->setSpacing(4); layerButtons->addWidget(front); layerButtons->addWidget(back); layerLayout->addLayout(layerButtons);
    auto* layerDetails = new QWidget(layerPage); auto* layerDetailsLayout = new QVBoxLayout(layerDetails); layerDetailsLayout->setContentsMargins(7, 4, 7, 4); layerDetailsLayout->setSpacing(6);
    auto* layerMore = new QToolButton(layerPage); layerMore->setText(QStringLiteral("Параметры слоя")); layerMore->setCheckable(true); layerMore->setProperty("flat", true); layerMore->setArrowType(Qt::RightArrow); layerMore->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    layerLayout->addWidget(layerMore); layerLayout->addWidget(layerDetails); layerDetails->hide(); connect(layerMore, &QToolButton::toggled, layerDetails, &QWidget::setVisible);
    auto* edgeButtons = new QHBoxLayout;
    auto* top = new QPushButton(QStringLiteral("В начало"), layerPage);
    top->setToolTip(QStringLiteral("На передний план"));
    top->setObjectName(QStringLiteral("layerBringFront"));
    auto* bottom = new QPushButton(QStringLiteral("В конец"), layerPage);
    bottom->setToolTip(QStringLiteral("На задний план"));
    bottom->setObjectName(QStringLiteral("layerSendBack"));
    edgeButtons->addWidget(top); edgeButtons->addWidget(bottom); layerDetailsLayout->addLayout(edgeButtons);
    connect(top, &QPushButton::clicked, this, [this] { moveLayer(200); });
    connect(bottom, &QPushButton::clicked, this, [this] { moveLayer(-200); });
    auto* rename = new QPushButton(QStringLiteral("Переименовать…"), layerPage);
    rename->setObjectName(QStringLiteral("renameLayer")); layerDetailsLayout->addWidget(rename);
    connect(rename, &QPushButton::clicked, this, &MainWindow::renameLayer);
    layerLocked_ = new QCheckBox(QStringLiteral("Заблокирован"), layerPage);
    layerLocked_->setObjectName(QStringLiteral("layerLocked")); layerDetailsLayout->addWidget(layerLocked_);
    layerVisible_ = new QCheckBox(QStringLiteral("Виден на холсте"), layerPage);
    layerVisible_->setObjectName(QStringLiteral("layerVisible")); layerDetailsLayout->addWidget(layerVisible_);
    layerPrintable_ = new QCheckBox(QStringLiteral("Печатать и экспортировать"), layerPage);
    layerPrintable_->setObjectName(QStringLiteral("layerPrintable")); layerDetailsLayout->addWidget(layerPrintable_);
    layerVisible_->setToolTip(QStringLiteral("Видимость на холсте не меняет печать."));
    layerLocked_->setToolTip(QStringLiteral("Блокировка защищает от перемещения и изменения."));
    layers_->setToolTip(QStringLiteral("Ctrl/Shift + клик — выбор нескольких объектов. Ctrl+Alt+клик на холсте — объект под другим."));
    connect(layerLocked_, &QCheckBox::clicked, this, [this](bool value) { setLayerFlag(LayerFlag::Locked, value); });
    connect(layerVisible_, &QCheckBox::clicked, this, [this](bool value) { setLayerFlag(LayerFlag::Visible, value); });
    connect(layerPrintable_, &QCheckBox::clicked, this, [this](bool value) { setLayerFlag(LayerFlag::Printable, value); });
    layerLayout->addStretch(1);
    auto* diagnosticsPage = new QWidget(center);
    diagnosticsPanel_ = diagnosticsPage;
    diagnosticsPage->setObjectName(QStringLiteral("workspaceDiagnosticsPanel"));
    diagnosticsPage->setMaximumHeight(210);
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsPage);
    diagnosticsLayout->setContentsMargins(10, 4, 10, 6);
    auto* diagnosticsHeader = new QHBoxLayout;
    diagnosticsHeader->addWidget(heading(QStringLiteral("Проверка"), diagnosticsPage));
    diagnosticsHeader->addStretch();
    auto* closeDiagnostics = new QToolButton(diagnosticsPage); closeDiagnostics->setText(QStringLiteral("Скрыть"));
    diagnosticsHeader->addWidget(closeDiagnostics); diagnosticsLayout->addLayout(diagnosticsHeader);
    connect(closeDiagnostics, &QToolButton::clicked, diagnosticsAction_, [this] { diagnosticsAction_->setChecked(false); });
    diagnostics_ = new QPlainTextEdit(diagnosticsPage); diagnostics_->setReadOnly(true); diagnostics_->setObjectName(QStringLiteral("diagnostics"));
    diagnosticsLayout->addWidget(diagnostics_);
    sourceDiagnostics_ = new SourceDiagnosticsList(diagnosticsPage);
    sourceDiagnostics_->setObjectName(QStringLiteral("sourceDiagnostics"));
    sourceDiagnostics_->setToolTip(QStringLiteral("Двойной щелчок — перейти к команде ZPL"));
    sourceDiagnostics_->setWordWrap(true);
    diagnosticsLayout->addWidget(sourceDiagnostics_);
    connect(sourceDiagnostics_, &SourceDiagnosticsList::sourceActivated, this, [this](int offset) {
        editorTabs_->setCurrentIndex(1);
        static_cast<ZplSourceEditor*>(code_)->goToSourceOffset(offset);
    });
    centerLayout->addWidget(diagnosticsPage); diagnosticsPage->hide();
    splitter->insertWidget(0, layerPage); splitter->insertWidget(1, center); splitter->insertWidget(2, inspectorSlot_);
    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setStretchFactor(2, 0);
    workspaceRow->addWidget(splitter, 1); outer->addLayout(workspaceRow, 1);
    connect(splitter, &QSplitter::splitterMoved, this, [this] {
        if (applyingWorkspace_) return;
        rememberWorkspaceSizes(); persistWorkspace();
    });
    setCentralWidget(central);
    statusBar()->setFixedHeight(30); statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(notice_, 1);
    statusBar()->addPermanentWidget(caption(QStringLiteral("Ctrl + колесо — масштаб"), this));
    auto* resetView = new QToolButton(this); resetView->setText(QStringLiteral("Сбросить вид")); resetView->setProperty("flat", true); resetView->setProperty("compact", true);
    statusBar()->addPermanentWidget(resetView); connect(resetView, &QToolButton::clicked, canvas_, &LabelCanvas::fitLabel);

    connect(grid_, &QCheckBox::toggled, this, [this](bool visible) {
        auto settings = session_->layoutSettings(); settings.gridVisible = visible;
        session_->setLayoutSettings(std::move(settings), QStringLiteral("Показать/скрыть сетку"));
    });
    connect(snap_, &QCheckBox::toggled, this, [this](bool enabled) {
        auto settings = session_->layoutSettings(); settings.snapEnabled = enabled;
        session_->setLayoutSettings(std::move(settings), QStringLiteral("Привязка холста"));
    });
    connect(canvas_, &LabelCanvas::layoutSettingsEdited, this, [this](CanvasLayoutSettings settings) {
        session_->setLayoutSettings(std::move(settings), QStringLiteral("Изменить направляющие"));
    });
    connect(canvas_, &LabelCanvas::zoomChanged, this, [this](int zoom) { zoomLabel_->setText(QStringLiteral("%1%").arg(zoom)); });
    connect(canvas_, &LabelCanvas::selectionSetChanged, this, [this] {
        if (!sourceAdapter_->isSyncingSelection()) {
            sourceAdapter_->cancelSelection();
        }
        syncSelection();
        if (!sourceAdapter_->isSyncingSelection()) syncDesignSelection(true);
    });
    connect(canvas_, &LabelCanvas::editingStarted, this, [this] {
        if (previews_->pending()) refreshPreview();
    });
    connect(canvas_, &LabelCanvas::elementDropped, this, &MainWindow::addElement);
    connect(canvas_, &LabelCanvas::imageDropped, this, [this](const QString& path, QPoint p) { importImage(path, p); });
    connect(canvas_, &LabelCanvas::deleteRequested, this, &MainWindow::deleteElement);
    connect(canvas_, &LabelCanvas::duplicateRequested, this, &MainWindow::duplicateElement);
    connect(canvas_, &LabelCanvas::elementMoved, this, [this](int index, QPoint position) {
        if (!session_->document().visualEditable || index < 0 || index >= session_->document().elements.size()
            || session_->document().elements[index].locked) return;
        ElementProperties properties; properties.x = position.x(); properties.y = position.y();
        executeOperation(UpdateElementProperties{session_->document().elements[index].id, properties},
            QStringLiteral("Переместить элемент"), index);
    });
    connect(canvas_, &LabelCanvas::elementResized, this, [this](int index, QSize size) {
        resizeElement(index, size);
    });
    connect(canvas_, &LabelCanvas::elementEditRequested, this, [this](int index) {
        selectElement(index);
        if (index >= 0 && session_->document().elements[index].type == ElementType::Table) editTable();
        else if (index >= 0 && workspacePreferences_.focus) {
            propertiesAction_->setChecked(true);
            inspectorTabs_->setCurrentIndex(0);
            propertiesPanel_->focusContent();
        }
    });
    connect(canvas_, &LabelCanvas::elementsMoved, this, [this](const QList<int>& indices, QPoint delta) {
        if (!session_->hasPendingSource()) session_->moveElements(indices, delta);
    });
    connect(front, &QPushButton::clicked, this, [this] { moveLayer(1); });
    connect(back, &QPushButton::clicked, this, [this] { moveLayer(-1); });
    connect(layers_, &QListWidget::itemSelectionChanged, this, [this] {
        if (updating_) return;
        selection_->setIds(layers_->selectedIds(), session_->document());
        canvas_->setSelectedIndices(selection_->indices(session_->document()));
        syncSelection();
    });
    connect(layers_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (updating_ || !current || !current->isSelected()) return;
        auto indices = canvas_->selectedIndices();
        const int primary = current->data(Qt::UserRole).toInt();
        if (indices.removeOne(primary)) { indices.append(primary); canvas_->setSelectedIndices(indices); }
    });
    auto labelChanged = [this] {
        if (updating_ || session_->hasPendingSource() || !session_->document().visualEditable) return;
        auto values = labelSettings(session_->document());
        values.width = labelWidth_->value(); values.height = labelHeight_->value(); values.dpi = dpi_->currentData().toInt();
        executeOperation(values, QStringLiteral("Параметры этикетки"));
    };
    connect(labelWidth_, &QSpinBox::valueChanged, this, labelChanged);
    connect(labelHeight_, &QSpinBox::valueChanged, this, labelChanged);
    connect(dpi_, &QComboBox::currentIndexChanged, this, labelChanged);
    connect(sourceAdapter_, &SourceEditorAdapter::sourceEdited, this, [this] {
        if (updating_ || code_->isReadOnly()) return;
        canvas_->setEnabled(!session_->hasPendingSource());
        for (auto* field : {labelWidth_, labelHeight_}) field->setEnabled(!session_->hasPendingSource() && session_->document().visualEditable);
        dpi_->setEnabled(!session_->hasPendingSource() && session_->document().visualEditable);
        refreshProperties();
        updateTitle(); updateHistoryActions();
        if (session_->hasPendingSource()) {
            sourceDiagnostics_->clear();
            sourceDiagnostics_->hide();
            const bool live = workspacePreferences_.editorLayout != EditorLayout::Single;
            sourceStatus_->setText(live ? QStringLiteral("Обновление после ввода…")
                : QStringLiteral("Код изменён. Перейдите в «Дизайн» или «Превью», чтобы посмотреть результат."));
            setNotice(sourceStatus_->text());
            if (!live) previewWidget_->setPreview({}, session_->document().dpi,
                QStringLiteral("Код ZPL изменён. Откройте вкладку «Превью», чтобы посмотреть результат."));
            if (live) sourceAdapter_->scheduleApply();
        } else { updateEditorPresentation(); refreshPreview(); }
        syncSourceSelection(code_->hasFocus());
        syncDesignSelection();
    });
    connect(editorTabs_, &QTabWidget::currentChanged, this, [this, zoomControls](int index) {
        zoomControls->setVisible(index == 0 && !workspacePreferences_.focus);
        if (!applyingEditorLayout_ && workspacePreferences_.focus
            && workspacePreferences_.editorLayout == EditorLayout::Single)
            applyWorkspaceLayout();
        if (updating_ || applyingEditorLayout_) return;
        if (workspacePreferences_.editorLayout != EditorLayout::Single) {
            if (index == 1) {
                editorTabs_->setCurrentIndex(workspacePreferences_.editorLayout == EditorLayout::CodePreview ? 2 : 0);
                code_->setFocus();
                return;
            }
            workspacePreferences_.editorLayout = index == 2 ? EditorLayout::CodePreview : EditorLayout::CodeDesign;
            const QSignalBlocker blocker(editorLayout_);
            editorLayout_->setCurrentIndex(static_cast<int>(workspacePreferences_.editorLayout));
            updateEditorPresentation();
            persistWorkspace();
            return;
        }
        if (editorTabs_->currentWidget() == previewWidget_) showPreview();
        else if (index == 0 && session_->hasPendingSource() && !applySource()) editorTabs_->setCurrentIndex(1);
        if (index == 1) syncDesignSelection(true);
    });
}

void MainWindow::setEditorLayout(EditorLayout layout) {
    applyingEditorLayout_ = true;
    const bool wasSplit = editorSplitter_->count() == 2;
    const bool sourceFocused = code_->hasFocus();
    workspacePreferences_.editorLayout = layout;
    const bool split = layout != EditorLayout::Single;
    const QSignalBlocker blocker(editorLayout_);
    editorLayout_->setCurrentIndex(static_cast<int>(layout));
    if (split) {
        editorSplitter_->insertWidget(workspacePreferences_.codeOnLeft ? 0 : 1, sourcePage_);
        editorSplitter_->setStretchFactor(0, 1);
        editorSplitter_->setStretchFactor(1, 1);
        sourcePage_->show();
        editorTabs_->setCurrentIndex(layout == EditorLayout::CodePreview ? 2 : 0);
        editorSplitter_->setSizes(workspacePreferences_.editorSizes);
    } else if (wasSplit) {
        sourceSlot_->layout()->addWidget(sourcePage_);
        sourcePage_->show();
        if (session_->hasPendingSource() || sourceFocused) editorTabs_->setCurrentIndex(1);
    }
    if (sourceFocused) code_->setFocus(Qt::OtherFocusReason);
    swapEditorSides_->setEnabled(split);
    sourceAdapter_->cancelApply();
    if (session_->hasPendingSource()) {
        sourceStatus_->setText(split ? QStringLiteral("Обновление после ввода…")
            : QStringLiteral("Код изменён. Перейдите в «Дизайн» или «Превью», чтобы посмотреть результат."));
        if (split) sourceAdapter_->scheduleApply();
    }
    applyingEditorLayout_ = false;
    updateEditorPresentation();
    updateHistoryActions();
    applyWorkspaceLayout();
    syncDesignSelection(!sourceFocused);
}

void MainWindow::updateEditorPresentation() {
    if (session_->hasPendingSource()) return;
    sourceStatus_->setText(workspacePreferences_.editorLayout == EditorLayout::Single
        ? QStringLiteral("ZPL применяется при переходе в «Дизайн» или «Превью», сохранении и печати.")
        : !session_->document().visualEditable
            ? QStringLiteral("Команды сохранены. Доступны правки ZPL и превью; визуальное редактирование этого исходника не поддерживается.")
            : workspacePreferences_.editorLayout == EditorLayout::CodePreview
                ? QStringLiteral("Превью обновляется автоматически при вводе ZPL.")
                : QStringLiteral("Код и дизайн синхронизируются автоматически."));
}

void MainWindow::rememberWorkspaceSizes() {
    if (!workspaceSplitter_ || applyingWorkspace_) return;
    auto& saved = workspacePreferences_.focus ? workspacePreferences_.focusSizes : workspacePreferences_.balanceSizes;
    const auto sizes = workspaceSplitter_->sizes();
    if (sizes.size() != 3) return;
    for (int i = 0; i < 3; ++i) if (sizes[i] >= 50) saved[i] = sizes[i];
}

void MainWindow::persistWorkspace() {
    workspacePreferences_.dark = designerTheme() == DesignerTheme::Dark;
    if (!saveWorkspacePreferences(workspacePreferences_))
        statusBar()->showMessage(QStringLiteral("Не удалось сохранить настройки рабочего пространства."), 5000);
}

void MainWindow::restoreCanvasZoom() {
    if (workspacePreferences_.canvasZoomPercent == 0) {
        canvas_->fitLabel();
    } else {
        canvas_->setZoomPercent(workspacePreferences_.canvasZoomPercent);
        canvas_->centerLabel();
    }
}

void MainWindow::setWorkspaceMode(bool focus) {
    if (workspacePreferences_.focus == focus) return;
    rememberWorkspaceSizes();
    workspacePreferences_.focus = focus;
    applyWorkspaceLayout();
    persistWorkspace();
}

void MainWindow::applyWorkspaceLayout() {
    applyingWorkspace_ = true;
    const bool focus = workspacePreferences_.focus;
    canvasZoomControls_->setVisible(!focus && editorTabs_->currentIndex() == 0);
    const bool layersVisible = focus ? workspacePreferences_.focusLayers : workspacePreferences_.balanceLayers;
    const bool propertiesVisible = focus ? workspacePreferences_.focusProperties : workspacePreferences_.balanceProperties;
    const QSignalBlocker balanceBlock(balanceAction_), focusBlock(focusAction_);
    const QSignalBlocker layersBlock(layersAction_), propertiesBlock(propertiesAction_);
    balanceAction_->setChecked(!focus); focusAction_->setChecked(focus);
    layersAction_->setChecked(layersVisible); propertiesAction_->setChecked(propertiesVisible);
    layersPanel_->setVisible(layersVisible);
    const bool floating = focus && workspacePreferences_.editorLayout == EditorLayout::Single
        && editorTabs_->currentIndex() == 0;
    QWidget* inspectorHost = floating ? inspectorCard_ : inspectorSlot_;
    if (inspectorTabs_->parentWidget() != inspectorHost)
        inspectorHost->layout()->addWidget(inspectorTabs_);
    static_cast<InspectorTabs*>(inspectorTabs_)->setRoundedCorners(floating);
    inspectorSlot_->setVisible(!floating && propertiesVisible);
    inspectorCard_->setVisible(floating && propertiesVisible);
    inspectorTabs_->setVisible(propertiesVisible);
    focusBar_->setVisible(floating);
    workspaceSplitter_->setSizes(focus ? workspacePreferences_.focusSizes : workspacePreferences_.balanceSizes);
    canvas_->setWorkspaceInsets(QMargins(35, 50, floating && propertiesVisible ? 274 : 35, floating ? 80 : 45));
    applyingWorkspace_ = false;
    positionWorkspaceOverlays();
    QTimer::singleShot(0, this, &MainWindow::positionWorkspaceOverlays);
    refreshFocusSummary();
}

void MainWindow::refreshFocusSummary() {
    if (!focusSummary_) return;
    const auto indices = canvas_->selectedIndices();
    const bool validSelection = selected_ >= 0 && selected_ < session_->document().elements.size();
    QString text;
    if (indices.isEmpty()) text = QStringLiteral("Выберите объект на этикетке");
    else if (indices.size() > 1) text = QStringLiteral("Выбрано объектов: %1").arg(indices.size());
    else if (validSelection) {
        const auto& element = session_->document().elements[selected_];
        const bool millimeters = session_->layoutSettings().unit == CanvasUnit::Millimeters;
        const auto coordinate = [this, millimeters](int dots) {
            return millimeters ? locale().toString(dots * 25.4 / std::max(1, session_->document().dpi), 'f', 2)
                               : locale().toString(dots);
        };
        text = QStringLiteral("%1   ·   X %2   Y %3 %4")
            .arg(element.name.isEmpty() ? elementTypeName(element.type) : element.name,
                 coordinate(element.x), coordinate(element.y),
                 millimeters ? QStringLiteral("мм") : QStringLiteral("dots"));
    }
    focusSummary_->setText(text);
    focusSummary_->setToolTip(text);
    if (canvasSelection_) canvasSelection_->setText(indices.isEmpty() ? QStringLiteral("Выберите объект")
        : validSelection ? session_->document().elements[selected_].name : QString());
    if (canvasSelection_) canvasSelection_->setToolTip(canvasSelection_->text());
    positionWorkspaceOverlays();
}

void MainWindow::positionCanvasDimensionsFooter() {
    if (!canvasDimensionsFooter_ || !canvasZoomTitle_) return;
    // Use equal clearance on both sides so the caption stays at the center of
    // the entire panel, independent of the selection name and zoom controls.
    const int width = canvasZoomControls_->width();
    const int side = std::min(width / 2, std::max(canvasSelection_->geometry().right() + 10,
                                                width - canvasZoomTitle_->x() + 9));
    canvasDimensionsFooter_->setGeometry(canvasZoomControls_->rect());
    canvasDimensionsFooter_->setContentsMargins(side, 0, side, 0);
    canvasDimensionsFooter_->raise();
}

void MainWindow::positionWorkspaceOverlays() {
    positionCanvasDimensionsFooter();
    if (!workspaceCenter_ || !focusBar_ || !inspectorCard_) return;
    const auto area = workspaceCenter_->rect();
    const QRect canvasArea(canvas_->viewport()->mapTo(workspaceCenter_, QPoint{}), canvas_->viewport()->size());
    const int stripWidth = std::min(canvasArea.width() - 30, focusBar_->sizeHint().width());
    focusBar_->setGeometry(canvasArea.left() + (canvasArea.width() - stripWidth) / 2,
                          canvasArea.bottom() + 1 - 16 - 41,
                          std::max(1, stripWidth), 41);
    const QRect editorArea(editorSplitter_->mapTo(workspaceCenter_, QPoint{}), editorSplitter_->size());
    constexpr int cardMargin = 14;
    const int cardTop = editorArea.top() + 18;
    inspectorCard_->setGeometry(area.width() - cardMargin - 238, cardTop, 238,
                               std::max(1, std::min(editorArea.bottom(), canvasArea.bottom()) + 1 - cardMargin - cardTop));
    if (focusBar_->isVisible()) focusBar_->raise();
    if (inspectorCard_->isVisible()) inspectorCard_->raise();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == canvasZoomControls_
        && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest))
        positionCanvasDimensionsFooter();
    if ((watched == workspaceCenter_ && event->type() == QEvent::Resize)
        || (canvas_ && watched == canvas_->viewport() && event->type() == QEvent::Resize)
        || (watched == editorSplitter_ && (event->type() == QEvent::Resize || event->type() == QEvent::Move)))
        positionWorkspaceOverlays();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && darkThemeAction_) {
        const QSignalBlocker blocker(darkThemeAction_);
        workspacePreferences_.dark = designerTheme() == DesignerTheme::Dark;
        darkThemeAction_->setChecked(workspacePreferences_.dark);
    }
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(QStringLiteral("Файл"));
    file->addAction(QStringLiteral("Новая этикетка"), this, [this] { newDocument(); });
    file->addAction(QStringLiteral("Открыть проект или ZPL…"), this, [this] { chooseOpen(); });
    file->addAction(QStringLiteral("Восстановить черновик…"), this, [this] { showRecoveryDialog(); })
        ->setObjectName(QStringLiteral("recoverDraft"));
    file->addAction(QStringLiteral("Шаблон из INI…"), this, [this] { choosePreset(); });
    file->addAction(findChild<QAction*>(QStringLiteral("saveAction")));
    file->addAction(QStringLiteral("Сохранить как…"), QKeySequence::SaveAs, this, [this] { save(true); });
    file->addSeparator();
    file->addAction(QStringLiteral("Импортировать изображение…"), this, [this] { chooseImage(); });
    file->addAction(previewAction_);
    file->addAction(findChild<QAction*>(QStringLiteral("printAction")));
    file->addAction(QStringLiteral("Экспортировать PNG…"), this, [this] { exportPng(); });
    file->addAction(QStringLiteral("Экспортировать шаблон ZPL…"), this, &MainWindow::exportZpl)
        ->setObjectName(QStringLiteral("exportTemplateZpl"));
    file->addAction(QStringLiteral("Экспортировать заполненный ZPL…"), this, &MainWindow::exportResolvedZpl)
        ->setObjectName(QStringLiteral("exportResolvedZpl"));
    file->addSeparator(); file->addAction(QStringLiteral("Выход"), QKeySequence::Quit, this, &QWidget::close);
    auto* edit = menuBar()->addMenu(QStringLiteral("Правка"));
    edit->addAction(undoAction_); edit->addAction(redoAction_); edit->addSeparator();
    const auto objectAction = [this, edit](const QString& text, const QString& name, const QKeySequence& key, auto callback) {
        auto* action = edit->addAction(text);
        action->setObjectName(name); action->setShortcut(key);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        canvas_->addAction(action); layers_->addAction(action);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    objectAction(QStringLiteral("Копировать объекты"), QStringLiteral("copyElements"), QKeySequence::Copy, [this] { copyElements(); });
    objectAction(QStringLiteral("Вырезать объекты"), QStringLiteral("cutElements"), QKeySequence::Cut, [this] { copyElements(true); });
    objectAction(QStringLiteral("Вставить объекты"), QStringLiteral("pasteElements"), QKeySequence::Paste, [this] { pasteElements(); });
    objectAction(QStringLiteral("Переименовать слой…"), QStringLiteral("renameLayerAction"), QKeySequence(Qt::Key_F2), [this] { renameLayer(); });
    edit->addSeparator();
    objectAction(QStringLiteral("Сгруппировать"), QStringLiteral("groupElements"), QKeySequence(QStringLiteral("Ctrl+G")), [this] { groupSelection(); });
    objectAction(QStringLiteral("Разгруппировать"), QStringLiteral("ungroupElements"), QKeySequence(QStringLiteral("Ctrl+Shift+G")), [this] { groupSelection(true); });
    edit->addSeparator();
    // Canvas shortcuts are scoped to the canvas, so typing in data/source fields
    // keeps the standard text-editor shortcuts.
    edit->addAction(QStringLiteral("Выделить все элементы"), this, [this] {
        if (!session_->document().visualEditable || session_->hasPendingSource()) return;
        QList<int> indices;
        for (int i = 0; i < session_->document().elements.size(); ++i)
            if (session_->document().elements[i].editorVisible) indices.append(i);
        canvas_->setSelectedIndices(indices);
    })->setObjectName(QStringLiteral("selectAllElements"));
    edit->addAction(QStringLiteral("Дублировать выбранные"), this, &MainWindow::duplicateElement)
        ->setObjectName(QStringLiteral("duplicateElements"));
    edit->addAction(QStringLiteral("Удалить выбранные"), this, &MainWindow::deleteElement)
        ->setObjectName(QStringLiteral("deleteElements"));
    auto* layout = menuBar()->addMenu(QStringLiteral("Компоновка"));
    layout->addAction(QStringLiteral("Преобразования этикетки…"), this, &MainWindow::editLabelSettings)
        ->setObjectName(QStringLiteral("labelSettings"));
    layout->addSeparator();
    auto* reference = layout->addAction(QStringLiteral("Выравнивать по этикетке"));
    reference->setObjectName(QStringLiteral("alignToLabel")); reference->setCheckable(true);
    reference->setToolTip(QStringLiteral("Без флажка — по рамке выбранной группы; одиночный элемент всегда по этикетке"));
    connect(reference, &QAction::toggled, this, [this](bool checked) { alignToLabel_ = checked; });
    layout->addSeparator();
    auto alignAction = [this, layout](const QString& title, const QString& name, ElementAlignment alignment) {
        auto* action = layout->addAction(title); action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, alignment] {
            if (session_->hasPendingSource() || !session_->document().visualEditable) return;
            if (previews_->pending()) refreshPreview();
            const auto indices = canvas_->selectedIndices();
            const auto group = indices.isEmpty() ? QUuid{} : session_->document().elements[indices.first()].groupId;
            const bool oneGroup = !group.isNull() && std::ranges::all_of(indices, [this, group](int index) {
                return session_->document().elements[index].groupId == group;
            });
            const auto reference = alignToLabel_ || indices.size() == 1 || oneGroup
                ? AlignmentReference::Label : AlignmentReference::Selection;
            session_->alignElements(indices, alignment, reference, previews_->editorPreview().bounds);
        });
    };
    alignAction(QStringLiteral("По левому краю"), QStringLiteral("alignLeft"), ElementAlignment::Left);
    alignAction(QStringLiteral("По центру горизонтально"), QStringLiteral("alignHorizontalCenter"), ElementAlignment::HorizontalCenter);
    alignAction(QStringLiteral("По правому краю"), QStringLiteral("alignRight"), ElementAlignment::Right);
    alignAction(QStringLiteral("По верхнему краю"), QStringLiteral("alignTop"), ElementAlignment::Top);
    alignAction(QStringLiteral("По центру вертикально"), QStringLiteral("alignVerticalCenter"), ElementAlignment::VerticalCenter);
    alignAction(QStringLiteral("По нижнему краю"), QStringLiteral("alignBottom"), ElementAlignment::Bottom);
    layout->addSeparator();
    for (auto axis : {Qt::Horizontal, Qt::Vertical}) {
        auto* action = layout->addAction(axis == Qt::Horizontal ? QStringLiteral("Равные промежутки по горизонтали") : QStringLiteral("Равные промежутки по вертикали"));
        action->setObjectName(axis == Qt::Horizontal ? QStringLiteral("distributeHorizontal") : QStringLiteral("distributeVertical"));
        connect(action, &QAction::triggered, this, [this, axis] {
            if (session_->hasPendingSource() || !session_->document().visualEditable) return;
            if (canvas_->selectedIndices().size() < 3) { setNotice(QStringLiteral("Для распределения выберите не менее трёх элементов.")); return; }
            if (previews_->pending()) refreshPreview();
            session_->distributeElements(canvas_->selectedIndices(), axis, previews_->editorPreview().bounds);
        });
    }
    auto* view = menuBar()->addMenu(QStringLiteral("Вид"));
    view->addAction(balanceAction_); view->addAction(focusAction_);
    view->addSeparator();
    view->addAction(layersAction_); view->addAction(propertiesAction_); view->addAction(diagnosticsAction_);
    view->addSeparator(); view->addAction(darkThemeAction_);
    auto* commandMenu = new QMenu(fileButton_);
    commandMenu->setObjectName(QStringLiteral("commandMenu"));
    commandMenu->addSection(QStringLiteral("Файл · все команды"));
    commandMenu->addActions(file->actions()); commandMenu->addSeparator(); commandMenu->addMenu(edit); commandMenu->addMenu(layout); commandMenu->addMenu(view);
    commandMenu->addAction(findChild<QAction*>(QStringLiteral("placeholderValuesAction")));
    fileButton_->setMenu(commandMenu); menuBar()->hide();
    auto* help = menuBar()->addMenu(QStringLiteral("Справка")); commandMenu->addMenu(help);
    help->addAction(QStringLiteral("О программе"), this, [this] {
        QMessageBox about(QMessageBox::NoIcon,
            QStringLiteral("QtZpl Designer %1").arg(QCoreApplication::applicationVersion()), QStringLiteral(
            "Версия %1\n\n"
            "Визуальный редактор этикеток на Qt Widgets.\n\n"
            "Перетащите элемент на лист; свойства справа.\n"
            "Ctrl+S — сохранить ZPL, Ctrl+Z — отменить.\n"
            "Delete — удалить, Ctrl+D — дубликат.\n"
            "Ctrl+колесо — масштаб, средняя кнопка — панорама.\n\n"
            "Превью рисует локальная библиотека QtZpl.\n"
            "Сложные ZPL доступны в режиме кода с сохранением исходника.\n"
            "Одна визуально редактируемая этикетка. Печать ZPL или растра через Windows и по IP.")
            .arg(QCoreApplication::applicationVersion()), QMessageBox::Close, this);
        about.setObjectName(QStringLiteral("aboutDialog"));
        about.setTextFormat(Qt::PlainText);
        about.button(QMessageBox::Close)->setText(QStringLiteral("Закрыть"));
        if (!windowIcon().isNull()) about.setIconPixmap(windowIcon().pixmap(64, 64));
        about.exec();
    })->setObjectName(QStringLiteral("aboutApplication"));
    buildCommandPanels(view);
}

void MainWindow::buildCommandPanels(QMenu* viewMenu) {
    struct Command { const char* name; CommandIcon icon; const char* shortcut = nullptr; };
    using Group = QList<Command>;
    const auto configure = [this, viewMenu](QToolBar* panel, const QString& menuName,
                                          const QString& title, const QList<Group>& groups) {
        panel->setProperty("commandPanel", true);
        panel->setMovable(false);
        panel->setFloatable(false);
        panel->setIconSize(QSize(16, 16));
        panel->setToolButtonStyle(Qt::ToolButtonIconOnly);
        panel->setFixedHeight(30);
        panel->setMinimumWidth(0);
        panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        panel->setAccessibleName(title);
        auto* menu = new QMenu(title, this);
        menu->setObjectName(menuName);
        viewMenu->addMenu(menu);
        QList<QList<QAction*>> visibleGroups;
        QList<QAction*> separators;
        QList<QAction*> switches;
        for (const auto& group : groups) {
            if (!visibleGroups.isEmpty()) {
                separators.append(panel->addSeparator());
                menu->addSeparator();
            }
            QList<QAction*> proxies;
            for (const auto& command : group) {
                const auto name = QString::fromLatin1(command.name);
                auto* source = findChild<QAction*>(name);
                Q_ASSERT(source);
                source->setIcon(commandIcon(command.icon));
                const QString shortcut = command.shortcut ? QString::fromLatin1(command.shortcut)
                    : source->shortcut().toString(QKeySequence::NativeText);
                const auto tooltip = source->text() + (shortcut.isEmpty() ? QString{} : QStringLiteral(" · ") + shortcut);
                source->setToolTip(tooltip);
                // Independent presentation actions keep hidden commands in their
                // menus and preserve the original shortcuts and focus context.
                auto* proxy = new QAction(panel);
                proxy->setObjectName(name + QStringLiteral("Toolbar"));
                const auto sync = [source, proxy] {
                    proxy->setText(source->text());
                    proxy->setIcon(source->icon());
                    proxy->setToolTip(source->toolTip());
                    proxy->setEnabled(source->isEnabled());
                    proxy->setCheckable(source->isCheckable());
                    proxy->setChecked(source->isChecked());
                };
                sync();
                connect(source, &QAction::changed, proxy, sync);
                connect(proxy, &QAction::triggered, source, &QAction::trigger);
                panel->addAction(proxy);
                auto* button = qobject_cast<QToolButton*>(panel->widgetForAction(proxy));
                Q_ASSERT(button);
                button->setObjectName(name + QStringLiteral("ToolButton"));
                button->setProperty("commandButton", true);
                button->setProperty("compact", true);
                button->setProperty("flat", true);
                if (command.icon == CommandIcon::Save) button->setProperty("designerControl", "accent");
                button->setToolButtonStyle(Qt::ToolButtonIconOnly);
                button->setFixedSize(26, 28);
                button->setFocusPolicy(Qt::NoFocus);
                button->setAccessibleName(source->text());
                const bool visible = !workspacePreferences_.hiddenToolbarCommands.contains(name);
                proxy->setVisible(visible);
                auto* toggle = menu->addAction(source->text());
                toggle->setObjectName(QStringLiteral("visibility_") + name);
                toggle->setCheckable(true);
                toggle->setChecked(visible);
                connect(toggle, &QAction::toggled, this, [this, proxy, name](bool show) {
                    proxy->setVisible(show);
                    workspacePreferences_.hiddenToolbarCommands.removeAll(name);
                    if (!show) workspacePreferences_.hiddenToolbarCommands.append(name);
                    persistWorkspace();
                });
                proxies.append(proxy);
                switches.append(toggle);
            }
            visibleGroups.append(proxies);
        }
        const auto updateSeparators = [visibleGroups, separators] {
            bool preceding = false;
            for (qsizetype i = 0; i < visibleGroups.size(); ++i) {
                const bool visible = std::ranges::any_of(visibleGroups[i], [](const QAction* action) { return action->isVisible(); });
                if (i > 0) separators[i - 1]->setVisible(preceding && visible);
                preceding |= visible;
            }
        };
        for (auto* toggle : switches) connect(toggle, &QAction::toggled, panel, updateSeparators);
        updateSeparators();
        const auto contextMenu = [menu](QWidget* widget) {
            widget->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(widget, &QWidget::customContextMenuRequested, menu, [widget, menu](const QPoint& point) {
                menu->popup(widget->mapToGlobal(point));
            });
        };
        contextMenu(panel);
        contextMenu(panel->parentWidget());
        for (auto* button : panel->findChildren<QToolButton*>()) contextMenu(button);
    };
    configure(documentCommandPanel_, QStringLiteral("documentCommandVisibilityMenu"), QStringLiteral("Панель 1 · файл и история"), {
        {{"saveAction", CommandIcon::Save}},
        {{"undoAction", CommandIcon::Undo}, {"redoAction", CommandIcon::Redo}},
        {{"printAction", CommandIcon::Print}}
    });
    configure(editCommandPanel_, QStringLiteral("editCommandVisibilityMenu"), QStringLiteral("Панель 2 · объекты"), {
        {{"alignLeft", CommandIcon::AlignLeft}, {"alignHorizontalCenter", CommandIcon::AlignHCenter},
         {"alignRight", CommandIcon::AlignRight}, {"alignTop", CommandIcon::AlignTop},
         {"alignVerticalCenter", CommandIcon::AlignVCenter}, {"alignBottom", CommandIcon::AlignBottom},
         {"distributeHorizontal", CommandIcon::DistributeHorizontal}, {"distributeVertical", CommandIcon::DistributeVertical}},
        {{"groupElements", CommandIcon::Group}, {"ungroupElements", CommandIcon::Ungroup}},
        {{"cutElements", CommandIcon::Cut}, {"copyElements", CommandIcon::Copy}, {"pasteElements", CommandIcon::Paste},
         {"duplicateElements", CommandIcon::Duplicate, "Ctrl+D"}, {"deleteElements", CommandIcon::Delete, "Delete"}}
    });
}

const DesignDocument& MainWindow::document() const { return session_->document(); }

void MainWindow::setDocument(const DesignDocument& document) {
    sourceAdapter_->cancelSelection();
    canvas_->setSelectedIndex(-1);
    selection_->setIds({}, document);
    selected_ = -1; session_->reset(document); refreshPreview();
}

void MainWindow::restoreDocument(const DesignDocument& document) {
    sourceAdapter_->cancelApply();
    const QSignalBlocker canvasSignals(canvas_);
    selection_->reconcile(document);
    canvas_->syncDocument(document);
    canvas_->setSelectedIndices(selection_->indices(document));
    selected_ = canvas_->selectedIndex();
    updating_ = true;
    labelWidth_->setValue(session_->document().width); labelHeight_->setValue(session_->document().height);
    refreshPropertyUnits(); // Millimetre text depends on the document DPI.
    const int dpiIndex = dpi_->findData(session_->document().dpi); dpi_->setCurrentIndex(dpiIndex < 0 ? 0 : dpiIndex);
    canvas_->setEnabled(!session_->hasPendingSource());
    labelWidth_->setEnabled(document.visualEditable && !session_->hasPendingSource()); labelHeight_->setEnabled(document.visualEditable && !session_->hasPendingSource()); dpi_->setEnabled(document.visualEditable && !session_->hasPendingSource());
    dimensions_->setText(QStringLiteral("%1 × %2 dots · %3 dpi").arg(session_->document().width).arg(session_->document().height).arg(session_->document().dpi));
    sourceAdapter_->syncDocument();
    updating_ = false;
    previews_->setSnapshot(document, session_->revision());
    refreshLayers(); refreshProperties(); updateTitle(); updateHistoryActions();
    sourceAdapter_->resumeSelection();
    syncDesignSelection();
    if (session_->hasPendingSource()) {
        const bool live = workspacePreferences_.editorLayout != EditorLayout::Single;
        sourceStatus_->setText(live ? QStringLiteral("Обновление после ввода…")
            : QStringLiteral("Восстановлен изменённый ZPL. Перейдите в «Дизайн» или «Превью», чтобы применить его."));
        if (live) sourceAdapter_->scheduleApply();
    } else updateEditorPresentation();
}

void MainWindow::executeOperation(const EditOperation& operation, const QString& title, int selection) {
    const auto result = session_->execute(operation, title);
    if (!result) { setNotice(result.error()); refreshProperties(); return; }
    if (selection != -2) selectElement(selection);
}

void MainWindow::refreshPreview() {
    previews_->setSnapshot(session_->document(), session_->revision());
    if (previews_->pending()) previews_->refresh();
    else presentPreview();
}

void MainWindow::presentPreview() {
    canvas_->setDocument(session_->document(), previews_->editorPreview());
    refreshProperties();
    QStringList notes = session_->document().importNotes; notes.append(previews_->editorPreview().diagnostics);
    if (!session_->layoutWarning().isEmpty()) notes.append(session_->layoutWarning());
    notes.removeDuplicates();
    diagnostics_->setPlainText(notes.isEmpty() ? QStringLiteral("Ошибок рендеринга нет.\nПревью построено локально библиотекой QtZpl.") : notes.join(QStringLiteral("\n\n")));
    if (session_->hasPendingSource()) return;
    refreshCleanPreview();
    if (!previews_->editorPreview().elementErrors.isEmpty()) setNotice(QStringLiteral(
        "Заполните «Значения плейсхолдеров…» или исправьте данные отмеченных штрихкодов. Печатное превью и PNG пока недоступны."));
    else if (!previews_->printError().isEmpty()) setNotice(QStringLiteral("Не удалось построить превью: %1").arg(previews_->printError()));
    else if (!session_->document().visualEditable) setNotice(QStringLiteral("Режим исходного ZPL: команды сохранены. Для правок откройте вкладку «Код ZPL»."));
    else if (!notes.isEmpty()) setNotice(QStringLiteral("Превью содержит замечания (%1). Откройте панель «Проверка».").arg(notes.size()));
    else setNotice(QStringLiteral("✓ Превью без ошибок      %1 объектов      Единицы: %2")
        .arg(session_->document().elements.size()).arg(session_->layoutSettings().unit == CanvasUnit::Dots ? QStringLiteral("dots") : QStringLiteral("мм")));
}

void MainWindow::showPreview() {
    if (editorTabs_->currentWidget() != previewWidget_) {
        editorTabs_->setCurrentWidget(previewWidget_);
        return;
    }
    const bool pendingSource = session_->hasPendingSource();
    if (!applySource()) {
        if (workspacePreferences_.editorLayout == EditorLayout::Single) editorTabs_->setCurrentIndex(1);
        else previewWidget_->setPreview({}, session_->document().dpi, sourceStatus_->text());
        return;
    }
    if (!pendingSource) refreshPreview();
    previewWidget_->setFocus();
}

void MainWindow::refreshCleanPreview() {
    if (session_->hasPendingSource()) return;
    previewWidget_->setPages(previews_->pageCount(), previews_->page());
    previewWidget_->setPreview(previews_->pagePreview(), previews_->dpi(), previews_->pageError());
    refreshSourceDiagnostics();
}

void MainWindow::refreshSourceDiagnostics() {
    sourceDiagnostics_->clear();
    auto document = session_->document();
    const bool separateSource = session_->hasPendingSource()
        || (!code_->toPlainText().isEmpty() && code_->toPlainText() != toZpl(document));
    if (separateSource) {
        document.originalZpl = code_->toPlainText();
        document.visualEditable = false;
    }
    sourceDiagnostics_->setDiagnostics(separateSource ? previews_->sourceDiagnostics(document)
                                                    : previews_->sourceDiagnostics());
}

QSize MainWindow::elementSize(int index) const {
    if (index < 0 || index >= session_->document().elements.size()) return {1, 1};
    const auto& element = session_->document().elements[index];
    const bool renderedSize = element.type == ElementType::Text || isBarcodeType(element.type);
    if (renderedSize && index < previews_->editorPreview().bounds.size() && !previews_->editorPreview().bounds[index].isEmpty())
        return previews_->editorPreview().bounds[index].size();
    return {element.width, element.type == ElementType::Circle ? element.width : element.height};
}

void MainWindow::resizeElement(int index, QSize size) {
    if (index < 0 || index >= session_->document().elements.size()) return;
    if (previews_->pending()) refreshPreview();
    const auto result = session_->execute(ResizeElement{session_->document().elements[index].id, size,
        elementSize(index), previews_->requestIdentity(PreviewMode::Editor).revision,
        previews_->editorPreview().elementErrors.value(index)}, QStringLiteral("Размер элемента"));
    if (!result) setNotice(result.error());
    if (result && *result) refreshPreview();
    else refreshProperties();
}

void MainWindow::selectElement(int index) {
    canvas_->setSelectedIndex(index);
    syncSelection();
}

void MainWindow::syncSelection() {
    selection_->setIndices(canvas_->selectedIndices(), session_->document());
    selected_ = canvas_->selectedIndex();
    const QSignalBlocker blocker(layers_);
    const auto indices = canvas_->selectedIndices();
    layers_->setFixedHeight(std::min(370, int(session_->document().elements.size()) * 37 + 8));
    layerCount_->setText(QStringLiteral("%1 объектов").arg(session_->document().elements.size()));
    layers_->setSelectedIds(selection_->ids());
    refreshProperties();
    syncDesignSelection();
}

void MainWindow::syncDesignSelection(bool reveal) {
    sourceAdapter_->setSelectionSyncEnabled(workspacePreferences_.selectionSync);
    sourceAdapter_->setSelection(selection_->indices(session_->document()), reveal);
}

void MainWindow::syncSourceSelection(bool allowCaret) {
    if (!updating_) sourceAdapter_->sourceSelection(workspacePreferences_.selectionSync, allowCaret);
}

void MainWindow::refreshLayers() {
    layers_->refresh(session_->document(), selection_->ids());
    layerCount_->setText(QStringLiteral("%1 объектов").arg(session_->document().elements.size()));
}

void MainWindow::refreshProperties() {
    updating_ = true;
    refreshFocusSummary();
    const auto indices = canvas_->selectedIndices();
    for (auto* check : {layerLocked_, layerVisible_, layerPrintable_}) {
        check->setEnabled(!indices.isEmpty() && session_->document().visualEditable && !session_->hasPendingSource());
    }
    const auto setFlagState = [this, &indices](QCheckBox* check, bool Element::* member) {
        const QSignalBlocker block(check);
        if (indices.isEmpty()) { check->setCheckState(Qt::Unchecked); return; }
        const bool first = session_->document().elements[indices.first()].*member;
        const bool mixed = std::ranges::any_of(indices, [this, member, first](int i) { return session_->document().elements[i].*member != first; });
        check->setTristate(mixed);
        check->setCheckState(mixed ? Qt::PartiallyChecked : first ? Qt::Checked : Qt::Unchecked);
    };
    setFlagState(layerLocked_, &Element::locked); setFlagState(layerVisible_, &Element::editorVisible);
    setFlagState(layerPrintable_, &Element::printable);
    propertiesPanel_->refresh();
    updating_ = false;
}

void MainWindow::copyElements(bool cut) {
    if (session_->hasPendingSource() || !session_->document().visualEditable) return;
    auto indices = canvas_->selectedIndices();
    std::ranges::sort(indices);
    QList<Element> elements;
    for (const int index : indices) elements.append(session_->document().elements[index]);
    if (elements.isEmpty()) return;
    const auto clipboardData = encodeElements(elements);
    if (clipboardData.isEmpty()) { setNotice(QStringLiteral("Не удалось скопировать объекты: предел буфера — 32 МиБ и 32 млн пикселей изображений.")); return; }
    auto* mime = new QMimeData;
    mime->setData(QLatin1StringView(elementMimeType), clipboardData);
    auto copy = session_->document();
    copy.elements = elements; copy.originalZpl.clear(); copy.sourcePreamble.clear(); copy.sourceEpilogue.clear();
    if (validateForExport(copy)) mime->setText(toZpl(copy));
    QApplication::clipboard()->setMimeData(mime);
    if (cut) deleteElement();
    else setNotice(QStringLiteral("Скопировано объектов: %1. Их можно вставить в другую этикетку.").arg(elements.size()));
}

void MainWindow::pasteElements() {
    if (session_->hasPendingSource() || !session_->document().visualEditable) return;
    const auto* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QLatin1StringView(elementMimeType))) {
        setNotice(QStringLiteral("В буфере нет объектов QtZpl Designer.")); return;
    }
    const auto elements = decodeElements(mime->data(QLatin1StringView(elementMimeType)));
    if (!elements) { setNotice(elements.error()); return; }
    const auto indices = session_->pasteElements(*elements);
    if (indices.isEmpty()) { setNotice(QStringLiteral("Вставка недоступна: максимум 200 объектов.")); return; }
    editorTabs_->setCurrentIndex(0);
    canvas_->setSelectedIndices(indices);
    canvas_->setFocus();
}

void MainWindow::groupSelection(bool ungroup) {
    if (session_->hasPendingSource() || !session_->document().visualEditable) return;
    const auto indices = canvas_->selectedIndices();
    const bool changed = ungroup ? session_->ungroupElements(indices) : session_->groupElements(indices);
    if (changed) canvas_->setSelectedIndices(indices);
    else setNotice(ungroup ? QStringLiteral("Выберите группу для разгруппировки.")
                          : QStringLiteral("Для группы выберите минимум два объекта."));
}

void MainWindow::editImage() {
    if (session_->hasPendingSource() || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (element.type != ElementType::Image) return;
    ImageEditDialog dialog(element, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto edited = dialog.editedElement();
    if (edited.image == element.image && edited.width == element.width && edited.height == element.height) return;
    executeOperation(UpdateElementContent{element.id, std::move(edited)}, QStringLiteral("Изменить изображение"));
}

void MainWindow::editTable() {
    if (session_->hasPendingSource() || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (element.type != ElementType::Table) return;
    TableDialog dialog(element, session_->layoutSettings().unit, session_->document().dpi, this);
    if (dialog.exec() != QDialog::Accepted) return;
    executeOperation(UpdateElementContent{element.id, dialog.editedElement()}, QStringLiteral("Изменить таблицу"));
}

void MainWindow::editQrPayload() {
    if (session_->hasPendingSource() || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (element.type != ElementType::QrCode) return;
    QrPayloadDialog dialog(element.text, this);
    if (dialog.exec() != QDialog::Accepted || dialog.payload() == element.text) return;
    executeOperation(UpdateText{element.id, dialog.payload()}, QStringLiteral("Содержимое QR"));
}

void MainWindow::editBarcodeOptions() {
    if (session_->hasPendingSource() || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (!isBarcodeType(element.type)) return;
    BarcodeOptionsDialog dialog(element, session_->layoutSettings().unit, session_->document().dpi, this);
    if (dialog.exec() != QDialog::Accepted) return;
    executeOperation(UpdateElementContent{element.id, dialog.editedElement()}, QStringLiteral("Параметры штрихкода"));
}

void MainWindow::editAdvancedText() {
    if (session_->hasPendingSource() || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (element.type != ElementType::Text) return;
    AdvancedTextDialog dialog(element, session_->layoutSettings().unit, session_->document().dpi, this);
    if (dialog.exec() != QDialog::Accepted) return;
    executeOperation(UpdateElementContent{element.id, dialog.editedElement()}, QStringLiteral("Положение текста"));
}

void MainWindow::editLabelSettings() {
    if (!applySource() || !session_->document().visualEditable) return;
    LabelSettingsDialog dialog(session_->document(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    executeOperation(labelSettings(dialog.editedDocument()), QStringLiteral("Преобразования этикетки"));
}

void MainWindow::refreshPropertyUnits() {
    propertiesPanel_->refreshUnits();
    for (auto* field : {labelWidth_, labelHeight_})
        field->setDisplayUnits(session_->layoutSettings().unit, session_->document().dpi);
}

bool MainWindow::selectionLocked() const {
    return std::ranges::any_of(canvas_->selectedIndices(), [this](int index) { return session_->document().elements[index].locked; });
}

void MainWindow::renameLayer() {
    if (session_->hasPendingSource() || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    bool accepted = false;
    const auto& element = session_->document().elements[selected_];
    const auto name = QInputDialog::getText(this, QStringLiteral("Название слоя"), QStringLiteral("Название:"),
        QLineEdit::Normal, element.name.isEmpty() ? elementTypeName(element.type) : element.name, &accepted).trimmed();
    if (!accepted || name == element.name) return;
    executeOperation(RenameElement{element.id, name}, QStringLiteral("Название слоя"));
}

void MainWindow::setLayerFlag(LayerFlag flag, bool value) {
    if (updating_) return;
    executeOperation(SetElementFlags{selection_->ids(), flag, value}, QStringLiteral("Параметры слоёв"));
}

void MainWindow::addElement(ElementType type, QPoint position) {
    if (!session_->document().visualEditable) { setNotice(QStringLiteral("Этот ZPL редактируется во вкладке кода. Создайте новую этикетку для визуального дизайна.")); return; }
    if (type == ElementType::Image) { chooseImage(position); return; }
    auto element = makeElement(type, position);
    if (type == ElementType::Eac)
        element.width = element.height = 2 * eacMinimumSize(session_->document().dpi, element.eacStyle);
    executeOperation(AddElement{std::move(element)}, QStringLiteral("Добавить элемент"),
        static_cast<int>(session_->document().elements.size()));
}

bool MainWindow::importImage(const QString& path, QPoint position) {
    if (!session_->document().visualEditable) { setNotice(QStringLiteral("Импорт изображения доступен в визуальном режиме.")); return false; }
    auto image = loadImage(path);
    if (!image) { setNotice(image.error()); return false; }
    auto element = makeElement(ElementType::Image, position);
    element.name = QFileInfo(path).fileName(); element.image = *image;
    const QSize size = image->size().scaled(QSize(std::max(1, session_->document().width - position.x()), std::max(1, session_->document().height - position.y())), Qt::KeepAspectRatio);
    element.width = std::min(image->width(), size.width()); element.height = std::min(image->height(), size.height());
    const int index = static_cast<int>(session_->document().elements.size());
    const auto added = session_->execute(AddElement{std::move(element)}, QStringLiteral("Импортировать изображение"));
    if (!added) { setNotice(added.error()); return false; }
    selectElement(index);
    return *added;
}

void MainWindow::chooseImage(QPoint position) {
    if (!session_->document().visualEditable) { setNotice(QStringLiteral("Создайте визуальную этикетку для импорта изображения.")); return; }
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Импортировать картинку"), {}, QStringLiteral("Изображения (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;Все файлы (*)"));
    if (!path.isEmpty()) importImage(path, position);
}

void MainWindow::duplicateElement() {
    if (session_->hasPendingSource() || !session_->document().visualEditable || selected_ < 0) return;
    const auto indices = canvas_->selectedIndices();
    if (session_->document().elements.size() + indices.size() > 200) { setNotice(QStringLiteral("После дублирования будет превышен предел 200 элементов.")); return; }
    const auto copies = session_->duplicateElements(indices);
    if (!copies.isEmpty()) canvas_->setSelectedIndices(copies);
}
void MainWindow::deleteElement() {
    if (!session_->hasPendingSource()) session_->deleteElements(canvas_->selectedIndices());
}
void MainWindow::moveLayer(int direction) {
    executeOperation(ReorderElements{selection_->ids(), direction}, QStringLiteral("Порядок слоёв"));
}

void MainWindow::showGs1Builder() {
    if (session_->hasPendingSource() || selectionLocked() || !session_->document().visualEditable || selected_ < 0
        || canvas_->selectedIndices().size() != 1) return;
    if (session_->document().elements[selected_].type != ElementType::DataMatrix) return;
    Gs1Dialog dialog(session_->document().elements[selected_].text, this);
    if (dialog.exec() != QDialog::Accepted) return;
    executeOperation(UpdateText{session_->document().elements[selected_].id, dialog.data(), true, true},
        QStringLiteral("Настроить GS1 DataMatrix"));
}

bool MainWindow::showPlaceholderValues() {
    if (session_->hasPendingSource() || !session_->document().visualEditable) return false;
    const auto names = dataMatrixParameters(session_->document());
    if (!names) { setNotice(names.error()); return false; }
    if (names->isEmpty()) {
        setNotice(QStringLiteral("Укажите плейсхолдеры в тексте или штрихкоде, например {{batch}} или (01){{01}}(21){{21}}."));
        return false;
    }
    PlaceholderValuesDialog dialog(session_->document(), this);
    if (dialog.exec() != QDialog::Accepted) return false;
    session_->setPreviewValues(dialog.values());
    refreshPreview();
    return true;
}

void MainWindow::exportResolvedZpl() {
    if (!applySource()) return;
    if (!session_->document().visualEditable) { setNotice(QStringLiteral("Заполненный ZPL доступен для визуальных шаблонов.")); return; }
    if (const auto resolved = resolveDataMatrixValues(session_->document()); !resolved) {
        if (!showPlaceholderValues()) return;
    }
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Экспортировать заполненный ZPL"),
        QStringLiteral("label-filled.zpl"), QStringLiteral("ZPL (*.zpl)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".zpl");
    const auto exported = session_->saveResolved(path);
    setNotice(exported ? QStringLiteral("Заполненный ZPL сохранён. Исходный шаблон остаётся открытым.") : exported.error());
}

void MainWindow::exportZpl() {
    if (!applySource()) return;
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Экспортировать шаблон ZPL"),
        QStringLiteral("label.zpl"), QStringLiteral("ZPL (*.zpl)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".zpl");
    const auto result = session_->exportZpl(path);
    setNotice(result ? QStringLiteral("Шаблон ZPL экспортирован. Проект остаётся открытым.") : result.error());
}

void MainWindow::showRecoveryDialog(bool startup) {
    const auto entries = DocumentSession::availableRecoveries();
    if (entries.isEmpty()) { if (!startup) setNotice(QStringLiteral("Нет черновиков для восстановления.")); return; }
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Восстановление черновиков"));
    dialog.setObjectName(QStringLiteral("recoveryDialog")); dialog.resize(680, 400);
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(QStringLiteral("Найдены изменения, оставшиеся после незавершённой работы. Восстановление откроет копию; сохранённый файл не перезаписывается."), &dialog);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* list = new QListWidget(&dialog); list->setObjectName(QStringLiteral("recoveryList")); layout->addWidget(list);
    for (const auto& entry : entries) {
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2%3")
            .arg(entry.originalPath.isEmpty() ? QStringLiteral("Новая этикетка") : entry.originalPath,
                entry.modifiedAt.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")),
                entry.error.isEmpty() ? QString{} : QStringLiteral("\n") + entry.error), list);
        item->setData(Qt::UserRole, entry.path);
        item->setData(Qt::UserRole + 1, entry.error.isEmpty());
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* recover = buttons->addButton(QStringLiteral("Восстановить"), QDialogButtonBox::AcceptRole);
    auto* remove = buttons->addButton(QStringLiteral("Удалить черновик"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    const auto update = [list, recover, remove] {
        recover->setEnabled(list->currentItem() && list->currentItem()->data(Qt::UserRole + 1).toBool());
        remove->setEnabled(list->currentItem());
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, update);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(remove, &QPushButton::clicked, &dialog, [this, &dialog, list, update] {
        if (!list->currentItem()) return;
        if (QMessageBox::question(&dialog, QStringLiteral("Удалить черновик?"),
            QStringLiteral("Удалить выбранную восстановительную копию? Основной файл останется без изменений.")) != QMessageBox::Yes) return;
        const auto result = DocumentSession::discardRecovery(list->currentItem()->data(Qt::UserRole).toString());
        if (!result) { QMessageBox::warning(&dialog, QStringLiteral("Черновик не удалён"), result.error()); return; }
        delete list->takeItem(list->currentRow()); update();
    });
    list->setCurrentRow(0); update();
    if (dialog.exec() != QDialog::Accepted || !list->currentItem() || !maybeSave()) return;
    const auto result = session_->recover(list->currentItem()->data(Qt::UserRole).toString());
    if (!result) { setNotice(result.error()); return; }
    restoreDocument(session_->document()); selectElement(-1); refreshCanvasLayout(); refreshPreview(); restoreCanvasZoom();
    if (session_->hasPendingSource()) editorTabs_->setCurrentIndex(1);
    setNotice(QStringLiteral("Черновик восстановлен. Сохраните проект, чтобы сохранить изменения."));
}

void MainWindow::showPrintDialog() {
    if (!applySource()) return;
    if (session_->document().visualEditable) {
        if (const auto resolved = resolveDataMatrixValues(session_->document()); !resolved)
            if (!showPlaceholderValues()) return;
    }
    PrintDialog dialog(session_->document(), this);
    dialog.exec();
}

void MainWindow::showLabelSetup() {
    if (session_->hasPendingSource() || !session_->document().visualEditable) {
        setNotice(QStringLiteral("Настройка формата доступна после применения ZPL в визуальном режиме.")); return;
    }
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Формат этикетки"));
    dialog.setObjectName(QStringLiteral("labelSetupDialog")); dialog.resize(440, 320);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* preset = new QComboBox(&dialog); preset->setObjectName(QStringLiteral("labelPreset"));
    preset->addItem(QStringLiteral("Свой размер"), QSizeF{});
    for (const auto size : {QSizeF(20, 20), QSizeF(30, 20), QSizeF(40, 30), QSizeF(58, 40),
                            QSizeF(58, 60), QSizeF(75, 120), QSizeF(100, 100), QSizeF(100, 150), QSizeF(101.6, 152.4)}) {
        preset->addItem(QStringLiteral("%1 × %2 мм").arg(size.width()).arg(size.height()), size);
    }
    form->addRow(QStringLiteral("Формат"), preset);
    auto dimension = [&dialog] (const QString& name, double value) {
        auto* spin = new QDoubleSpinBox(&dialog); spin->setObjectName(name); spin->setDecimals(3);
        spin->setRange(0.01, 600); spin->setValue(value); spin->setSuffix(QStringLiteral(" мм")); spin->setKeyboardTracking(false);
        return spin;
    };
    auto* width = dimension(QStringLiteral("labelWidthMm"), session_->document().width * 25.4 / session_->document().dpi);
    auto* height = dimension(QStringLiteral("labelHeightMm"), session_->document().height * 25.4 / session_->document().dpi);
    auto* resolution = new QComboBox(&dialog); resolution->setObjectName(QStringLiteral("labelSetupDpi"));
    for (int dpi : {203, 300, 600}) resolution->addItem(QStringLiteral("%1 dpi").arg(dpi), dpi);
    resolution->setCurrentIndex(resolution->findData(session_->document().dpi));
    form->addRow(QStringLiteral("Ширина"), width); form->addRow(QStringLiteral("Высота"), height);
    form->addRow(QStringLiteral("Принтер"), resolution); layout->addLayout(form);
    auto* rotate = new QPushButton(QStringLiteral("Поменять ширину и высоту"), &dialog);
    rotate->setObjectName(QStringLiteral("swapLabelDimensions")); layout->addWidget(rotate);
    auto* result = new QLabel(&dialog); result->setObjectName(QStringLiteral("labelSetupResult")); result->setWordWrap(true); layout->addWidget(result);
    auto* hint = new QLabel(QStringLiteral("Размер округляется до целых точек принтера. Положение и размеры элементов остаются в dots. DPI сохраняется вместе с настройками холста; для ZPL без настроек используется 203 dpi."), &dialog);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Применить"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto update = [=] {
        const int dpi = resolution->currentData().toInt();
        const int w = qRound(width->value() * dpi / 25.4), h = qRound(height->value() * dpi / 25.4);
        const bool valid = w >= 32 && h >= 32 && w <= 4096 && h <= 4096;
        result->setText(valid ? QStringLiteral("%1 × %2 dots · фактически %3 × %4 мм").arg(w).arg(h)
            .arg(w * 25.4 / dpi, 0, 'f', 3).arg(h * 25.4 / dpi, 0, 'f', 3)
            : QStringLiteral("Размер должен быть от 32 до 4096 dots по каждой стороне при выбранном DPI."));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    };
    connect(preset, &QComboBox::activated, &dialog, [=](int index) {
        if (index == 0) return;
        const auto size = preset->itemData(index).toSizeF();
        const QSignalBlocker bw(width), bh(height);
        width->setValue(size.width()); height->setValue(size.height()); update();
    });
    auto dimensionChanged = [=] { preset->setCurrentIndex(0); update(); };
    connect(width, &QDoubleSpinBox::valueChanged, &dialog, dimensionChanged);
    connect(height, &QDoubleSpinBox::valueChanged, &dialog, dimensionChanged);
    connect(resolution, &QComboBox::currentIndexChanged, &dialog, update);
    connect(rotate, &QPushButton::clicked, &dialog, [=] {
        const double w = width->value(); width->setValue(height->value()); height->setValue(w);
    });
    update();
    if (dialog.exec() != QDialog::Accepted) return;
    auto next = labelSettings(session_->document()); next.dpi = resolution->currentData().toInt();
    next.width = qRound(width->value() * next.dpi / 25.4); next.height = qRound(height->value() * next.dpi / 25.4);
    if (next.width == session_->document().width && next.height == session_->document().height && next.dpi == session_->document().dpi) return;
    executeOperation(next, QStringLiteral("Формат этикетки в миллиметрах"));
    restoreCanvasZoom();
}

void MainWindow::refreshCanvasLayout() {
    const auto& settings = session_->layoutSettings();
    const QSignalBlocker gridSignals(grid_), snapSignals(snap_);
    grid_->setChecked(settings.gridVisible);
    snap_->setChecked(settings.snapEnabled);
    const QString step = settings.unit == CanvasUnit::Millimeters
        ? QStringLiteral("%1 мм").arg(locale().toString(settings.gridStep * 25.4 / std::max(1, session_->document().dpi), 'f', 2))
        : QStringLiteral("%1 dots").arg(settings.gridStep);
    snap_->setToolTip(QStringLiteral("Шаг сетки: %1. Привязка к направляющим, отступу, краям и центрам соседних объектов. Alt — временно отключить.").arg(step));
    canvas_->setLayoutSettings(settings);
    refreshPropertyUnits();
    refreshFocusSummary();
    updateTitle(); updateHistoryActions();
}

void MainWindow::showCanvasLayout() {
    if (!canvasPanel_) {
        canvasPanel_ = new CanvasQuickPanel(session_, this);
        // Selection labels are workspace preferences: persisted per user, outside label history.
        connect(canvasPanel_, &CanvasQuickPanel::geometryLabelsChanged, this, [this](bool sizes, bool coordinates) {
            if (workspacePreferences_.geometrySizes == sizes && workspacePreferences_.geometryCoordinates == coordinates) return;
            workspacePreferences_.geometrySizes = sizes;
            workspacePreferences_.geometryCoordinates = coordinates;
            canvas_->setGeometryLabelsVisible(sizes, coordinates);
            persistWorkspace();
        });
        connect(canvasPanel_, &CanvasQuickPanel::rulersDockedChanged, this, [this](bool docked) {
            if (workspacePreferences_.rulersDocked == docked) return;
            workspacePreferences_.rulersDocked = docked;
            canvas_->setRulersDocked(docked);
            persistWorkspace();
        });
        connect(canvasPanel_, &CanvasQuickPanel::closed, this, [this] { canvasSetup_->setChecked(false); });
        connect(canvasPanel_, &CanvasQuickPanel::guideListRequested, this, &MainWindow::showGuideList, Qt::QueuedConnection);
    }
    if (canvasPanel_->isVisible()) { canvasPanel_->close(); return; }
    canvasPanel_->setGeometryLabelsVisible(workspacePreferences_.geometrySizes, workspacePreferences_.geometryCoordinates);
    canvasPanel_->setRulersDocked(workspacePreferences_.rulersDocked);
    canvasSetup_->setChecked(true);
    canvasPanel_->popup(canvasSetup_);
}

void MainWindow::showGuideList() {
    GuideListDialog dialog(session_->layoutSettings(),
        {session_->document().width, session_->document().height}, session_->document().dpi, this);
    connect(&dialog, &GuideListDialog::currentGuideChanged, canvas_, &LabelCanvas::setSelectedGuide);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    canvas_->setSelectedGuide(std::nullopt);
    if (accepted) session_->setLayoutSettings(dialog.settings(), QStringLiteral("Направляющие"));
}

bool MainWindow::applySource() {
    sourceAdapter_->cancelApply();
    if (!session_->hasPendingSource()) return true;
    const auto applied = sourceAdapter_->apply();
    if (!applied) {
        sourceStatus_->setText(QStringLiteral("ZPL не применён: %1 Последний применённый макет сохранён.").arg(applied.error()));
        setNotice(sourceStatus_->text());
        refreshSourceDiagnostics();
        return false;
    }
    refreshPreview();
    return true;
}

bool MainWindow::openFile(const QString& path) {
    auto result = session_->open(path);
    if (!result) { setNotice(result.error()); return false; }
    selectElement(-1); refreshPreview(); restoreCanvasZoom(); updateTitle();
    if (session_->hasPendingSource()) { restoreDocument(session_->document()); editorTabs_->setCurrentIndex(1); }
    if (!session_->layoutWarning().isEmpty()) setNotice(session_->layoutWarning());
    return true;
}

bool MainWindow::saveFile(const QString& path) {
    if (QFileInfo(path).suffix().compare(QStringLiteral("qtzpl"), Qt::CaseInsensitive) != 0 && !applySource()) return false;
    const QString previousLayoutWarning = session_->layoutWarning();
    auto result = session_->save(path);
    if (!result) { setNotice(result.error()); return false; }
    if (!previousLayoutWarning.isEmpty()) {
        QStringList notes = session_->document().importNotes;
        notes.append(previews_->editorPreview().diagnostics); notes.removeDuplicates();
        diagnostics_->setPlainText(notes.isEmpty() ? QStringLiteral("Ошибок рендеринга нет.\nПревью построено локально библиотекой QtZpl.") : notes.join(QStringLiteral("\n\n")));
        if (notice_->text() == previousLayoutWarning)
            setNotice(QStringLiteral("Этикетка и настройки холста сохранены."));
    }
    updateTitle(); statusBar()->showMessage(QStringLiteral("Сохранено: %1").arg(session_->filePath()), 8000); return true;
}

bool MainWindow::save(bool saveAs) {
    auto path = session_->filePath();
    QString selectedFilter = QFileInfo(path).suffix().compare(QStringLiteral("zpl"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("ZPL (*.zpl)") : QStringLiteral("Проект QtZpl (*.qtzpl)");
    if (path.isEmpty() || saveAs) path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить этикетку"),
        path.isEmpty() ? QStringLiteral("label.qtzpl") : path,
        QStringLiteral("Проект QtZpl (*.qtzpl);;ZPL (*.zpl)"), &selectedFilter);
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += selectedFilter.startsWith(QStringLiteral("ZPL")) ? QStringLiteral(".zpl") : QStringLiteral(".qtzpl");
    return saveFile(path);
}

bool MainWindow::maybeSave() {
    if (!session_->modified()) return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("Сохранить изменения?"), QStringLiteral("В этикетке есть несохранённые изменения."), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    return answer == QMessageBox::Discard || save();
}

void MainWindow::newDocument() {
    if (!maybeSave()) return;
    setDocument(blankDocument()); restoreCanvasZoom();
}
void MainWindow::chooseOpen() {
    if (!maybeSave()) return;
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Открыть проект или ZPL"), {},
        QStringLiteral("Этикетки (*.qtzpl *.zpl *.txt);;Проект QtZpl (*.qtzpl);;ZPL (*.zpl *.txt);;Все файлы (*)"));
    if (!path.isEmpty()) openFile(path);
}
bool MainWindow::openPreset(const QString& path, int index) {
    auto presets = readPresetCatalog(path);
    if (!presets) { setNotice(presets.error()); return false; }
    if (index < 0 || index >= presets->size()) { setNotice(QStringLiteral("Шаблон не найден.")); return false; }
    auto imported = fromZpl(presets->at(index).source);
    if (!imported) { setNotice(imported.error()); return false; }
    setDocument(*imported);
    restoreCanvasZoom();
    statusBar()->showMessage(QStringLiteral("Шаблон «%1» открыт. Сохранение создаст отдельный ZPL.").arg(presets->at(index).name), 10000);
    return true;
}
void MainWindow::choosePreset() {
    if (!maybeSave()) return;
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Шаблоны маркиратора"), {}, QStringLiteral("INI (*.ini)"));
    if (path.isEmpty()) return;
    auto presets = readPresetCatalog(path);
    if (!presets) { setNotice(presets.error()); return; }
    QStringList names;
    for (qsizetype i = 0; i < presets->size(); ++i) names.append(QStringLiteral("%1. %2").arg(i + 1).arg(presets->at(i).name));
    bool accepted = false;
    const auto choice = QInputDialog::getItem(this, QStringLiteral("Открыть шаблон"), QStringLiteral("Шаблон из INI:"), names, 0, false, &accepted);
    if (accepted) openPreset(path, static_cast<int>(names.indexOf(choice)));
}
void MainWindow::exportPng() {
    const bool pendingSource = session_->hasPendingSource();
    if (!applySource()) return;
    if (!pendingSource) refreshPreview();
    if (!previews_->pageError().isEmpty() || previews_->pagePreview().image.isNull()) return;
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить превью"), QStringLiteral("label.png"), QStringLiteral("PNG (*.png)"));
    if (!path.isEmpty() && !previews_->pagePreview().image.save(path, "PNG")) setNotice(QStringLiteral("Не удалось сохранить PNG."));
}
void MainWindow::updateTitle() {
    const QString name = session_->filePath().isEmpty() ? QStringLiteral("Новая этикетка") : QFileInfo(session_->filePath()).fileName();
    setWindowTitle(QStringLiteral("%1%2 — %3").arg(name, session_->modified() ? QStringLiteral(" *") : QString(), applicationTitle()));
    if (documentName_) documentName_->setText(name + (session_->modified() ? QStringLiteral(" *") : QString()));
}
void MainWindow::updateHistoryActions() {
    if (code_ && code_->hasFocus()) {
        undoAction_->setEnabled(code_->document()->isUndoAvailable());
        redoAction_->setEnabled(code_->document()->isRedoAvailable());
        return;
    }
    undoAction_->setEnabled(!session_->hasPendingSource() && undo_->canUndo());
    redoAction_->setEnabled(!session_->hasPendingSource() && undo_->canRedo());
}
void MainWindow::setNotice(const QString& text) { notice_->setText(text); notice_->setToolTip(text); }
void MainWindow::closeEvent(QCloseEvent* event) {
    if (maybeSave()) {
        workspacePreferences_.windowGeometry = saveGeometry();
        rememberWorkspaceSizes(); persistWorkspace();
        session_->discardRecovery(); event->accept();
    } else event->ignore();
}

bool MainWindow::smokeTest(const QString& directory) {
    QDir().mkpath(directory);
    const auto path = QDir(directory).filePath(QStringLiteral("smoke-label.zpl"));
    const auto original = toZpl(session_->document());
    addElement(ElementType::Text, {32, 300});
    if (session_->document().elements.isEmpty()) return false;
    undo_->undo(); if (toZpl(session_->document()) != original) return false;
    undo_->redo();
    if (!saveFile(path)) return false;
    const auto saved = toZpl(session_->document());
    if (!openFile(path) || toZpl(session_->document()) != saved) return false;
    refreshPreview();
    return !previews_->editorPreview().image.isNull() && previews_->editorPreview().image.save(QDir(directory).filePath(QStringLiteral("smoke-label.png")));
}
} // namespace Designer
