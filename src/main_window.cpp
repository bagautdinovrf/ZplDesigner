#include "main_window.hpp"
#include "command_icons.hpp"
#include "label_canvas.hpp"
#include "canvas_layout_dialog.hpp"
#include "document_session.hpp"
#include "preset_catalog.hpp"
#include "preview_widget.hpp"
#include "preview_pages.hpp"
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
#include <algorithm>
#include <array>
#include <span>

namespace Designer {
namespace {
QString applicationTitle() {
    const auto version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QStringLiteral("QtZpl Designer")
        : QStringLiteral("QtZpl Designer %1").arg(version);
}

// The HTML concept uses painted flat surfaces rather than platform chrome.
class WorkspaceSurface final : public QWidget {
public:
    explicit WorkspaceSurface(QWidget* parent = nullptr, bool alternate = false, bool rounded = false,
                              bool bottomSeparator = true)
        : QWidget(parent), alternate_(alternate), rounded_(rounded), bottomSeparator_(bottomSeparator) {}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, rounded_);
        p.setPen(designerColor(DesignerColor::Border));
        p.setBrush(designerColor(alternate_ ? DesignerColor::PanelAlternate : DesignerColor::Panel));
        if (rounded_) p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 7, 7);
        else {
            p.fillRect(rect(), p.brush());
            if (bottomSeparator_) p.drawLine(0, height() - 1, width(), height() - 1);
        }
    }
private:
    bool alternate_;
    bool rounded_;
    bool bottomSeparator_;
};

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

class ObjectBadge final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen);
        const bool outline = property("outline").toBool();
        if (outline) p.setPen(designerColor(DesignerColor::Border));
        p.setBrush(designerColor(outline ? DesignerColor::PanelAlternate : DesignerColor::Tint));
        p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), outline ? 3 : 5, outline ? 3 : 5); p.end();
        QLabel::paintEvent(event);
    }
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

QLabel* caption(const QString& text, QWidget* parent = nullptr, int size = 10) {
    auto* label = new QLabel(text, parent);
    auto font = label->font(); font.setPixelSize(size); label->setFont(font);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

QWidget* propertyField(const QString& text, QWidget* control, QWidget* parent) {
    auto* field = new QWidget(parent);
    auto* layout = new QVBoxLayout(field); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(5);
    layout->addWidget(caption(text, field)); layout->addWidget(control);
    control->setFixedHeight(30);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (auto* combo = qobject_cast<QComboBox*>(control)) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(4);
    }
    control->setProperty("propertyField", QVariant::fromValue<QObject*>(field));
    if (auto* spin = qobject_cast<QSpinBox*>(control)) {
        spin->setProperty("hideUnitSuffix", true);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setMinimumWidth(0);
    }
    return field;
}

void setPropertyRowVisible(QFormLayout* form, QWidget* control, bool visible) {
    if (auto* field = qobject_cast<QWidget*>(control->property("propertyField").value<QObject*>())) field->setVisible(visible);
    else form->setRowVisible(control, visible);
}

class LayerDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {170, 37}; }
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect.adjusted(0, 1, 0, -1);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        if (selected || option.state.testFlag(QStyle::State_MouseOver)) {
            p->setPen(Qt::NoPen); p->setBrush(designerColor(DesignerColor::Tint)); p->drawRoundedRect(r, 4, 4);
        }
        const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        icon.paint(p, QRect(r.left() + 7, r.center().y() - 8, 16, 16));
        auto font = option.font; font.setPixelSize(11); p->setFont(font);
        p->setPen(designerColor(selected ? DesignerColor::Accent : DesignerColor::Ink));
        const QRect nameRect = r.adjusted(33, 0, -33, 0);
        p->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight, nameRect.width()));
        font.setPixelSize(9); p->setFont(font); p->setPen(designerColor(DesignerColor::Muted));
        p->drawText(r.adjusted(0, 0, -5, 0), Qt::AlignRight | Qt::AlignVCenter, index.data(Qt::UserRole + 1).toString());
        p->restore();
    }
};

constexpr std::array linearBarcodeTypes{
    ElementType::Code128, ElementType::Code39, ElementType::Code93,
    ElementType::Ean13, ElementType::Ean8, ElementType::UpcA, ElementType::UpcE,
    ElementType::Codabar, ElementType::Interleaved2of5, ElementType::Industrial2of5,
    ElementType::Gs1DataBar};
constexpr std::array matrixBarcodeTypes{
    ElementType::DataMatrix, ElementType::QrCode, ElementType::Pdf417,
    ElementType::MicroPdf417, ElementType::Aztec, ElementType::MaxiCode};

std::span<const ElementType> barcodeTypes(ElementType type) {
    if (isLinearBarcodeType(type) || type == ElementType::Gs1DataBar) return linearBarcodeTypes;
    if (isBarcodeType(type)) return matrixBarcodeTypes;
    return {};
}

class ElementIconEngine final : public QIconEngine {
public:
    explicit ElementIconEngine(ElementType type) : type_(type) {}
    QIconEngine* clone() const override { return new ElementIconEngine(type_); }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
    painter->save();
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 32.0, rect.height() / 32.0);
    auto& p = *painter;
    const auto palette = QApplication::palette();
    const auto color = mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::Text)
        : mode == QIcon::Selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::PlaceholderText);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 1.8));
    switch (type_) {
    case ElementType::Text:
        p.drawLine(8, 8, 24, 8); p.drawLine(16, 8, 16, 25); p.drawLine(11, 25, 21, 25); break;
    case ElementType::Box: p.drawRoundedRect(QRectF(6, 7, 21, 18), 2, 2); break;
    case ElementType::Line: p.drawLine(6, 16, 26, 16); break;
    case ElementType::Diagonal: p.drawLine(6, 25, 26, 7); break;
    case ElementType::Circle: p.drawEllipse(QRectF(6, 6, 21, 21)); break;
    case ElementType::Ellipse: p.drawEllipse(QRectF(5, 9, 23, 15)); break;
    case ElementType::Image:
        p.drawRoundedRect(QRectF(5, 6, 23, 21), 2, 2); p.drawEllipse(QPointF(12, 12), 2, 2);
        p.drawPolyline(QPolygonF{{6, 24}, {15, 16}, {19, 20}, {24, 14}, {27, 18}}); break;
    case ElementType::Code128:
    case ElementType::Ean13:
    case ElementType::Ean8:
    case ElementType::UpcA:
    case ElementType::UpcE:
    case ElementType::Code39:
    case ElementType::Pdf417:
    case ElementType::Codabar:
    case ElementType::Code93:
    case ElementType::Interleaved2of5:
    case ElementType::Industrial2of5:
    case ElementType::Gs1DataBar:
    case ElementType::MicroPdf417:
        for (int x = 5; x < 28; x += 4) { p.setPen(QPen(color, x % 3 + 1)); p.drawLine(x, 7, x, 25); } break;
    case ElementType::QrCode:
    case ElementType::DataMatrix:
    case ElementType::Aztec:
    case ElementType::MaxiCode:
        p.drawRect(5, 5, 8, 8); p.drawRect(19, 5, 8, 8); p.drawRect(5, 19, 8, 8);
        p.fillRect(20, 20, 4, 4, color); p.fillRect(25, 25, 3, 3, color); break;
    case ElementType::Table:
        p.drawRect(5, 6, 23, 21);
        p.drawLine(5, 13, 28, 13); p.drawLine(5, 20, 28, 20);
        p.drawLine(13, 6, 13, 27); p.drawLine(21, 6, 21, 27); break;
    }
    painter->restore();
    }
private:
    ElementType type_;
};

QIcon elementIcon(ElementType type) { return QIcon(new ElementIconEngine(type)); }

QString elementSymbol(ElementType type) {
    if (type == ElementType::Text) return QStringLiteral("T");
    if (isLinearBarcodeType(type)) return QStringLiteral("▥");
    if (isBarcodeType(type)) return QStringLiteral("▦");
    if (type == ElementType::Table) return QStringLiteral("▤");
    if (type == ElementType::Circle || type == ElementType::Ellipse) return QStringLiteral("○");
    if (type == ElementType::Line || type == ElementType::Diagonal) return QStringLiteral("╱");
    if (type == ElementType::Image) return QStringLiteral("▧");
    return QStringLiteral("□");
}

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
                         ElementType::DataMatrix, ElementType::Table, ElementType::Diagonal}) {
            const auto name = type == ElementType::Code128 ? QStringLiteral("1D-код")
                : type == ElementType::DataMatrix ? QStringLiteral("2D-код") : elementTypeName(type);
            auto* item = new QListWidgetItem(elementIcon(type), name, this);
            item->setData(Qt::UserRole, static_cast<int>(type));
            item->setSizeHint({31, 32});
            item->setToolTip(isBarcodeType(type)
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

QLabel* heading(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    auto font = label->font(); font.setWeight(QFont::DemiBold); font.setPixelSize(12);
    label->setFont(font);
    return label;
}
QLabel* sectionHeading(const QString& text, QWidget* parent) {
    auto* label = heading(text, parent); auto font = label->font(); font.setPixelSize(11); label->setFont(font); return label;
}
QSpinBox* spin(const QString& name, int minimum, int maximum, QWidget* parent) {
    auto* value = new QSpinBox(parent);
    value->setObjectName(name);
    value->setRange(minimum, maximum);
    value->setKeyboardTracking(false);
    value->setMinimumHeight(30);
    return value;
}
QSpinBox* dotSpin(const QString& name, int minimum, int maximum, QWidget* parent) {
    auto* value = new DotSpinBox(parent);
    value->setObjectName(name);
    value->setRange(minimum, maximum);
    return value;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("designerWindow"));
    resize(1440, 900);
    setMinimumSize(1050, 680);
    workspacePreferences_ = loadWorkspacePreferences();
    setDesignerTheme(workspacePreferences_.dark ? DesignerTheme::Dark : DesignerTheme::Light);
    session_ = new DocumentSession(this);
    undo_ = session_->undoStack();
    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(90);
    connect(previewTimer_, &QTimer::timeout, this, &MainWindow::refreshPreview);
    sourceTimer_ = new QTimer(this);
    sourceTimer_->setSingleShot(true);
    sourceTimer_->setInterval(300);
    connect(sourceTimer_, &QTimer::timeout, this, &MainWindow::applyLiveSource);
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
    auto* fresh = new QAction(QStringLiteral("Новая"), this); fresh->setShortcut(QKeySequence::New); addAction(fresh);
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
        else if (!codePending_) undo_->undo();
    });
    connect(redoAction_, &QAction::triggered, this, [this] {
        if (code_->hasFocus()) code_->redo();
        else if (!codePending_) undo_->redo();
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
    auto* brand = heading(applicationTitle(), title); brand->setObjectName(QStringLiteral("applicationBrand"));
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
    labelWidth_ = spin(QStringLiteral("labelWidth"), 32, 4096, labelPage);
    formatGrid->addWidget(propertyField(QStringLiteral("Ширина, dots"), labelWidth_, formatSection), 0, 0);
    labelHeight_ = spin(QStringLiteral("labelHeight"), 32, 4096, labelPage);
    formatGrid->addWidget(propertyField(QStringLiteral("Высота, dots"), labelHeight_, formatSection), 0, 1);
    dpi_ = new QComboBox(labelPage); dpi_->setObjectName(QStringLiteral("dpi"));
    for (int dpi : {203, 300, 600}) dpi_->addItem(QStringLiteral("%1 dpi").arg(dpi), dpi);
    formatGrid->addWidget(propertyField(QStringLiteral("Разрешение"), dpi_, formatSection), 1, 0, 1, 2);
    auto* formatHint = caption(QStringLiteral("Геометрия хранится в точках принтера.\nМасштаб не меняет размер этикетки."), formatSection);
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
        sourceSelectionActive_ = false;
        sourceCaretActive_ = false;
        updateSelectionToolTip();
        if (enabled && code_->hasFocus()) syncSourceSelection(true);
        syncDesignSelection();
        persistWorkspace();
    });
    canvasHeaderRow->addStretch();
    grid_ = new QCheckBox(QStringLiteral("Сетка"), canvasHeader); grid_->setObjectName(QStringLiteral("showGrid")); grid_->setChecked(true); grid_->hide();
    connect(labelGrid, &QCheckBox::toggled, grid_, &QCheckBox::setChecked); connect(grid_, &QCheckBox::toggled, labelGrid, &QCheckBox::setChecked);
    snap_ = new QCheckBox(QStringLiteral("Привязка"), canvasHeader); snap_->setObjectName(QStringLiteral("snapEnabled")); snap_->setChecked(true); snap_->hide();
    for (auto* check : {grid_, snap_}) {
        auto* button = new QPushButton(check->text(), canvasHeader); button->setCheckable(true); button->setChecked(check->isChecked()); button->setProperty("compact", true);
        canvasHeaderRow->addWidget(button); connect(button, &QPushButton::toggled, check, &QCheckBox::setChecked); connect(check, &QCheckBox::toggled, button, &QPushButton::setChecked);
    }
    auto* canvasSetup = new QPushButton(QStringLiteral("···"), canvasHeader); canvasSetup->setObjectName(QStringLiteral("canvasSetup")); canvasSetup->setProperty("compact", true); canvasSetup->setFixedWidth(25);
    canvasSetup->setToolTip(QStringLiteral("Сетка, линейки, направляющие и безопасный отступ")); canvasHeaderRow->addWidget(canvasSetup);
    connect(canvasSetup, &QPushButton::clicked, this, &MainWindow::showCanvasLayout);
    auto* propertiesTool = addTool(canvasHeaderRow, propertiesAction_, true);
    propertiesTool->setToolTip(QStringLiteral("Показать или скрыть свойства · Ctrl+Alt+P"));
    centerLayout->addWidget(canvasHeader);
    canvas_ = new LabelCanvas(editorTabs_); canvas_->setObjectName(QStringLiteral("labelCanvas"));
    canvas_->viewport()->installEventFilter(this);
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
        previewPage_ = page;
        refreshCleanPreview();
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
    auto* properties = new WorkspaceSurface(scroll, false, false, false); auto* propertiesLayout = new QVBoxLayout(properties);
    propertiesLayout->setContentsMargins(0, 0, 0, 0); propertiesLayout->setSpacing(0);
    auto* summary = new WorkspaceSurface(properties); auto* summaryLayout = new QVBoxLayout(summary); summaryLayout->setContentsMargins(13, 12, 13, 12); summaryLayout->setSpacing(10);
    summaryLayout->addWidget(heading(QStringLiteral("Выбранный объект"), summary));
    auto* summaryRow = new QHBoxLayout; summaryRow->setSpacing(9);
    objectSymbol_ = new ObjectBadge(QStringLiteral("T"), summary); objectSymbol_->setFixedSize(29, 29); objectSymbol_->setAlignment(Qt::AlignCenter); objectSymbol_->setForegroundRole(QPalette::Link);
    auto symbolFont = objectSymbol_->font(); symbolFont.setPixelSize(15); objectSymbol_->setFont(symbolFont);
    auto* objectText = new QVBoxLayout; objectText->setSpacing(2);
    typeTitle_ = heading(QStringLiteral("Выберите элемент"), summary); objectText->addWidget(typeTitle_);
    objectSubtitle_ = caption(QString(), summary); objectText->addWidget(objectSubtitle_);
    summaryRow->addWidget(objectSymbol_); summaryRow->addLayout(objectText, 1); summaryLayout->addLayout(summaryRow); propertiesLayout->addWidget(summary);
    propertyUnits_ = new QComboBox(properties);
    propertyUnits_->setObjectName(QStringLiteral("propertyUnits"));
    propertyUnits_->addItem(QStringLiteral("мм"), static_cast<int>(CanvasUnit::Millimeters));
    propertyUnits_->addItem(QStringLiteral("dots"), static_cast<int>(CanvasUnit::Dots));
    propertyUnits_->setFixedSize(60, 25); propertyUnits_->setProperty("compact", true);
    connect(propertyUnits_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_) return;
        auto settings = session_->layoutSettings();
        settings.unit = static_cast<CanvasUnit>(propertyUnits_->currentData().toInt());
        session_->setLayoutSettings(std::move(settings), QStringLiteral("Единицы измерения"));
    });
    elementForm_ = new QWidget(properties); auto* elementLayout = new QVBoxLayout(elementForm_); elementLayout->setContentsMargins(0, 0, 0, 0); elementLayout->setSpacing(0);
    auto* geometry = new WorkspaceSurface(elementForm_); auto* geometryLayout = new QVBoxLayout(geometry); geometryLayout->setContentsMargins(13, 12, 13, 12); geometryLayout->setSpacing(10);
    auto* geometryTitle = new QHBoxLayout; geometryTitle->addWidget(sectionHeading(QStringLiteral("Положение и размер"), geometry)); geometryTitle->addStretch(); geometryTitle->addWidget(propertyUnits_); geometryLayout->addLayout(geometryTitle);
    auto* geometryGrid = new QGridLayout; geometryGrid->setSpacing(10); geometryGrid->setColumnStretch(0, 1); geometryGrid->setColumnStretch(1, 1);
    geometryLayout->addLayout(geometryGrid); elementLayout->addWidget(geometry);
    auto* contentSection = new WorkspaceSurface(elementForm_); contentSection_ = contentSection;
    auto* contentLayout = new QVBoxLayout(contentSection); contentLayout->setContentsMargins(13, 12, 13, 12); contentLayout->setSpacing(10);
    contentLayout->addWidget(sectionHeading(QStringLiteral("Содержимое"), contentSection));
    auto* contentGrid = new QGridLayout; contentGrid->setSpacing(10); contentGrid->setColumnStretch(0, 1); contentGrid->setColumnStretch(1, 1);
    contentLayout->addLayout(contentGrid); elementLayout->addWidget(contentSection);
    auto* advanced = new QWidget(elementForm_); advanced->setObjectName(QStringLiteral("advancedProperties"));
    auto* form = new QFormLayout(advanced); form->setObjectName(QStringLiteral("detailForm"));
    form->setContentsMargins(13, 12, 13, 12); form->setSpacing(8); form->setRowWrapPolicy(QFormLayout::WrapAllRows); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    barcodeType_ = new QComboBox(properties);
    barcodeType_->setObjectName(QStringLiteral("barcodeType"));
    contentLayout->insertWidget(1, propertyField(QStringLiteral("Тип кода"), barcodeType_, contentSection));
    connect(barcodeType_, &QComboBox::currentIndexChanged, this, &MainWindow::editBarcodeType);
    x_ = dotSpin(QStringLiteral("elementX"), -4096, 4096, properties); geometryGrid->addWidget(propertyField(QStringLiteral("X"), x_, geometry), 0, 0);
    y_ = dotSpin(QStringLiteral("elementY"), -4096, 4096, properties); geometryGrid->addWidget(propertyField(QStringLiteral("Y"), y_, geometry), 0, 1);
    width_ = dotSpin(QStringLiteral("elementWidth"), 1, 4096, properties); geometryGrid->addWidget(propertyField(QStringLiteral("Ширина"), width_, geometry), 1, 0);
    height_ = dotSpin(QStringLiteral("elementHeight"), 1, 4096, properties); geometryGrid->addWidget(propertyField(QStringLiteral("Высота"), height_, geometry), 1, 1);
    geometryAspect_ = new QCheckBox(QStringLiteral("Сохранять пропорции"), geometry);
    auto aspectFont = geometryAspect_->font(); aspectFont.setPixelSize(11); geometryAspect_->setFont(aspectFont);
    geometryLayout->addWidget(geometryAspect_);
    connect(geometryAspect_, &QCheckBox::clicked, this, [this](bool checked) {
        if (!updating_ && selected_ >= 0 && session_->document().elements[selected_].type == ElementType::Image)
            imageKeepAspectRatio_->setChecked(checked);
    });
    sizeHint_ = new QLabel(properties); sizeHint_->setWordWrap(true);
    sizeHint_->setObjectName(QStringLiteral("elementSizeHint")); form->addRow(sizeHint_);
    content_ = new QPlainTextEdit(properties); content_->setObjectName(QStringLiteral("elementContent"));
    content_->setFixedHeight(30); content_->setTabChangesFocus(true); contentGrid->addWidget(propertyField(QStringLiteral("Содержимое"), content_, contentSection), 0, 0, 1, 2);
    gs1_ = new QCheckBox(QStringLiteral("GS1 DataMatrix (FNC1)"), properties);
    auto primaryRow = [contentLayout, contentSection](QWidget* control) {
        auto* row = new QWidget(contentSection); auto* layout = new QVBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0); layout->addWidget(control);
        control->setProperty("propertyField", QVariant::fromValue<QObject*>(row)); contentLayout->addWidget(row);
    };
    gs1_->setObjectName(QStringLiteral("dataMatrixGs1")); primaryRow(gs1_);
    gs1Builder_ = new QPushButton(QStringLiteral("Мастер GS1…"), properties);
    gs1Builder_->setObjectName(QStringLiteral("gs1Builder")); primaryRow(gs1Builder_);
    placeholderValues_ = new QPushButton(QStringLiteral("Значения плейсхолдеров…"), properties);
    placeholderValues_->setObjectName(QStringLiteral("placeholderValues"));
    placeholderValues_->setToolTip(QStringLiteral("Значения полей {{01}}, {{21}} и параметров [[имя]] для превью и заполненного ZPL"));
    primaryRow(placeholderValues_);
    connect(placeholderValues_, &QPushButton::clicked, this, &MainWindow::showPlaceholderValues);
    gs1Status_ = new QLabel(properties); gs1Status_->setWordWrap(true);
    gs1Status_->setObjectName(QStringLiteral("gs1Status")); primaryRow(gs1Status_);
    connect(gs1Builder_, &QPushButton::clicked, this, &MainWindow::showGs1Builder);
    fontHeight_ = dotSpin(QStringLiteral("fontHeight"), 1, 512, properties); contentGrid->addWidget(propertyField(QStringLiteral("Высота шрифта, dots"), fontHeight_, contentSection), 1, 0);
    fontWidth_ = dotSpin(QStringLiteral("fontWidth"), 0, 512, properties); fontWidth_->setSpecialValueText(QStringLiteral("Авто"));
    form->addRow(QStringLiteral("Ширина букв"), fontWidth_);
    blockWidth_ = dotSpin(QStringLiteral("blockWidth"), 0, 4096, properties);
    blockWidth_->setSpecialValueText(QStringLiteral("Без блока"));
    form->addRow(QStringLiteral("Ширина блока"), blockWidth_);
    blockLines_ = spin(QStringLiteral("blockMaxLines"), 1, 128, properties);
    form->addRow(QStringLiteral("Строк в блоке"), blockLines_);
    blockSpacing_ = dotSpin(QStringLiteral("blockLineSpacing"), -512, 512, properties);
    form->addRow(QStringLiteral("Интервал"), blockSpacing_);
    blockIndent_ = dotSpin(QStringLiteral("blockHangingIndent"), -4096, 4096, properties);
    form->addRow(QStringLiteral("Отступ"), blockIndent_);
    blockAlignment_ = new QComboBox(properties); blockAlignment_->setObjectName(QStringLiteral("blockAlignment"));
    blockAlignment_->addItem(QStringLiteral("Слева"), QStringLiteral("L"));
    blockAlignment_->addItem(QStringLiteral("По центру"), QStringLiteral("C"));
    blockAlignment_->addItem(QStringLiteral("Справа"), QStringLiteral("R"));
    blockAlignment_->addItem(QStringLiteral("По ширине"), QStringLiteral("J"));
    form->addRow(QStringLiteral("Выравнивание"), blockAlignment_);
    code128Mode_ = new QComboBox(properties); code128Mode_->setObjectName(QStringLiteral("code128Mode"));
    code128Mode_->addItem(QStringLiteral("Обычный (N)"), QStringLiteral("N"));
    code128Mode_->addItem(QStringLiteral("Автоматический (A)"), QStringLiteral("A"));
    code128Mode_->addItem(QStringLiteral("Subset B"), QStringLiteral("B"));
    code128Mode_->addItem(QStringLiteral("Subset C"), QStringLiteral("C"));
    code128Mode_->addItem(QStringLiteral("GS1-128 (D)"), QStringLiteral("D"));
    code128Mode_->addItem(QStringLiteral("UCC (U)"), QStringLiteral("U"));
    form->addRow(QStringLiteral("Режим Code 128"), code128Mode_);
    interpretationAbove_ = new QCheckBox(QStringLiteral("Подпись над кодом"), properties);
    interpretationAbove_->setObjectName(QStringLiteral("interpretationAbove"));
    form->addRow(interpretationAbove_);
    thickness_ = dotSpin(QStringLiteral("thickness"), 1, 512, properties); contentGrid->addWidget(propertyField(QStringLiteral("Толщина, dots"), thickness_, contentSection), 1, 0);
    module_ = dotSpin(QStringLiteral("moduleWidth"), 1, 10, properties); contentGrid->addWidget(propertyField(QStringLiteral("Модуль, dots"), module_, contentSection), 1, 0);
    orientation_ = new QComboBox(properties); orientation_->setObjectName(QStringLiteral("orientation"));
    for (const auto& pair : {qMakePair("0°", 'N'), qMakePair("90°", 'R'), qMakePair("180°", 'I'), qMakePair("270°", 'B')})
        orientation_->addItem(QString::fromUtf8(pair.first), QString(QChar::fromLatin1(pair.second)));
    contentGrid->addWidget(propertyField(QStringLiteral("Поворот"), orientation_, contentSection), 1, 1);
    readable_ = new QCheckBox(QStringLiteral("Текст под штрихкодом"), properties); form->addRow(readable_);
    reverse_ = new QCheckBox(QStringLiteral("Инверсия поля"), properties); form->addRow(reverse_);
    whiteGraphic_ = new QCheckBox(QStringLiteral("Белая фигура"), properties);
    whiteGraphic_->setObjectName(QStringLiteral("whiteGraphic")); form->addRow(whiteGraphic_);
    connect(whiteGraphic_, &QCheckBox::toggled, this, &MainWindow::editProperties);
    diagonalDirection_ = new QComboBox(properties); diagonalDirection_->setObjectName(QStringLiteral("diagonalDirection"));
    diagonalDirection_->addItem(QStringLiteral("Восходящая /"), QStringLiteral("R"));
    diagonalDirection_->addItem(QStringLiteral("Нисходящая \\"), QStringLiteral("L"));
    form->addRow(QStringLiteral("Диагональ"), diagonalDirection_);
    connect(diagonalDirection_, &QComboBox::currentIndexChanged, this, &MainWindow::editProperties);
    threshold_ = spin(QStringLiteral("imageThreshold"), 0, 255, properties); form->addRow(QStringLiteral("Порог чёрного"), threshold_);
    dither_ = new QCheckBox(QStringLiteral("Дизеринг Floyd–Steinberg"), properties); form->addRow(dither_);
    imageKeepAspectRatio_ = new QCheckBox(QStringLiteral("Сохранять пропорции"), properties);
    imageKeepAspectRatio_->setObjectName(QStringLiteral("imageKeepAspectRatio")); form->addRow(imageKeepAspectRatio_);
    imageEdit_ = new QPushButton(QStringLiteral("Изменить изображение…"), properties);
    imageEdit_->setToolTip(QStringLiteral("Обрезка, поворот и отражение изображения"));
    imageEdit_->setObjectName(QStringLiteral("editImage")); form->addRow(imageEdit_);
    connect(imageEdit_, &QPushButton::clicked, this, &MainWindow::editImage);
    tableEdit_ = new QPushButton(QStringLiteral("Ячейки таблицы…"), properties);
    tableEdit_->setObjectName(QStringLiteral("editTable")); form->addRow(tableEdit_);
    connect(tableEdit_, &QPushButton::clicked, this, &MainWindow::editTable);
    barcodeOptions_ = new QPushButton(QStringLiteral("Параметры кода…"), properties);
    barcodeOptions_->setObjectName(QStringLiteral("barcodeOptions")); form->addRow(barcodeOptions_);
    connect(barcodeOptions_, &QPushButton::clicked, this, &MainWindow::editBarcodeOptions);
    advancedText_ = new QPushButton(QStringLiteral("Направление текста…"), properties);
    advancedText_->setToolTip(QStringLiteral("Положение, базовая линия и направление текста"));
    advancedText_->setObjectName(QStringLiteral("advancedText")); form->addRow(advancedText_);
    connect(advancedText_, &QPushButton::clicked, this, &MainWindow::editAdvancedText);
    qrCorrection_ = new QComboBox(properties); qrCorrection_->setObjectName(QStringLiteral("qrCorrection"));
    for (const auto& level : {QStringLiteral("L"), QStringLiteral("M"), QStringLiteral("Q"), QStringLiteral("H")})
        qrCorrection_->addItem(level, level);
    form->addRow(QStringLiteral("Коррекция QR"), qrCorrection_);
    qrPayload_ = new QPushButton(QStringLiteral("Содержимое QR…"), properties);
    qrPayload_->setToolTip(QStringLiteral("Ссылка, Wi-Fi или контакт"));
    qrPayload_->setObjectName(QStringLiteral("qrPayload")); form->addRow(qrPayload_);
    connect(qrPayload_, &QPushButton::clicked, this, &MainWindow::editQrPayload);
    connect(qrCorrection_, &QComboBox::currentIndexChanged, this, &MainWindow::editProperties);
    pdfRowHeight_ = dotSpin(QStringLiteral("pdfRowHeight"), 1, 64, properties);
    pdfColumns_ = spin(QStringLiteral("pdfColumns"), 0, 30, properties); pdfColumns_->setSpecialValueText(QStringLiteral("Авто"));
    pdfSecurity_ = spin(QStringLiteral("pdfSecurity"), 0, 8, properties);
    form->addRow(QStringLiteral("Высота строки PDF417"), pdfRowHeight_);
    form->addRow(QStringLiteral("Колонки PDF417"), pdfColumns_);
    form->addRow(QStringLiteral("Коррекция PDF417"), pdfSecurity_);
    for (auto* control : {pdfRowHeight_, pdfColumns_, pdfSecurity_})
        connect(control, &QSpinBox::valueChanged, this, &MainWindow::editProperties);
    auto* alignment = new WorkspaceSurface(elementForm_); auto* alignmentLayout = new QVBoxLayout(alignment); alignmentLayout->setContentsMargins(13, 12, 13, 12); alignmentLayout->setSpacing(10);
    alignmentLayout->addWidget(sectionHeading(QStringLiteral("Выравнивание"), alignment)); auto* alignmentRow = new QHBoxLayout; alignmentRow->setSpacing(4);
    const QStringList alignNames{QStringLiteral("alignLeft"), QStringLiteral("alignHorizontalCenter"), QStringLiteral("alignRight")};
    const QStringList alignTexts{QStringLiteral("Слева"), QStringLiteral("Центр"), QStringLiteral("Справа")};
    for (int i = 0; i < 3; ++i) { auto* button = new QPushButton(alignTexts[i], alignment); button->setProperty("compact", true); alignmentRow->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name = alignNames[i]] { if (auto* action = findChild<QAction*>(name)) action->trigger(); }); }
    alignmentLayout->addLayout(alignmentRow); elementLayout->addWidget(alignment);
    auto* more = new QToolButton(elementForm_); more->setObjectName(QStringLiteral("advancedPropertiesToggle")); more->setText(QStringLiteral("Дополнительные параметры")); more->setCheckable(true); more->setProperty("flat", true); more->setArrowType(Qt::RightArrow); more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    elementLayout->addWidget(more); elementLayout->addWidget(advanced); advanced->hide();
    connect(more, &QToolButton::toggled, advanced, &QWidget::setVisible);
    connect(more, &QToolButton::toggled, more, [more](bool open) { more->setArrowType(open ? Qt::DownArrow : Qt::RightArrow); });
    propertiesLayout->addWidget(elementForm_);
    selectionProperties_ = new SelectionProperties(properties);
    propertiesLayout->addWidget(selectionProperties_);
    connect(selectionProperties_, &SelectionProperties::changesRequested, this, [this](const QMap<QString, int>& values) {
        if (codePending_ || !session_->document().visualEditable || selectionLocked()) return;
        auto next = session_->document();
        if (applySelectionProperties(next, canvas_->selectedIndices(), values))
            commitDocument(std::move(next), QStringLiteral("Общие свойства выделения"));
    });
    auto* actions = new QHBoxLayout; actions->setContentsMargins(13, 12, 13, 12);
    auto* duplicate = new QPushButton(QStringLiteral("Дублировать"), properties);
    auto* remove = new QPushButton(QStringLiteral("Удалить"), properties); actions->addWidget(duplicate); actions->addWidget(remove);
    propertiesLayout->addLayout(actions); propertiesLayout->addStretch(); scroll->setWidget(properties);
    inspectorTabs_->addTab(scroll, QStringLiteral("Объект"));
    inspectorTabs_->addTab(labelPage, QStringLiteral("Этикетка"));
    auto* layerPage = new WorkspaceSurface(splitter); auto* layerLayout = new QVBoxLayout(layerPage);
    layersPanel_ = layerPage; layerPage->setObjectName(QStringLiteral("layersPanel"));
    layerPage->setMinimumWidth(150); layerPage->setMaximumWidth(320);
    layerLayout->setContentsMargins(6, 0, 6, 0); layerLayout->setSpacing(0);
    auto* layerHeader = new QWidget(layerPage); layerHeader->setFixedHeight(37); auto* layerHeaderRow = new QHBoxLayout(layerHeader); layerHeaderRow->setContentsMargins(7, 0, 7, 0);
    layerHeaderRow->addWidget(heading(QStringLiteral("Слои"), layerHeader)); layerHeaderRow->addStretch(); layerCount_ = caption(QString(), layerHeader); layerHeaderRow->addWidget(layerCount_); layerLayout->addWidget(layerHeader);
    layers_ = new QListWidget(layerPage); layers_->setObjectName(QStringLiteral("layerList")); layerLayout->addWidget(layers_);
    layers_->setFrameShape(QFrame::NoFrame); layers_->setItemDelegate(new LayerDelegate(layers_)); layers_->setMouseTracking(true); layers_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    connect(layerLocked_, &QCheckBox::clicked, this, [this](bool value) { setLayerFlag(QStringLiteral("locked"), value); });
    connect(layerVisible_, &QCheckBox::clicked, this, [this](bool value) { setLayerFlag(QStringLiteral("visible"), value); });
    connect(layerPrintable_, &QCheckBox::clicked, this, [this](bool value) { setLayerFlag(QStringLiteral("printable"), value); });
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
    sourceDiagnostics_ = new QListWidget(diagnosticsPage);
    sourceDiagnostics_->setObjectName(QStringLiteral("sourceDiagnostics"));
    sourceDiagnostics_->setToolTip(QStringLiteral("Двойной щелчок — перейти к команде ZPL"));
    sourceDiagnostics_->setWordWrap(true);
    diagnosticsLayout->addWidget(sourceDiagnostics_);
    connect(sourceDiagnostics_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item->data(Qt::UserRole).toInt() < 0) return;
        editorTabs_->setCurrentIndex(1);
        static_cast<ZplSourceEditor*>(code_)->goToSourceOffset(item->data(Qt::UserRole).toInt());
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
        if (!syncingSourceSelection_) {
            sourceSelectionActive_ = false;
            sourceCaretActive_ = false;
        }
        syncSelection();
        if (!syncingSourceSelection_) syncDesignSelection(true);
    });
    connect(canvas_, &LabelCanvas::editingStarted, this, [this] {
        if (previewTimer_->isActive()) refreshPreview();
    });
    connect(canvas_, &LabelCanvas::elementDropped, this, &MainWindow::addElement);
    connect(canvas_, &LabelCanvas::imageDropped, this, [this](const QString& path, QPoint p) { importImage(path, p); });
    connect(canvas_, &LabelCanvas::deleteRequested, this, &MainWindow::deleteElement);
    connect(canvas_, &LabelCanvas::duplicateRequested, this, &MainWindow::duplicateElement);
    connect(canvas_, &LabelCanvas::elementMoved, this, [this](int index, QPoint position) {
        if (!session_->document().visualEditable || index < 0 || index >= session_->document().elements.size()
            || session_->document().elements[index].locked) return;
        auto next = session_->document(); next.elements[index].x = position.x(); next.elements[index].y = position.y();
        commitDocument(std::move(next), QStringLiteral("Переместить элемент"), index);
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
            content_->setFocus(Qt::OtherFocusReason);
        }
    });
    connect(canvas_, &LabelCanvas::elementsMoved, this, [this](const QList<int>& indices, QPoint delta) {
        if (!codePending_) session_->moveElements(indices, delta);
    });
    connect(duplicate, &QPushButton::clicked, this, &MainWindow::duplicateElement);
    connect(remove, &QPushButton::clicked, this, &MainWindow::deleteElement);
    connect(front, &QPushButton::clicked, this, [this] { moveLayer(1); });
    connect(back, &QPushButton::clicked, this, [this] { moveLayer(-1); });
    connect(layers_, &QListWidget::itemSelectionChanged, this, [this] {
        if (updating_) return;
        QList<int> indices;
        for (const auto* item : layers_->selectedItems()) indices.append(item->data(Qt::UserRole).toInt());
        if (const auto* current = layers_->currentItem(); current && current->isSelected()) {
            const int primary = current->data(Qt::UserRole).toInt();
            indices.removeOne(primary); indices.append(primary);
        }
        canvas_->setSelectedIndices(indices);
        syncSelection();
    });
    connect(layers_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (updating_ || !current || !current->isSelected()) return;
        auto indices = canvas_->selectedIndices();
        const int primary = current->data(Qt::UserRole).toInt();
        if (indices.removeOne(primary)) { indices.append(primary); canvas_->setSelectedIndices(indices); }
    });
    for (auto* value : {x_, y_, fontHeight_, fontWidth_, thickness_, module_, threshold_,
                       blockWidth_, blockLines_, blockSpacing_, blockIndent_})
        connect(value, &QSpinBox::valueChanged, this, &MainWindow::editProperties);
    auto resizeProperty = [this](bool horizontal, int value) {
        if (updating_ || selected_ < 0) return;
        if (previewTimer_->isActive()) refreshPreview();
        QSize size = elementSize(selected_);
        if (horizontal) size.setWidth(value); else size.setHeight(value);
        resizeElement(selected_, size);
    };
    connect(width_, &QSpinBox::valueChanged, this, [resizeProperty](int value) { resizeProperty(true, value); });
    connect(height_, &QSpinBox::valueChanged, this, [resizeProperty](int value) { resizeProperty(false, value); });
    connect(content_, &QPlainTextEdit::textChanged, this, &MainWindow::editProperties);
    connect(orientation_, &QComboBox::currentIndexChanged, this, &MainWindow::editProperties);
    connect(blockAlignment_, &QComboBox::currentIndexChanged, this, &MainWindow::editProperties);
    connect(code128Mode_, &QComboBox::currentIndexChanged, this, &MainWindow::editProperties);
    connect(interpretationAbove_, &QCheckBox::toggled, this, &MainWindow::editProperties);
    for (auto* value : {readable_, reverse_, dither_, imageKeepAspectRatio_}) connect(value, &QCheckBox::toggled, this, &MainWindow::editProperties);
    connect(gs1_, &QCheckBox::toggled, this, [this] {
        if (updating_ || canvas_->selectedIndices().size() != 1 || !session_->document().visualEditable
            || selected_ < 0 || selected_ >= session_->document().elements.size()) return;
        const auto& element = session_->document().elements[selected_];
        if (element.type != ElementType::DataMatrix) return;
        if (codePending_) {
            const QSignalBlocker blocker(gs1_);
            gs1_->setChecked(element.gs1);
            setNotice(QStringLiteral("Перейдите в «Дизайн» или «Превью», чтобы применить изменённый ZPL."));
            return;
        }
        editProperties();
    });
    auto labelChanged = [this] {
        if (updating_ || codePending_ || !session_->document().visualEditable) return;
        auto next = session_->document(); next.width = labelWidth_->value(); next.height = labelHeight_->value(); next.dpi = dpi_->currentData().toInt();
        commitDocument(std::move(next), QStringLiteral("Параметры этикетки"));
    };
    connect(labelWidth_, &QSpinBox::valueChanged, this, labelChanged);
    connect(labelHeight_, &QSpinBox::valueChanged, this, labelChanged);
    connect(dpi_, &QComboBox::currentIndexChanged, this, labelChanged);
    connect(code_, &QPlainTextEdit::textChanged, this, [this] {
        if (updating_ || code_->isReadOnly()) return;
        codePending_ = code_->toPlainText() != mappedSource_;
        mappedSourceRevision_ = codePending_ ? -1 : code_->document()->revision();
        sourceTimer_->stop();
        if (codePending_) session_->setPendingSource(code_->toPlainText());
        else session_->clearPendingSource();
        canvas_->setEnabled(!codePending_);
        for (auto* field : {labelWidth_, labelHeight_}) field->setEnabled(!codePending_ && session_->document().visualEditable);
        dpi_->setEnabled(!codePending_ && session_->document().visualEditable);
        refreshProperties();
        updateTitle(); updateHistoryActions();
        if (codePending_) {
            sourceDiagnostics_->clear();
            sourceDiagnostics_->hide();
            const bool live = workspacePreferences_.editorLayout != EditorLayout::Single;
            sourceStatus_->setText(live ? QStringLiteral("Обновление после ввода…")
                : QStringLiteral("Код изменён. Перейдите в «Дизайн» или «Превью», чтобы посмотреть результат."));
            setNotice(sourceStatus_->text());
            if (!live) previewWidget_->setPreview({}, session_->document().dpi,
                QStringLiteral("Код ZPL изменён. Откройте вкладку «Превью», чтобы посмотреть результат."));
            if (live) sourceTimer_->start();
        } else { updateEditorPresentation(); refreshPreview(); }
        syncSourceSelection(code_->hasFocus());
        syncDesignSelection();
    });
    connect(code_, &QPlainTextEdit::selectionChanged, this, [this] {
        syncSourceSelection(code_->hasFocus());
    });
    connect(code_, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (code_->hasFocus()) syncSourceSelection(true);
    });
    connect(static_cast<ZplSourceEditor*>(code_), &ZplSourceEditor::sourceCursorActivated,
            this, [this] { syncSourceSelection(true); });
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
        else if (index == 0 && codePending_ && !applySource()) editorTabs_->setCurrentIndex(1);
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
        if (codePending_ || sourceFocused) editorTabs_->setCurrentIndex(1);
    }
    if (sourceFocused) code_->setFocus(Qt::OtherFocusReason);
    swapEditorSides_->setEnabled(split);
    sourceTimer_->stop();
    if (codePending_) {
        sourceStatus_->setText(split ? QStringLiteral("Обновление после ввода…")
            : QStringLiteral("Код изменён. Перейдите в «Дизайн» или «Превью», чтобы посмотреть результат."));
        if (split) sourceTimer_->start();
    }
    applyingEditorLayout_ = false;
    updateEditorPresentation();
    updateHistoryActions();
    applyWorkspaceLayout();
    syncDesignSelection(!sourceFocused);
}

void MainWindow::updateEditorPresentation() {
    if (codePending_) return;
    sourceStatus_->setText(workspacePreferences_.editorLayout == EditorLayout::Single
        ? QStringLiteral("ZPL применяется при переходе в «Дизайн» или «Превью», сохранении и печати.")
        : !session_->document().visualEditable
            ? QStringLiteral("Команды сохранены. Доступны правки ZPL и превью; визуальное редактирование этого исходника не поддерживается.")
            : workspacePreferences_.editorLayout == EditorLayout::CodePreview
                ? QStringLiteral("Превью обновляется автоматически при вводе ZPL.")
                : QStringLiteral("Код и дизайн синхронизируются автоматически."));
}

void MainWindow::applyLiveSource() {
    if (workspacePreferences_.editorLayout == EditorLayout::Single || !codePending_) return;
    if (!applySource()) {
        previewWidget_->setPreview({}, session_->document().dpi, sourceStatus_->text());
    }
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
        if (!session_->document().visualEditable || codePending_) return;
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
            if (codePending_ || !session_->document().visualEditable) return;
            if (previewTimer_->isActive()) refreshPreview();
            const auto indices = canvas_->selectedIndices();
            const auto group = indices.isEmpty() ? QUuid{} : session_->document().elements[indices.first()].groupId;
            const bool oneGroup = !group.isNull() && std::ranges::all_of(indices, [this, group](int index) {
                return session_->document().elements[index].groupId == group;
            });
            const auto reference = alignToLabel_ || indices.size() == 1 || oneGroup
                ? AlignmentReference::Label : AlignmentReference::Selection;
            session_->alignElements(indices, alignment, reference, preview_.bounds);
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
            if (codePending_ || !session_->document().visualEditable) return;
            if (canvas_->selectedIndices().size() < 3) { setNotice(QStringLiteral("Для распределения выберите не менее трёх элементов.")); return; }
            if (previewTimer_->isActive()) refreshPreview();
            session_->distributeElements(canvas_->selectedIndices(), axis, preview_.bounds);
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
    sourceSelectionActive_ = false;
    sourceCaretActive_ = false;
    canvas_->setSelectedIndex(-1);
    selected_ = -1; codePending_ = false; session_->reset(document); refreshPreview();
}

void MainWindow::restoreDocument(const DesignDocument& document) {
    sourceTimer_->stop();
    const QSignalBlocker canvasSignals(canvas_);
    canvas_->syncDocument(document);
    selected_ = canvas_->selectedIndex();
    codePending_ = session_->hasPendingSource();
    updating_ = true;
    labelWidth_->setValue(session_->document().width); labelHeight_->setValue(session_->document().height);
    const int dpiIndex = dpi_->findData(session_->document().dpi); dpi_->setCurrentIndex(dpiIndex < 0 ? 0 : dpiIndex);
    canvas_->setEnabled(!codePending_);
    labelWidth_->setEnabled(document.visualEditable && !codePending_); labelHeight_->setEnabled(document.visualEditable && !codePending_); dpi_->setEnabled(document.visualEditable && !codePending_);
    dimensions_->setText(QStringLiteral("%1 × %2 dots · %3 dpi").arg(session_->document().width).arg(session_->document().height).arg(session_->document().dpi));
    // Invalid GS1 fields cannot be serialized. Never expose an incomplete ZPL
    // that could silently discard those elements when applied back to the model.
    const auto sourceValid = document.visualEditable ? validateForExport(document)
        : std::expected<void, QString>{};
    code_->setReadOnly(!sourceValid && !codePending_);
    code_->setPlaceholderText(sourceValid ? QString{} : QStringLiteral(
        "ZPL пока недоступен: %1\nИсправьте данные во вкладке «Дизайн». Все элементы сохранены в макете.").arg(sourceValid.error()));
    auto source = codePending_ ? session_->pendingSource() : sourceValid ? toZpl(document) : QString{};
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source.replace(u'\r', u'\n');
    // Applying live source must not reset the editor's cursor, scroll or undo.
    // Design changes replace the generated text but keep the reader's position.
    if (code_->toPlainText() != source) {
        // A design edit can move field offsets. Keep the selected objects,
        // rather than interpreting the old caret against newly generated ZPL.
        sourceSelectionActive_ = false;
        sourceCaretActive_ = false;
        const int oldPosition = code_->textCursor().position();
        const int oldAnchor = code_->textCursor().anchor();
        const int vertical = code_->verticalScrollBar()->value();
        const int horizontal = code_->horizontalScrollBar()->value();
        code_->setPlainText(source);
        auto cursor = code_->textCursor();
        cursor.setPosition(std::min(oldAnchor, static_cast<int>(source.size())));
        cursor.setPosition(std::min(oldPosition, static_cast<int>(source.size())), QTextCursor::KeepAnchor);
        code_->setTextCursor(cursor);
        code_->verticalScrollBar()->setValue(vertical);
        code_->horizontalScrollBar()->setValue(horizontal);
    }
    mappedSource_ = sourceValid ? toZpl(document) : QString{};
    mappedSource_.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    mappedSource_.replace(u'\r', u'\n');
    mappedSourceRevision_ = codePending_ ? -1 : code_->document()->revision();
    sourceElementRanges_ = sourceElementRanges(document, mappedSource_);
    updating_ = false;
    refreshLayers(); refreshProperties(); updateTitle(); updateHistoryActions(); previewTimer_->start();
    if (sourceSelectionActive_) syncSourceSelection(sourceCaretActive_);
    syncDesignSelection();
    if (codePending_) {
        const bool live = workspacePreferences_.editorLayout != EditorLayout::Single;
        sourceStatus_->setText(live ? QStringLiteral("Обновление после ввода…")
            : QStringLiteral("Восстановлен изменённый ZPL. Перейдите в «Дизайн» или «Превью», чтобы применить его."));
        if (live) sourceTimer_->start();
    } else updateEditorPresentation();
}

void MainWindow::commitDocument(DesignDocument document, const QString& title, int selection) {
    if (codePending_) { setNotice(QStringLiteral("Перейдите в «Дизайн» или «Превью», чтобы применить изменённый ZPL.")); return; }
    session_->commit(std::move(document), title);
    if (selection != -2) selectElement(selection);
}

void MainWindow::refreshPreview() {
    previewTimer_->stop();
    auto rendered = renderPreview(session_->document());
    previewError_.clear();
    printPreview_ = rendered ? *rendered : Preview{};
    const bool differentVisibility = std::ranges::any_of(session_->document().elements,
        [](const Element& element) { return element.editorVisible != element.printable; });
    if (!rendered || differentVisibility) {
        preview_ = {};
        if (!rendered) previewError_ = rendered.error();
        if (auto editor = renderEditorPreview(session_->document())) preview_ = std::move(*editor);
        if (!previewError_.isEmpty()) preview_.diagnostics.append(previewError_);
    } else preview_ = std::move(*rendered);
    canvas_->setDocument(session_->document(), preview_);
    refreshProperties();
    QStringList notes = session_->document().importNotes; notes.append(preview_.diagnostics);
    if (!session_->layoutWarning().isEmpty()) notes.append(session_->layoutWarning());
    notes.removeDuplicates();
    diagnostics_->setPlainText(notes.isEmpty() ? QStringLiteral("Ошибок рендеринга нет.\nПревью построено локально библиотекой QtZpl.") : notes.join(QStringLiteral("\n\n")));
    if (codePending_) return;
    refreshCleanPreview();
    if (!preview_.elementErrors.isEmpty()) setNotice(QStringLiteral(
        "Заполните «Значения плейсхолдеров…» или исправьте данные отмеченных штрихкодов. Печатное превью и PNG пока недоступны."));
    else if (!previewError_.isEmpty()) setNotice(QStringLiteral("Не удалось построить превью: %1").arg(previewError_));
    else if (!session_->document().visualEditable) setNotice(QStringLiteral("Режим исходного ZPL: команды сохранены. Для правок откройте вкладку «Код ZPL»."));
    else if (!notes.isEmpty()) setNotice(QStringLiteral("Превью содержит замечания (%1). Откройте панель «Проверка».").arg(notes.size()));
    else setNotice(QStringLiteral("✓ Превью без ошибок      %1 объектов      Координаты: %2")
        .arg(session_->document().elements.size()).arg(session_->layoutSettings().unit == CanvasUnit::Dots ? QStringLiteral("dots") : QStringLiteral("мм")));
}

void MainWindow::showPreview() {
    if (editorTabs_->currentWidget() != previewWidget_) {
        editorTabs_->setCurrentWidget(previewWidget_);
        return;
    }
    const bool pendingSource = codePending_;
    if (!applySource()) {
        if (workspacePreferences_.editorLayout == EditorLayout::Single) editorTabs_->setCurrentIndex(1);
        else previewWidget_->setPreview({}, session_->document().dpi, sourceStatus_->text());
        return;
    }
    if (!pendingSource) refreshPreview();
    previewWidget_->setFocus();
}

void MainWindow::refreshCleanPreview() {
    if (codePending_) return;
    const auto count = previewPageCount(session_->document());
    const int pages = count ? *count : 1;
    previewPage_ = std::clamp(previewPage_, 0, std::max(0, pages - 1));
    previewWidget_->setPages(pages, previewPage_);
    cleanPreview_ = printPreview_;
    cleanPreviewError_ = previewError_;
    if (previewPage_ > 0) {
        const auto rendered = renderPreviewPage(session_->document(), previewPage_);
        cleanPreview_ = rendered ? *rendered : Preview{};
        cleanPreviewError_ = rendered ? QString{} : rendered.error();
    }
    if (!cleanPreviewError_.isEmpty() && !cleanPreview_.diagnostics.contains(cleanPreviewError_))
        cleanPreview_.diagnostics.append(cleanPreviewError_);
    previewWidget_->setPreview(cleanPreview_, session_->document().dpi, cleanPreviewError_);
    refreshSourceDiagnostics();
}

void MainWindow::refreshSourceDiagnostics() {
    sourceDiagnostics_->clear();
    auto document = session_->document();
    if (codePending_) { document.originalZpl = code_->toPlainText(); document.visualEditable = false; }
    for (const auto& diagnostic : sourceDiagnostics(document, previewPage_)) {
        auto* item = new QListWidgetItem(diagnostic.text, sourceDiagnostics_);
        item->setData(Qt::UserRole, diagnostic.offset);
        if (diagnostic.offset >= 0) item->setToolTip(QStringLiteral("Двойной щелчок — перейти к команде"));
    }
    sourceDiagnostics_->setVisible(sourceDiagnostics_->count() > 0);
}

QSize MainWindow::elementSize(int index) const {
    if (index < 0 || index >= session_->document().elements.size()) return {1, 1};
    const auto& element = session_->document().elements[index];
    const bool renderedSize = element.type == ElementType::Text || isBarcodeType(element.type);
    if (renderedSize && index < preview_.bounds.size() && !preview_.bounds[index].isEmpty())
        return preview_.bounds[index].size();
    return {element.width, element.type == ElementType::Circle ? element.width : element.height};
}

void MainWindow::resizeElement(int index, QSize size) {
    if (codePending_ || !session_->document().visualEditable || index < 0
        || index >= session_->document().elements.size()) return;
    if (preview_.elementErrors.contains(index)) {
        setNotice(QStringLiteral("Для изменения размера штрихкода сначала заполните его данные через «Значения плейсхолдеров…»."));
        refreshProperties();
        return;
    }
    auto next = session_->document();
    const auto& before = session_->document().elements[index];
    if (before.locked) return;
    auto& resized = next.elements[index];
    auto effective = before;
    if (effective.useDefaultOrientation) effective.orientation = next.defaultOrientation;
    resized = resizedElement(effective, size, elementSize(index));
    resized.orientation = before.orientation;
    if (resized.width == before.width && resized.height == before.height
        && resized.fontWidth == before.fontWidth && resized.fontHeight == before.fontHeight
        && resized.blockWidth == before.blockWidth && resized.blockLineSpacing == before.blockLineSpacing
        && resized.blockHangingIndent == before.blockHangingIndent
        && resized.moduleWidth == before.moduleWidth && resized.pdf417RowHeight == before.pdf417RowHeight
        && resized.tableColumnWidths == before.tableColumnWidths && resized.tableRowHeights == before.tableRowHeights) {
        refreshProperties();
        return;
    }
    next.originalZpl.clear();
    if (const auto valid = validateForExport(next); !valid && !renderEditorPreview(next)) {
        setNotice(QStringLiteral("Размер не изменён: %1").arg(valid.error()));
        refreshProperties();
        return;
    }
    commitDocument(std::move(next), QStringLiteral("Размер элемента"), index);
    // Reflect rounded font/module dimensions immediately, including subsequent gestures.
    refreshPreview();
}

void MainWindow::selectElement(int index) {
    canvas_->setSelectedIndex(index);
    syncSelection();
}

void MainWindow::syncSelection() {
    selected_ = canvas_->selectedIndex();
    const QSignalBlocker blocker(layers_);
    const auto indices = canvas_->selectedIndices();
    layers_->setFixedHeight(std::min(370, int(session_->document().elements.size()) * 37 + 8));
    layerCount_->setText(QStringLiteral("%1 объектов").arg(session_->document().elements.size()));
    layers_->setCurrentRow(selected_ < 0 ? -1 : static_cast<int>(session_->document().elements.size()) - selected_ - 1,
                           QItemSelectionModel::NoUpdate);
    layers_->clearSelection();
    for (int row = 0; row < layers_->count(); ++row) {
        auto* item = layers_->item(row);
        item->setSelected(indices.contains(item->data(Qt::UserRole).toInt()));
    }
    refreshProperties();
    syncDesignSelection();
}

bool MainWindow::sourceMatchesDocument() {
    if (codePending_ || !session_->document().visualEditable) return false;
    const int revision = code_->document()->revision();
    if (revision != mappedSourceRevision_) {
        // Syntax highlighting also advances QTextDocument's revision. A palette
        // change must not invalidate the mapping when the source is identical.
        if (code_->toPlainText() != mappedSource_) return false;
        mappedSourceRevision_ = revision;
    }
    return true;
}

void MainWindow::syncDesignSelection(bool reveal) {
    QList<QPair<int, int>> ranges;
    if (workspacePreferences_.selectionSync && sourceMatchesDocument()) {
        const auto indices = canvas_->selectedIndices();
        for (const auto& range : sourceElementRanges_) {
            if (indices.contains(range.elementIndex))
                ranges.append({static_cast<int>(range.start), static_cast<int>(range.length)});
        }
    }
    static_cast<ZplSourceEditor*>(code_)->setObjectHighlights(ranges, reveal && code_->isVisible());
}

void MainWindow::syncSourceSelection(bool allowCaret) {
    if (updating_) return;
    if (!workspacePreferences_.selectionSync) {
        sourceSelectionActive_ = false;
        sourceCaretActive_ = false;
        return;
    }
    const QScopedValueRollback selectionGuard(syncingSourceSelection_, true);
    const auto cursor = code_->textCursor();
    const bool selectCaret = allowCaret && !cursor.hasSelection();
    // Wait for this exact source to be applied. The last valid design keeps its
    // own selection while pending or unsupported code is being inspected.
    if (!sourceMatchesDocument()) {
        sourceSelectionActive_ = cursor.hasSelection() || selectCaret;
        sourceCaretActive_ = selectCaret;
        return;
    }
    if (!cursor.hasSelection() && !selectCaret) {
        if (sourceSelectionActive_) {
            sourceSelectionActive_ = false;
            sourceCaretActive_ = false;
            canvas_->setSelectedIndices({});
            if (canvas_->signalsBlocked()) syncSelection();
        }
        return;
    }
    sourceSelectionActive_ = true;
    sourceCaretActive_ = selectCaret;
    QList<int> indices;
    if (selectCaret) {
        // Prefer the exact field when several objects share a physical line.
        for (const auto& range : sourceElementRanges_) {
            if (cursor.position() >= range.start && cursor.position() < range.start + range.length) {
                indices.append(range.elementIndex);
                break;
            }
        }
    }
    if (!selectCaret || indices.isEmpty()) {
        // A click in trailing whitespace still activates the object's line.
        // Range selections retain their original half-open intersection rules.
        const auto block = cursor.block();
        const auto start = selectCaret ? block.position() : cursor.selectionStart();
        const auto end = selectCaret ? block.position() + block.length() - 1 : cursor.selectionEnd();
        for (const auto& range : sourceElementRanges_) {
            if (range.start < end && range.start + range.length > start
                && !indices.contains(range.elementIndex))
                indices.append(range.elementIndex);
        }
    }
    canvas_->setSelectedIndices(indices);
    // restoreDocument can temporarily block canvas signals while applying ZPL.
    if (canvas_->signalsBlocked()) syncSelection();
}

void MainWindow::refreshLayers() {
    const QSignalBlocker blocker(layers_); layers_->clear();
    for (qsizetype i = session_->document().elements.size(); i > 0; --i) {
        const auto& e = session_->document().elements[i - 1];
        auto* item = new QListWidgetItem(elementIcon(e.type), (e.locked ? QStringLiteral("🔒 ") : QString{})
            + (!e.editorVisible ? QStringLiteral("◌ ") : QString{}) + (!e.printable ? QStringLiteral("⊘ ") : QString{})
            + (e.groupId.isNull() ? QString{} : QStringLiteral("▣ "))
            + (e.name.isEmpty() ? elementTypeName(e.type) : e.name), layers_);
        item->setToolTip(QStringLiteral("%1\nНа холсте: %2 · Печать: %3 · %4")
            .arg(elementTypeName(e.type), e.editorVisible ? QStringLiteral("да") : QStringLiteral("нет"),
                e.printable ? QStringLiteral("да") : QStringLiteral("нет"),
                e.locked ? QStringLiteral("заблокирован") : QStringLiteral("доступен для правок")));
        item->setData(Qt::UserRole, static_cast<int>(i - 1));
        item->setData(Qt::UserRole + 1, e.editorVisible ? QStringLiteral("виден") : QStringLiteral("скрыт"));
        item->setSelected(canvas_->selectedIndices().contains(static_cast<int>(i - 1)));
    }
    layers_->setMaximumHeight(std::min(370, int(session_->document().elements.size()) * 37 + 2));
    layerCount_->setText(QStringLiteral("%1 объектов").arg(session_->document().elements.size()));
    layers_->setCurrentRow(selected_ < 0 ? -1 : static_cast<int>(session_->document().elements.size()) - selected_ - 1,
                           QItemSelectionModel::NoUpdate);
}

void MainWindow::refreshProperties() {
    updating_ = true;
    refreshFocusSummary();
    barcodeType_->setEnabled(!codePending_);
    refreshPropertyUnits();
    const auto indices = canvas_->selectedIndices();
    for (auto* check : {layerLocked_, layerVisible_, layerPrintable_}) {
        check->setEnabled(!indices.isEmpty() && session_->document().visualEditable && !codePending_);
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
    const bool multi = canvas_->selectedIndices().size() > 1;
    const bool valid = !multi && session_->document().visualEditable && selected_ >= 0 && selected_ < session_->document().elements.size();
    elementForm_->setEnabled(valid && !selectionLocked() && !codePending_);
    elementForm_->setVisible(valid);
    selectionProperties_->setVisible(multi && session_->document().visualEditable);
    selectionProperties_->setEnabled(!selectionLocked() && !codePending_);
    if (multi) selectionProperties_->setSelection(session_->document(), canvas_->selectedIndices());
    typeTitle_->setText(multi ? QStringLiteral("Выбрано элементов: %1\nМеню «Компоновка» — выравнивание").arg(canvas_->selectedIndices().size())
        : valid ? (session_->document().elements[selected_].name.isEmpty() ? elementTypeName(session_->document().elements[selected_].type) : session_->document().elements[selected_].name) : QStringLiteral("Выберите элемент"));
    objectSubtitle_->setText(valid ? QStringLiteral("%1 · %2 из %3").arg(elementTypeName(session_->document().elements[selected_].type)).arg(session_->document().elements.size() - selected_).arg(session_->document().elements.size()) : QString());
    objectSymbol_->setText(valid ? elementSymbol(session_->document().elements[selected_].type) : QStringLiteral("↖"));
    if (valid) {
        const auto& e = session_->document().elements[selected_];
        const QSize size = elementSize(selected_);
        x_->setValue(e.x); y_->setValue(e.y); width_->setValue(size.width()); height_->setValue(size.height());
        if (content_->toPlainText() != e.text) content_->setPlainText(e.text);
        content_->setFixedHeight(e.text.contains(u'\n') || e.blockWidth > 0 ? 90 : 30);
        fontHeight_->setValue(e.fontHeight); fontWidth_->setValue(e.fontWidth); thickness_->setValue(e.thickness);
        module_->setValue(e.moduleWidth); threshold_->setValue(e.threshold);
        orientation_->setCurrentIndex(orientation_->findData(QString(e.orientation)));
        readable_->setChecked(e.humanReadable); reverse_->setChecked(e.reverse); dither_->setChecked(e.dither);
        whiteGraphic_->setChecked(e.graphicWhite);
        diagonalDirection_->setCurrentIndex(diagonalDirection_->findData(QString(e.diagonalDirection)));
        imageKeepAspectRatio_->setChecked(e.imageKeepAspectRatio);
        gs1_->setChecked(e.gs1);
        blockWidth_->setValue(e.blockWidth); blockLines_->setValue(e.blockMaxLines);
        blockSpacing_->setValue(e.blockLineSpacing); blockIndent_->setValue(e.blockHangingIndent);
        blockAlignment_->setCurrentIndex(blockAlignment_->findData(QString(e.blockJustification)));
        code128Mode_->setCurrentIndex(code128Mode_->findData(QString(e.code128Mode)));
        qrCorrection_->setCurrentIndex(qrCorrection_->findData(QString(e.qrErrorCorrection)));
        pdfRowHeight_->setValue(e.pdf417RowHeight); pdfColumns_->setValue(e.pdf417Columns); pdfSecurity_->setValue(e.pdf417Security);
        interpretationAbove_->setChecked(e.interpretationAbove);
        const bool text = e.type == ElementType::Text;
        const bool code = isBarcodeType(e.type);
        const bool table = e.type == ElementType::Table;
        const bool image = e.type == ElementType::Image;
        geometryAspect_->setEnabled(image && !selectionLocked() && !codePending_);
        geometryAspect_->setChecked(image ? e.imageKeepAspectRatio
            : e.type == ElementType::Circle || e.type == ElementType::QrCode || e.type == ElementType::DataMatrix);
        auto* form = elementForm_->findChild<QFormLayout*>(QStringLiteral("detailForm"));
        setPropertyRowVisible(form, barcodeType_, code);
        if (code) {
            const QSignalBlocker blocker(barcodeType_);
            const auto types = barcodeTypes(e.type);
            if (barcodeType_->count() != static_cast<int>(types.size())
                || barcodeType_->itemData(0).toInt() != static_cast<int>(types.front())) {
                barcodeType_->clear();
                for (const auto type : types) barcodeType_->addItem(elementTypeName(type), static_cast<int>(type));
            }
            barcodeType_->setCurrentIndex(barcodeType_->findData(static_cast<int>(e.type)));
        }
        contentSection_->setVisible(!image && !table);
        setPropertyRowVisible(form, content_, text || code);
        const bool dataMatrix = e.type == ElementType::DataMatrix;
        setPropertyRowVisible(form, gs1_, dataMatrix); setPropertyRowVisible(form, gs1Builder_, dataMatrix);
        setPropertyRowVisible(form, placeholderValues_, table);
        setPropertyRowVisible(form, gs1Status_, false);
        content_->setPlaceholderText(dataMatrix ? (e.gs1 ? QStringLiteral("(01){{01}}(21){{21}} или 01{{01}}21{{21}}{{GS}}91{{91}}")
                                                                        : QStringLiteral("{{21}} или PREFIX-[[serial]]")) : QString());
        if (text || code) {
            const auto names = e.literalText ? std::expected<QStringList, QString>(QStringList{}) : requiredValueParameters(e.text);
            const auto gs1Data = dataMatrix && e.gs1 ? parseGs1Template(e.text) : std::expected<Gs1Data, QString>(Gs1Data{});
            setPropertyRowVisible(form, placeholderValues_, !names || !names->isEmpty());
            setPropertyRowVisible(form, gs1Status_, !names || !gs1Data || preview_.elementErrors.contains(selected_));
            if (!names) gs1Status_->setText(names.error());
            else if (!gs1Data) gs1Status_->setText(QStringLiteral("GS1 включён. %1\nИсправьте данные или используйте «Мастер GS1…».").arg(gs1Data.error()));
            else if (preview_.elementErrors.contains(selected_)) gs1Status_->setText(preview_.elementErrors.value(selected_));
            else if (!names->isEmpty()) gs1Status_->setText(QStringLiteral("Плейсхолдеры: %1. Задайте «Значения плейсхолдеров…» для превью. Сохранение оставляет шаблон.").arg(names->join(QStringLiteral(", "))));
            else gs1Status_->setText(e.gs1 ? QStringLiteral("Поля GS1 корректны. FNC1 и разделители добавляются автоматически.")
                                          : QStringLiteral("В данных можно использовать {{имя}} и [[имя]]."));
        }
        setPropertyRowVisible(form, fontHeight_, text); setPropertyRowVisible(form, fontWidth_, text);
        setPropertyRowVisible(form, blockWidth_, text);
        for (QWidget* widget : {static_cast<QWidget*>(blockLines_), static_cast<QWidget*>(blockSpacing_),
                               static_cast<QWidget*>(blockIndent_), static_cast<QWidget*>(blockAlignment_)})
            setPropertyRowVisible(form, widget, text && e.blockWidth > 0);
        setPropertyRowVisible(form, code128Mode_, e.type == ElementType::Code128);
        setPropertyRowVisible(form, interpretationAbove_, isLinearBarcodeType(e.type) && e.humanReadable
            && e.type != ElementType::Codabar && e.type != ElementType::Code39);
        setPropertyRowVisible(form, thickness_, !text && !code && !image && !table);
        setPropertyRowVisible(form, whiteGraphic_, !text && !code && !image && !table);
        setPropertyRowVisible(form, diagonalDirection_, e.type == ElementType::Diagonal);
        setPropertyRowVisible(form, reverse_, !table);
        setPropertyRowVisible(form, module_, code && e.type != ElementType::MaxiCode);
        setPropertyRowVisible(form, orientation_, (text || code) && e.type != ElementType::MaxiCode);
        orientation_->setEnabled(!e.useDefaultOrientation);
        setPropertyRowVisible(form, readable_, isLinearBarcodeType(e.type)
            && e.type != ElementType::Codabar && e.type != ElementType::Code39);
        setPropertyRowVisible(form, qrCorrection_, e.type == ElementType::QrCode);
        setPropertyRowVisible(form, qrPayload_, e.type == ElementType::QrCode);
        setPropertyRowVisible(form, tableEdit_, table);
        setPropertyRowVisible(form, barcodeOptions_, code);
        setPropertyRowVisible(form, advancedText_, text);
        for (auto* field : {pdfRowHeight_, pdfColumns_, pdfSecurity_}) setPropertyRowVisible(form, field, e.type == ElementType::Pdf417);
        setPropertyRowVisible(form, threshold_, image); setPropertyRowVisible(form, dither_, image);
        setPropertyRowVisible(form, imageKeepAspectRatio_, image); setPropertyRowVisible(form, imageEdit_, image);
        setPropertyRowVisible(form, width_, true);
        setPropertyRowVisible(form, height_, true);
        width_->setEnabled(e.type != ElementType::MaxiCode);
        height_->setEnabled(e.type != ElementType::MaxiCode);
        sizeHint_->setText(text ? (e.blockWidth > 0
            ? QStringLiteral("Размер меняет шрифт и текстовый блок. Точная ширина блока задаётся ниже.")
            : QStringLiteral("Размер меняет шрифт. Фактические размеры округляются до точек."))
            : code ? QStringLiteral("Модуль: %1 мм. Размер меняется целыми модулями; пропорции QR и DataMatrix сохраняются.")
                .arg(e.moduleWidth * 25.4 / session_->document().dpi, 0, 'f', 3)
            : e.type == ElementType::Circle ? QStringLiteral("Ширина и высота задают один диаметр.")
            : QStringLiteral("Меняйте размеры здесь или тяните маркеры рамки на холсте."));
        sizeHint_->setText(sizeHint_->text() + QStringLiteral("\nФактически: %1 × %2 dots = %3 × %4 мм.")
            .arg(size.width()).arg(size.height()).arg(size.width() * 25.4 / session_->document().dpi, 0, 'f', 3)
            .arg(size.height() * 25.4 / session_->document().dpi, 0, 'f', 3));
    }
    updating_ = false;
}

void MainWindow::editBarcodeType() {
    if (updating_ || codePending_ || selectionLocked() || canvas_->selectedIndices().size() != 1
        || !session_->document().visualEditable || selected_ < 0
        || selected_ >= session_->document().elements.size() || barcodeType_->currentIndex() < 0) return;
    const auto& before = session_->document().elements[selected_];
    const auto type = static_cast<ElementType>(barcodeType_->currentData().toInt());
    if (type == before.type || !std::ranges::contains(barcodeTypes(before.type), type)) return;
    auto next = session_->document();
    auto& element = next.elements[selected_];
    if (element.name == elementTypeName(element.type)) element.name = elementTypeName(type);
    element.type = type;
    // Keep payload, placement and imported source tokens; incompatible data is
    // diagnosed by the normal preview so it can be edited after changing type.
    if (type != ElementType::DataMatrix) element.gs1 = false;
    if (type == ElementType::Code39 || type == ElementType::Codabar) {
        element.humanReadable = false;
        element.interpretationAbove = false;
    }
    if (type == ElementType::MaxiCode) {
        element.orientation = u'N';
        element.useDefaultOrientation = false;
        element.width = 201;
        element.height = 195;
    }
    commitDocument(std::move(next), QStringLiteral("Тип штрихкода"));
}

void MainWindow::editProperties() {
    if (updating_ || selectionLocked() || canvas_->selectedIndices().size() != 1 || !session_->document().visualEditable || selected_ < 0 || selected_ >= session_->document().elements.size()) return;
    auto next = session_->document(); auto& e = next.elements[selected_];
    e.x = x_->value(); e.y = y_->value();
    if (e.text != content_->toPlainText()) e.literalText = false;
    e.text = content_->toPlainText(); e.fontHeight = fontHeight_->value(); e.fontWidth = fontWidth_->value();
    e.thickness = thickness_->value(); e.moduleWidth = module_->value(); e.threshold = threshold_->value();
    e.orientation = orientation_->currentData().toString().at(0);
    e.humanReadable = readable_->isChecked(); e.reverse = reverse_->isChecked(); e.dither = dither_->isChecked();
    e.graphicWhite = whiteGraphic_->isChecked();
    e.diagonalDirection = diagonalDirection_->currentData().toString().at(0);
    e.imageKeepAspectRatio = imageKeepAspectRatio_->isChecked();
    e.gs1 = e.type == ElementType::DataMatrix && gs1_->isChecked();
    e.blockWidth = blockWidth_->value(); e.blockMaxLines = blockLines_->value();
    e.blockLineSpacing = blockSpacing_->value(); e.blockHangingIndent = blockIndent_->value();
    e.blockJustification = blockAlignment_->currentData().toString().at(0);
    e.code128Mode = code128Mode_->currentData().toString().at(0);
    e.interpretationAbove = interpretationAbove_->isChecked();
    e.qrErrorCorrection = qrCorrection_->currentData().toString().at(0);
    e.pdf417RowHeight = pdfRowHeight_->value(); e.pdf417Columns = pdfColumns_->value(); e.pdf417Security = pdfSecurity_->value();
    commitDocument(std::move(next), QStringLiteral("Изменить свойства"));
}

void MainWindow::copyElements(bool cut) {
    if (codePending_ || !session_->document().visualEditable) return;
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
    if (codePending_ || !session_->document().visualEditable) return;
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
    if (codePending_ || !session_->document().visualEditable) return;
    const auto indices = canvas_->selectedIndices();
    const bool changed = ungroup ? session_->ungroupElements(indices) : session_->groupElements(indices);
    if (changed) canvas_->setSelectedIndices(indices);
    else setNotice(ungroup ? QStringLiteral("Выберите группу для разгруппировки.")
                          : QStringLiteral("Для группы выберите минимум два объекта."));
}

void MainWindow::editImage() {
    if (codePending_ || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (element.type != ElementType::Image) return;
    ImageEditDialog dialog(element, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto edited = dialog.editedElement();
    if (edited.image == element.image && edited.width == element.width && edited.height == element.height) return;
    auto next = session_->document();
    next.elements[selected_] = std::move(edited);
    commitDocument(std::move(next), QStringLiteral("Изменить изображение"));
}

void MainWindow::editTable() {
    if (codePending_ || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (element.type != ElementType::Table) return;
    TableDialog dialog(element, session_->layoutSettings().unit, session_->document().dpi, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto next = session_->document(); next.elements[selected_] = dialog.editedElement();
    if (next.elements[selected_].tableCells != element.tableCells) next.elements[selected_].literalText = false;
    commitDocument(std::move(next), QStringLiteral("Изменить таблицу"));
}

void MainWindow::editQrPayload() {
    if (codePending_ || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (element.type != ElementType::QrCode) return;
    QrPayloadDialog dialog(element.text, this);
    if (dialog.exec() != QDialog::Accepted || dialog.payload() == element.text) return;
    auto next = session_->document(); next.elements[selected_].text = dialog.payload();
    next.elements[selected_].literalText = false;
    commitDocument(std::move(next), QStringLiteral("Содержимое QR"));
}

void MainWindow::editBarcodeOptions() {
    if (codePending_ || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (!isBarcodeType(element.type)) return;
    BarcodeOptionsDialog dialog(element, session_->layoutSettings().unit, session_->document().dpi, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto next = session_->document(); next.elements[selected_] = dialog.editedElement();
    commitDocument(std::move(next), QStringLiteral("Параметры штрихкода"));
}

void MainWindow::editAdvancedText() {
    if (codePending_ || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    const auto& element = session_->document().elements[selected_];
    if (element.type != ElementType::Text) return;
    AdvancedTextDialog dialog(element, session_->layoutSettings().unit, session_->document().dpi, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto next = session_->document(); next.elements[selected_] = dialog.editedElement();
    commitDocument(std::move(next), QStringLiteral("Положение текста"));
}

void MainWindow::editLabelSettings() {
    if (!applySource() || !session_->document().visualEditable) return;
    LabelSettingsDialog dialog(session_->document(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    commitDocument(dialog.editedDocument(), QStringLiteral("Преобразования этикетки"));
}

void MainWindow::refreshPropertyUnits() {
    const auto unit = session_->layoutSettings().unit;
    const int dpi = session_->document().dpi;
    const QSignalBlocker blocker(propertyUnits_);
    propertyUnits_->setCurrentIndex(propertyUnits_->findData(static_cast<int>(unit)));
    for (auto* control : elementForm_->findChildren<DotSpinBox*>()) {
        const bool contentDots = control == fontHeight_ || control == module_ || control == thickness_;
        control->setDisplayUnits(contentDots ? CanvasUnit::Dots : unit, dpi);
    }
    selectionProperties_->setDisplayUnits(unit, dpi);
}

bool MainWindow::selectionLocked() const {
    return std::ranges::any_of(canvas_->selectedIndices(), [this](int index) { return session_->document().elements[index].locked; });
}

void MainWindow::renameLayer() {
    if (codePending_ || selectionLocked() || canvas_->selectedIndices().size() != 1 || selected_ < 0) return;
    bool accepted = false;
    const auto& element = session_->document().elements[selected_];
    const auto name = QInputDialog::getText(this, QStringLiteral("Название слоя"), QStringLiteral("Название:"),
        QLineEdit::Normal, element.name.isEmpty() ? elementTypeName(element.type) : element.name, &accepted).trimmed();
    if (!accepted || name == element.name) return;
    if (name.size() > 256) { setNotice(QStringLiteral("Название слоя — не более 256 символов.")); return; }
    auto next = session_->document(); next.elements[selected_].name = name;
    commitDocument(std::move(next), QStringLiteral("Название слоя"));
}

void MainWindow::setLayerFlag(const QString& flag, bool value) {
    if (updating_ || codePending_ || !session_->document().visualEditable) return;
    const auto indices = canvas_->selectedIndices();
    if (indices.isEmpty()) return;
    // Lock/unlock is always available; printing is a design change protected by the lock.
    if (flag == QStringLiteral("printable") && selectionLocked()) {
        setNotice(QStringLiteral("Сначала разблокируйте выбранные слои.")); refreshProperties(); return;
    }
    auto next = session_->document();
    for (int index : indices) {
        auto& element = next.elements[index];
        if (flag == QStringLiteral("locked")) element.locked = value;
        else if (flag == QStringLiteral("visible")) element.editorVisible = value;
        else if (flag == QStringLiteral("printable")) element.printable = value;
    }
    commitDocument(std::move(next), QStringLiteral("Параметры слоёв"));
}

void MainWindow::addElement(ElementType type, QPoint position) {
    if (!session_->document().visualEditable) { setNotice(QStringLiteral("Этот ZPL редактируется во вкладке кода. Создайте новую этикетку для визуального дизайна.")); return; }
    if (session_->document().elements.size() >= 200) { setNotice(QStringLiteral("На этикетке уже 200 элементов — предел текущей версии.")); return; }
    if (type == ElementType::Image) { chooseImage(position); return; }
    auto next = session_->document();
    next.elements.append(makeElement(type, position));
    commitDocument(std::move(next), QStringLiteral("Добавить элемент"), static_cast<int>(session_->document().elements.size()));
}

bool MainWindow::importImage(const QString& path, QPoint position) {
    if (!session_->document().visualEditable) { setNotice(QStringLiteral("Импорт изображения доступен в визуальном режиме.")); return false; }
    if (session_->document().elements.size() >= 200) { setNotice(QStringLiteral("На этикетке уже 200 элементов.")); return false; }
    auto image = loadImage(path);
    if (!image) { setNotice(image.error()); return false; }
    auto next = session_->document(); auto element = makeElement(ElementType::Image, position);
    element.name = QFileInfo(path).fileName(); element.image = *image;
    const QSize size = image->size().scaled(QSize(std::max(1, session_->document().width - position.x()), std::max(1, session_->document().height - position.y())), Qt::KeepAspectRatio);
    element.width = std::min(image->width(), size.width()); element.height = std::min(image->height(), size.height());
    next.elements.append(std::move(element));
    commitDocument(std::move(next), QStringLiteral("Импортировать изображение"), static_cast<int>(session_->document().elements.size()));
    return true;
}

void MainWindow::chooseImage(QPoint position) {
    if (!session_->document().visualEditable) { setNotice(QStringLiteral("Создайте визуальную этикетку для импорта изображения.")); return; }
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Импортировать картинку"), {}, QStringLiteral("Изображения (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;Все файлы (*)"));
    if (!path.isEmpty()) importImage(path, position);
}

void MainWindow::duplicateElement() {
    if (codePending_ || !session_->document().visualEditable || selected_ < 0) return;
    const auto indices = canvas_->selectedIndices();
    if (session_->document().elements.size() + indices.size() > 200) { setNotice(QStringLiteral("После дублирования будет превышен предел 200 элементов.")); return; }
    const auto copies = session_->duplicateElements(indices);
    if (!copies.isEmpty()) canvas_->setSelectedIndices(copies);
}
void MainWindow::deleteElement() {
    if (!codePending_) session_->deleteElements(canvas_->selectedIndices());
}
void MainWindow::moveLayer(int direction) {
    if (codePending_ || selectionLocked() || !session_->document().visualEditable || selected_ < 0) return;
    auto next = session_->document();
    QSet<QUuid> selectedIds;
    for (int index : canvas_->selectedIndices()) selectedIds.insert(next.elements[index].id);
    if (std::abs(direction) >= 200) {
        QList<Element> selected, other;
        for (const auto& element : next.elements) (selectedIds.contains(element.id) ? selected : other).append(element);
        next.elements = direction > 0 ? other + selected : selected + other;
    } else if (direction > 0) {
        for (int i = int(next.elements.size()) - 2; i >= 0; --i)
            if (selectedIds.contains(next.elements[i].id) && !selectedIds.contains(next.elements[i + 1].id)) next.elements.swapItemsAt(i, i + 1);
    } else {
        for (int i = 1; i < next.elements.size(); ++i)
            if (selectedIds.contains(next.elements[i].id) && !selectedIds.contains(next.elements[i - 1].id)) next.elements.swapItemsAt(i, i - 1);
    }
    commitDocument(std::move(next), QStringLiteral("Порядок слоёв"));
}

void MainWindow::showGs1Builder() {
    if (codePending_ || selectionLocked() || !session_->document().visualEditable || selected_ < 0
        || canvas_->selectedIndices().size() != 1) return;
    if (session_->document().elements[selected_].type != ElementType::DataMatrix) return;
    Gs1Dialog dialog(session_->document().elements[selected_].text, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto next = session_->document();
    next.elements[selected_].gs1 = true;
    next.elements[selected_].text = dialog.data();
    commitDocument(std::move(next), QStringLiteral("Настроить GS1 DataMatrix"));
}

bool MainWindow::showPlaceholderValues() {
    if (codePending_ || !session_->document().visualEditable) return false;
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
    if (codePending_ || !session_->document().visualEditable) {
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
    auto next = session_->document(); next.dpi = resolution->currentData().toInt();
    next.width = qRound(width->value() * next.dpi / 25.4); next.height = qRound(height->value() * next.dpi / 25.4);
    if (next.width == session_->document().width && next.height == session_->document().height && next.dpi == session_->document().dpi) return;
    commitDocument(std::move(next), QStringLiteral("Формат этикетки в миллиметрах"));
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
    CanvasLayoutDialog dialog(session_->layoutSettings(),
        {session_->document().width, session_->document().height}, session_->document().dpi, this);
    if (dialog.exec() == QDialog::Accepted)
        session_->setLayoutSettings(dialog.settings(), QStringLiteral("Настройки холста"));
}

bool MainWindow::applySource() {
    sourceTimer_->stop();
    if (!codePending_) return true;
    auto imported = fromZpl(code_->toPlainText());
    if (!imported) {
        sourceStatus_->setText(QStringLiteral("ZPL не применён: %1 Последний применённый макет сохранён.").arg(imported.error()));
        setNotice(sourceStatus_->text());
        refreshSourceDiagnostics();
        return false;
    }
    imported->dpi = session_->document().dpi;
    codePending_ = false;
    session_->clearPendingSource();
    selected_ = -1;
    session_->commit(std::move(*imported), QStringLiteral("Применить ZPL"), true);
    refreshPreview(); return true;
}

bool MainWindow::openFile(const QString& path) {
    auto result = session_->open(path);
    if (!result) { setNotice(result.error()); return false; }
    selectElement(-1); codePending_ = session_->hasPendingSource(); refreshPreview(); restoreCanvasZoom(); updateTitle();
    if (codePending_) { restoreDocument(session_->document()); editorTabs_->setCurrentIndex(1); }
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
        notes.append(preview_.diagnostics); notes.removeDuplicates();
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
    setDocument(DesignDocument{}); restoreCanvasZoom();
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
    const bool pendingSource = codePending_;
    if (!applySource()) return;
    if (!pendingSource) refreshPreview();
    if (!cleanPreviewError_.isEmpty() || cleanPreview_.image.isNull()) return;
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить превью"), QStringLiteral("label.png"), QStringLiteral("PNG (*.png)"));
    if (!path.isEmpty() && !cleanPreview_.image.save(path, "PNG")) setNotice(QStringLiteral("Не удалось сохранить PNG."));
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
    undoAction_->setEnabled(!codePending_ && undo_->canUndo());
    redoAction_->setEnabled(!codePending_ && undo_->canRedo());
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
    return !preview_.image.isNull() && preview_.image.save(QDir(directory).filePath(QStringLiteral("smoke-label.png")));
}
} // namespace Designer
