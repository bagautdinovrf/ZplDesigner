#include "designer_theme.hpp"
#include "document_session.hpp"
#include "label_canvas.hpp"
#include "main_window.hpp"
#include "workspace_preferences.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextCursor>
#include <QUuid>
#include <QtTest>
#include <QtMath>

#include <memory>

using namespace Designer;
using namespace Qt::StringLiterals;

class WorkspaceTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void focusGivesSpaceToCanvasAndRestoresBalance();
    void panelsCanBeReopenedWithoutLeavingFocus();
    void focusPropertiesAvoidSourceAndPreview();
    void modeSwitchPreservesPendingSourceAndHistory();
    void themeChangesColorsWithoutRebuildingEditors();
    void themeAndModePreservePrinterGeometryAndWhitePaper();
    void preferencesRestoreAcrossWindows();
    void toolbarPreferencesDefaultVisibleAndPersistHiddenCommands();
    void optionalWorkspaceScreenshots();

private:
    QAction* action(const char* name) const;
    void createWindow();
    std::unique_ptr<MainWindow> window_;
    DesignDocument initial_;
    QString previousName_;
    QString settingsDirectory_;
    bool previousTestMode_ = false;
};

QAction* WorkspaceTests::action(const char* name) const
{
    return window_->findChild<QAction*>(QString::fromLatin1(name));
}

void WorkspaceTests::createWindow()
{
    window_ = std::make_unique<MainWindow>();
    window_->setDocument(initial_);
    window_->resize(1360, 850);
    window_->show();
    QApplication::processEvents();
}

void WorkspaceTests::init()
{
    previousName_ = QCoreApplication::applicationName();
    previousTestMode_ = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"workspace-tests-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    settingsDirectory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    installDesignerStyle();
    setDesignerTheme(DesignerTheme::Light);
    initial_ = {};
    initial_.width = 400;
    initial_.height = 300;
    auto box = makeElement(ElementType::Box, {40, 48});
    box.width = 80;
    box.height = 56;
    initial_.elements = {box};
    createWindow();
    for (const auto* name : {"workspaceBalance", "workspaceFocus", "workspaceProperties",
                            "workspaceLayers", "workspaceDiagnostics", "darkTheme"}) {
        QVERIFY2(action(name), name);
        QVERIFY2(action(name)->isCheckable(), name);
    }
}

void WorkspaceTests::cleanup()
{
    if (window_) {
        window_->setDocument(initial_);
        window_.reset();
    }
    QFile::remove(QDir(settingsDirectory_).filePath(u"workspace.ini"_s));
    QDir().rmdir(settingsDirectory_);
    QCoreApplication::setApplicationName(previousName_);
    QStandardPaths::setTestModeEnabled(previousTestMode_);
    setDesignerTheme(DesignerTheme::Light);
}

void WorkspaceTests::focusGivesSpaceToCanvasAndRestoresBalance()
{
    auto* splitter = window_->findChild<QSplitter*>(u"workspaceSplitter"_s);
    auto* layers = window_->findChild<QWidget*>(u"layersPanel"_s);
    auto* inspector = window_->findChild<QTabWidget*>(u"inspectorTabs"_s);
    auto* palette = window_->findChild<QListWidget*>(u"elementPalette"_s);
    auto* canvas = window_->findChild<LabelCanvas*>();
    QVERIFY(splitter); QVERIFY(layers); QVERIFY(inspector); QVERIFY(palette); QVERIFY(canvas);
    QCOMPARE(splitter->count(), 3);
    QCOMPARE(splitter->widget(0), layers);
    QVERIFY(splitter->widget(2) == inspector || splitter->widget(2)->isAncestorOf(inspector));
    QVERIFY(action("workspaceBalance")->isChecked());
    QVERIFY(!action("workspaceFocus")->isChecked());
    QVERIFY(layers->isVisible());
    QVERIFY(inspector->isVisible());
    QVERIFY(palette->isVisible());
    splitter->setSizes({225, 670, 315});
    QApplication::processEvents();
    const auto sizes = splitter->sizes();
    const auto canvasWidth = canvas->width();

    action("workspaceFocus")->trigger();
    QApplication::processEvents();
    QVERIFY(action("workspaceFocus")->isChecked());
    QVERIFY(!action("workspaceBalance")->isChecked());
    QVERIFY(!layers->isVisible());
    QVERIFY(!inspector->isVisible());
    QVERIFY(palette->isVisible());
    QVERIFY(canvas->width() > canvasWidth + 300);

    action("workspaceBalance")->trigger();
    QApplication::processEvents();
    QVERIFY(layers->isVisible());
    QVERIFY(inspector->isVisible());
    QCOMPARE(splitter->sizes(), sizes);
    QCOMPARE(canvas->width(), canvasWidth);
}

void WorkspaceTests::panelsCanBeReopenedWithoutLeavingFocus()
{
    auto* layers = window_->findChild<QWidget*>(u"layersPanel"_s);
    auto* inspector = window_->findChild<QTabWidget*>(u"inspectorTabs"_s);
    auto* diagnostics = window_->findChild<QWidget*>(u"workspaceDiagnosticsPanel"_s);
    auto* canvas = window_->findChild<LabelCanvas*>();
    QVERIFY(layers); QVERIFY(inspector); QVERIFY(diagnostics); QVERIFY(canvas);
    // Keep a deliberately asymmetric Balance layout before entering Focus.
    action("workspaceLayers")->trigger();
    QVERIFY(!layers->isVisible());
    QVERIFY(inspector->isVisible());
    action("workspaceFocus")->trigger();
    QApplication::processEvents();
    const int focusedCanvasWidth = canvas->width();
    action("workspaceProperties")->trigger();
    QApplication::processEvents();
    QVERIFY(inspector->isVisible());
    QVERIFY(!layers->isVisible());
    QVERIFY(action("workspaceFocus")->isChecked());
    // Focus opens its inspector over the canvas rather than reallocating it.
    QCOMPARE(canvas->width(), focusedCanvasWidth);
    action("workspaceLayers")->trigger();
    QApplication::processEvents();
    QVERIFY(layers->isVisible());
    QVERIFY(action("workspaceFocus")->isChecked());
    QVERIFY(!diagnostics->isVisible());
    action("workspaceDiagnostics")->trigger();
    QApplication::processEvents();
    QVERIFY(diagnostics->isVisible());
    QVERIFY(action("workspaceFocus")->isChecked());
    action("workspaceDiagnostics")->trigger();
    QVERIFY(!diagnostics->isVisible());

    action("workspaceBalance")->trigger();
    QApplication::processEvents();
    QVERIFY(!layers->isVisible());
    QVERIFY(inspector->isVisible());
    QVERIFY(!action("workspaceLayers")->isChecked());
    QVERIFY(action("workspaceProperties")->isChecked());
}

void WorkspaceTests::focusPropertiesAvoidSourceAndPreview()
{
    auto* tabs = window_->findChild<QTabWidget*>(u"editorTabs"_s);
    auto* inspector = window_->findChild<QTabWidget*>(u"inspectorTabs"_s);
    auto* card = window_->findChild<QWidget*>(u"focusInspectorCard"_s);
    auto* slot = window_->findChild<QWidget*>(u"inspectorSlot"_s);
    auto* focusBar = window_->findChild<QWidget*>(u"focusContextBar"_s);
    auto* layout = window_->findChild<QComboBox*>(u"editorLayout"_s);
    auto* editorSplitter = window_->findChild<QSplitter*>(u"editorSplitter"_s);
    auto* code = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    auto* canvas = window_->findChild<LabelCanvas*>();
    auto* session = window_->findChild<DocumentSession*>();
    QVERIFY(tabs); QVERIFY(inspector); QVERIFY(card); QVERIFY(slot); QVERIFY(focusBar);
    QVERIFY(layout); QVERIFY(editorSplitter); QVERIFY(code); QVERIFY(canvas); QVERIFY(session);
    canvas->setSelectedIndex(0);
    const auto source = code->toPlainText();
    const auto selection = canvas->selectedIndices();
    QSignalSpy sourceChanges(code, &QPlainTextEdit::textChanged);
    QSignalSpy documentChanges(session, &DocumentSession::documentChanged);
    const auto bounds = [this](const QWidget* widget) {
        return QRect(widget->mapTo(window_.get(), QPoint{}), widget->size());
    };

    action("workspaceFocus")->trigger();
    action("workspaceProperties")->trigger();
    QApplication::processEvents();
    const int designWidth = canvas->width();
    QVERIFY(card->isVisible());
    QVERIFY(card->isAncestorOf(inspector));
    QVERIFY(!slot->isVisible());
    QVERIFY(focusBar->isVisible());

    for (const int tab : {1, 2, 1}) {
        tabs->setCurrentIndex(tab);
        QApplication::processEvents();
        QVERIFY(action("workspaceFocus")->isChecked());
        QVERIFY(action("workspaceProperties")->isChecked());
        QVERIFY(inspector->isVisible());
        QVERIFY(slot->isVisible());
        QVERIFY(slot->isAncestorOf(inspector));
        QVERIFY(!card->isVisible());
        QVERIFY(!focusBar->isVisible());
        QVERIFY(!bounds(slot).intersects(bounds(tabs->currentWidget())));
        QVERIFY(!bounds(slot).intersects(bounds(editorSplitter)));
        QVERIFY(editorSplitter->width() < designWidth);
    }

    action("workspaceProperties")->trigger();
    QApplication::processEvents();
    const int sourceWithoutProperties = editorSplitter->width();
    QVERIFY(!inspector->isVisible());
    QVERIFY(!slot->isVisible());
    action("workspaceProperties")->trigger();
    QApplication::processEvents();
    QVERIFY(slot->isVisible());
    QVERIFY(editorSplitter->width() < sourceWithoutProperties);
    QVERIFY(!bounds(slot).intersects(bounds(code)));

    tabs->setCurrentIndex(0);
    QApplication::processEvents();
    QVERIFY(card->isVisible());
    QVERIFY(card->isAncestorOf(inspector));
    QVERIFY(!slot->isVisible());
    QVERIFY(focusBar->isVisible());
    QCOMPARE(canvas->width(), designWidth);

    for (const int mode : {1, 2}) {
        layout->setCurrentIndex(mode);
        QApplication::processEvents();
        QCOMPARE(editorSplitter->count(), 2);
        QVERIFY(slot->isVisible());
        QVERIFY(inspector->isVisible());
        QVERIFY(slot->isAncestorOf(inspector));
        QVERIFY(!card->isVisible());
        QVERIFY(!focusBar->isVisible());
        QVERIFY(!bounds(slot).intersects(bounds(code)));
        QVERIFY(!bounds(slot).intersects(bounds(tabs->currentWidget())));
    }
    QCOMPARE(code->toPlainText(), source);
    QCOMPARE(canvas->selectedIndices(), selection);
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(documentChanges.count(), 0);
}

void WorkspaceTests::modeSwitchPreservesPendingSourceAndHistory()
{
    auto* canvas = window_->findChild<LabelCanvas*>();
    auto* code = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    auto* tabs = window_->findChild<QTabWidget*>(u"editorTabs"_s);
    auto* session = window_->findChild<DocumentSession*>();
    QVERIFY(canvas); QVERIFY(code); QVERIFY(tabs); QVERIFY(session);
    canvas->setSelectedIndex(0);
    auto* x = window_->findChild<QSpinBox*>(u"elementX"_s);
    QVERIFY(x);
    x->setValue(72);
    const QString committed = toZpl(window_->document());
    const auto selection = canvas->selectedIndices();
    const auto historyCount = session->undoStack()->count();
    const auto historyIndex = session->undoStack()->index();
    tabs->setCurrentIndex(1);
    const QString pending = u"^XA\n^PW360\n^LL200\n^FO16,24^GB88,40,2^FS\n^XZ\n"_s;
    code->setPlainText(pending);
    QTextCursor cursor = code->textCursor();
    cursor.setPosition(5);
    cursor.setPosition(13, QTextCursor::KeepAnchor);
    code->setTextCursor(cursor);
    QSignalSpy sourceChanges(code, &QPlainTextEdit::textChanged);
    QSignalSpy documentChanges(session, &DocumentSession::documentChanged);
    QSignalSpy layoutChanges(session, &DocumentSession::layoutSettingsChanged);

    action("workspaceFocus")->trigger();
    action("workspaceProperties")->trigger();
    action("workspaceBalance")->trigger();
    QApplication::processEvents();
    QCOMPARE(window_->findChild<QPlainTextEdit*>(u"zplEditor"_s), code);
    QCOMPARE(window_->findChild<QSpinBox*>(u"elementX"_s), x);
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(code->toPlainText(), pending);
    QCOMPARE(code->textCursor().position(), cursor.position());
    QCOMPARE(code->textCursor().anchor(), cursor.anchor());
    QCOMPARE(canvas->selectedIndices(), selection);
    QVERIFY(session->hasPendingSource());
    QCOMPARE(session->pendingSource(), pending);
    QCOMPARE(toZpl(window_->document()), committed);
    QCOMPARE(session->undoStack()->count(), historyCount);
    QCOMPARE(session->undoStack()->index(), historyIndex);
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(documentChanges.count(), 0);
    QCOMPARE(layoutChanges.count(), 0);
    QVERIFY(!action("undoAction")->isEnabled());

    QVERIFY(window_->applySource());
    action("undoAction")->trigger();
    QCOMPARE(toZpl(window_->document()), committed);
    action("undoAction")->trigger();
    QCOMPARE(toZpl(window_->document()), toZpl(initial_));
}

void WorkspaceTests::themeChangesColorsWithoutRebuildingEditors()
{
    auto* canvas = window_->findChild<LabelCanvas*>();
    auto* code = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    auto* layers = window_->findChild<QListWidget*>(u"layerList"_s);
    auto* x = window_->findChild<QSpinBox*>(u"elementX"_s);
    auto* session = window_->findChild<DocumentSession*>();
    QVERIFY(canvas); QVERIFY(code); QVERIFY(layers); QVERIFY(x); QVERIFY(session);
    canvas->setSelectedIndex(0);
    x->setValue(72);
    code->setPlainText(u"^XA\n^PW360\n^LL200\n^FX unfinished edit"_s);
    QTextCursor cursor = code->textCursor();
    cursor.setPosition(8);
    code->setTextCursor(cursor);
    // Selection and pending-source updates can queue inspector layout work.
    // Measure the settled light state before attributing geometry to a theme.
    QTest::qWait(150);
    QApplication::processEvents();
    const auto initialPalette = qApp->palette();
    auto* style = qApp->style();
    const auto appFont = qApp->font();
    const auto codeFont = code->font();
    const auto spinFont = x->font();
    const auto geometry = window_->geometry();
    const auto canvasGeometry = canvas->geometry();
    const auto spinGeometry = x->geometry();
    const auto historyCount = session->undoStack()->count();
    const auto source = code->toPlainText();
    QSignalSpy modelResets(layers->model(), &QAbstractItemModel::modelReset);
    QSignalSpy sourceChanges(code, &QPlainTextEdit::textChanged);
    QSignalSpy documentChanges(session, &DocumentSession::documentChanged);

    action("darkTheme")->trigger();
    QApplication::processEvents();
    QCOMPARE(designerTheme(), DesignerTheme::Dark);
    QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 128);
    QVERIFY(qApp->palette().color(QPalette::Text).lightness() > 128);
    QCOMPARE(qApp->style(), style);
    QCOMPARE(qApp->font(), appFont);
    QCOMPARE(code->font(), codeFont);
    QCOMPARE(x->font(), spinFont);
    QCOMPARE(window_->geometry(), geometry);
    QCOMPARE(canvas->geometry(), canvasGeometry);
    QCOMPARE(x->geometry(), spinGeometry);
    QCOMPARE(window_->findChild<QPlainTextEdit*>(u"zplEditor"_s), code);
    QCOMPARE(window_->findChild<QSpinBox*>(u"elementX"_s), x);
    QCOMPARE(code->textCursor().position(), cursor.position());
    QCOMPARE(code->toPlainText(), source);
    QCOMPARE(canvas->selectedIndex(), 0);
    QCOMPARE(session->undoStack()->count(), historyCount);
    QVERIFY(session->hasPendingSource());
    QCOMPARE(modelResets.count(), 0);
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(documentChanges.count(), 0);

    action("darkTheme")->trigger();
    QApplication::processEvents();
    QCOMPARE(designerTheme(), DesignerTheme::Light);
    QCOMPARE(qApp->palette(), initialPalette);
    QCOMPARE(qApp->style(), style);
    QCOMPARE(qApp->font(), appFont);
    QCOMPARE(canvas->geometry(), canvasGeometry);
    QCOMPARE(x->geometry(), spinGeometry);
    QCOMPARE(code->toPlainText(), source);
}

void WorkspaceTests::themeAndModePreservePrinterGeometryAndWhitePaper()
{
    auto* canvas = window_->findChild<LabelCanvas*>();
    auto* session = window_->findChild<DocumentSession*>();
    QVERIFY(canvas); QVERIFY(session);
    canvas->setGridVisible(false);
    canvas->actualSize();
    const auto printZpl = toZpl(window_->document());
    const auto printPreview = renderPreview(window_->document());
    QVERIFY(printPreview);
    const auto transform = canvas->transform();
    QSignalSpy documentChanges(session, &DocumentSession::documentChanged);
    action("workspaceFocus")->trigger();
    action("darkTheme")->trigger();
    QApplication::processEvents();
    const auto screenshot = canvas->viewport()->grab().toImage();
    const auto whitePoint = canvas->mapFromScene(QPointF(210, 145));
    QVERIFY(canvas->viewport()->rect().contains(whitePoint));
    const auto devicePoint = QPoint(qRound(whitePoint.x() * screenshot.devicePixelRatio()),
                                    qRound(whitePoint.y() * screenshot.devicePixelRatio()));
    QCOMPARE(screenshot.pixelColor(devicePoint), QColor(Qt::white));
    QCOMPARE(canvas->transform(), transform);
    QCOMPARE(toZpl(window_->document()), printZpl);
    const auto after = renderPreview(window_->document());
    QVERIFY(after);
    QCOMPARE(after->image, printPreview->image);
    QCOMPARE(window_->document().width, initial_.width);
    QCOMPARE(window_->document().height, initial_.height);
    QCOMPARE(documentChanges.count(), 0);
    QVERIFY(!session->modified());
    QCOMPARE(session->undoStack()->count(), 0);
}

void WorkspaceTests::preferencesRestoreAcrossWindows()
{
    auto* splitter = window_->findChild<QSplitter*>(u"workspaceSplitter"_s);
    QVERIFY(splitter);
    splitter->setSizes({225, 670, 315});
    QApplication::processEvents();
    const auto balancedSizes = splitter->sizes();
    action("workspaceFocus")->trigger();
    action("workspaceProperties")->trigger();
    action("darkTheme")->trigger();
    window_.reset();
    QVERIFY(QFile::exists(QDir(settingsDirectory_).filePath(u"workspace.ini"_s)));
    createWindow();

    QVERIFY(action("workspaceFocus")->isChecked());
    QVERIFY(!action("workspaceBalance")->isChecked());
    QVERIFY(action("darkTheme")->isChecked());
    QCOMPARE(designerTheme(), DesignerTheme::Dark);
    auto* layers = window_->findChild<QWidget*>(u"layersPanel"_s);
    auto* inspector = window_->findChild<QTabWidget*>(u"inspectorTabs"_s);
    QVERIFY(layers); QVERIFY(inspector);
    QVERIFY(!layers->isVisible());
    QVERIFY(inspector->isVisible());
    QVERIFY(!action("workspaceLayers")->isChecked());
    QVERIFY(action("workspaceProperties")->isChecked());
    action("workspaceBalance")->trigger();
    QApplication::processEvents();
    QVERIFY(layers->isVisible());
    QVERIFY(inspector->isVisible());
    QCOMPARE(window_->findChild<QSplitter*>(u"workspaceSplitter"_s)->sizes(), balancedSizes);
}

void WorkspaceTests::toolbarPreferencesDefaultVisibleAndPersistHiddenCommands()
{
    window_.reset();
    const auto path = workspacePreferencesPath();
    if (QFile::exists(path)) QVERIFY(QFile::remove(path));
    const auto defaults = loadWorkspacePreferences();
    QVERIFY(defaults.hiddenToolbarCommands.isEmpty());
    auto preferences = defaults;
    preferences.hiddenToolbarCommands = {u"save"_s, u"undo"_s, u"group"_s};
    QVERIFY(saveWorkspacePreferences(preferences));
    QCOMPARE(loadWorkspacePreferences().hiddenToolbarCommands, preferences.hiddenToolbarCommands);
    QVERIFY(saveWorkspacePreferences(defaults));
    QVERIFY(loadWorkspacePreferences().hiddenToolbarCommands.isEmpty());
}

void WorkspaceTests::optionalWorkspaceScreenshots()
{
    const auto destination = qEnvironmentVariable("DESIGNER_WORKSPACE_SCREENSHOTS");
    if (destination.isEmpty()) return;
    QVERIFY(QDir().mkpath(destination));
    window_.reset();
    window_ = std::make_unique<MainWindow>();
    // Use the exact specimen from the HTML reference, rendered by QtZpl.
    auto reference = fromZpl(u"^XA\n^PW600\n^LL320\n^CI28\n"
        "^FO20,20^GB560,280,2^FS\n"
        "^FO44,44^A0N,32,0^FDPRECISION PARTS^FS\n"
        "^FO44,90^A0N,22,0^FDSKU: QT-2048 / LOT: 07^FS\n"
        "^FO44,142^BXN,5,200^FDQT-2048-07^FS\n"
        "^FO224,148^A0N,28,0^FDQTY 100^FS\n"
        "^FO224,192^A0N,22,0^FDINSPECTED^FS\n^XZ"_s);
    QVERIFY(reference);
    QCOMPARE(reference->elements.size(), 6);
    const QStringList names{u"Рамка этикетки"_s, u"Название"_s, u"Артикул и партия"_s,
                            u"DataMatrix"_s, u"Количество"_s, u"Отметка контроля"_s};
    for (qsizetype index = 0; index < names.size(); ++index)
        reference->elements[index].name = names[index];
    // Frontmost layers appear first in the native list, as in the reference.
    const auto elements = reference->elements;
    reference->elements = {elements[0], elements[5], elements[4], elements[3], elements[2], elements[1]};
    auto* session = window_->findChild<DocumentSession*>();
    QVERIFY(session);
    session->reset(*reference, u"product-label.zpl"_s);
    auto layout = session->layoutSettings();
    layout.unit = CanvasUnit::Dots;
    session->setLayoutSettings(layout);
    session->undoStack()->setClean();
    window_->resize(1440, 900);
    window_->show();
    auto* canvas = window_->findChild<LabelCanvas*>();
    canvas->setSelectedIndex(5);
    canvas->fitLabel();
    const auto screenshotSource = toZpl(window_->document());
    const QDir directory(destination);
    QJsonObject geometry;
    const auto rectObject = [](const QRect& rectangle) {
        return QJsonObject{{u"x"_s, rectangle.x()}, {u"y"_s, rectangle.y()},
                           {u"width"_s, rectangle.width()}, {u"height"_s, rectangle.height()}};
    };
    const auto saveScreenshot = [&](const QString& name) {
        // Let the preview debounce timer and native paint/layout events settle
        // before capturing each mode; all captures use the same label data.
        QTest::qWait(150);
        QApplication::processEvents();
        QJsonObject widgets;
        for (const auto* objectName : {"elementPalette", "layersPanel", "inspectorTabs", "labelCanvas", "focusInspectorCard"}) {
            auto* widget = window_->findChild<QWidget*>(QString::fromLatin1(objectName));
            if (!widget) continue;
            auto bounds = rectObject(QRect(widget->mapTo(window_.get(), QPoint{}), widget->size()));
            bounds.insert(u"visible"_s, widget->isVisible());
            widgets.insert(QString::fromLatin1(objectName), bounds);
        }
        auto* inspector = window_->findChild<QTabWidget*>(u"inspectorTabs"_s);
        QJsonObject controls;
        bool fieldsFit = inspector != nullptr;
        if (inspector) {
            for (const auto* objectName : {"propertyUnits", "elementX", "elementY", "elementWidth", "elementHeight",
                                         "elementContent", "fontHeight", "orientation", "barcodeType", "moduleWidth",
                                         "thickness", "placeholderValues", "dataMatrixGs1", "gs1Builder", "gs1Status"}) {
                auto* control = window_->findChild<QWidget*>(QString::fromLatin1(objectName));
                if (!control) {
                    fieldsFit = false;
                    qWarning().noquote() << name << "is missing inspector control" << objectName;
                    continue;
                }
                const QRect bounds(control->mapTo(inspector, QPoint{}), control->size());
                const bool visible = control->isVisible();
                const bool contained = bounds.isEmpty() || inspector->rect().contains(bounds);
                auto details = rectObject(bounds);
                details.insert(u"visible"_s, visible);
                details.insert(u"containedByInspector"_s, contained);
                controls.insert(QString::fromLatin1(objectName), details);
                if (visible && !contained) {
                    fieldsFit = false;
                    qWarning().noquote() << name << "clips inspector control" << objectName
                                        << bounds << "outside" << inspector->rect();
                }
            }
        }
        if (auto* card = window_->findChild<QWidget*>(u"focusInspectorCard"_s); card && card->isVisible()) {
            if (card->width() != 238) {
                fieldsFit = false;
                qWarning().noquote() << name << "has focus inspector width" << card->width() << "instead of 238";
            }
        }
        // Validate the same whole-window image that is saved. A direct canvas
        // grab would miss painter-origin regressions in QWidget::render().
        const auto snapshot = window_->grab().toImage();
        const auto viewportPaper = canvas->mapFromScene(QRectF(0, 0, reference->width, reference->height)).boundingRect();
        const QRect logicalPaper(canvas->viewport()->mapTo(window_.get(), viewportPaper.topLeft()), viewportPaper.size());
        const auto clippedPaper = logicalPaper.intersected(window_->rect());
        const qreal dpr = snapshot.devicePixelRatio();
        const QRect pixelPaper = QRect(qFloor(clippedPaper.x() * dpr), qFloor(clippedPaper.y() * dpr),
                                        qCeil(clippedPaper.width() * dpr), qCeil(clippedPaper.height() * dpr))
                                     .intersected(snapshot.rect());
        int blackPixels = 0;
        int whitePixels = 0;
        for (int y = pixelPaper.top(); y <= pixelPaper.bottom(); ++y) {
            for (int x = pixelPaper.left(); x <= pixelPaper.right(); ++x) {
                const auto color = snapshot.pixelColor(x, y);
                if (color.red() < 64 && color.green() < 64 && color.blue() < 64) ++blackPixels;
                if (color.red() > 245 && color.green() > 245 && color.blue() > 245) ++whitePixels;
            }
        }
        geometry.insert(name, QJsonObject{{u"window"_s, rectObject(window_->rect())},
                                         {u"devicePixelRatio"_s, dpr}, {u"widgets"_s, widgets},
                                         {u"inspectorControls"_s, controls},
                                         {u"paper"_s, rectObject(logicalPaper)},
                                         {u"blackPaperPixels"_s, blackPixels}, {u"whitePaperPixels"_s, whitePixels}});
        const bool saved = snapshot.save(directory.filePath(name + u".png"_s));
        // Keep measured bounds even when a capture assertion fails.
        QFile metrics(directory.filePath(u"workspace-geometry.json"_s));
        const bool metricsSaved = metrics.open(QIODevice::WriteOnly)
            && metrics.write(QJsonDocument(geometry).toJson()) > 0;
        if (pixelPaper.isEmpty() || blackPixels <= 1000 || whitePixels <= pixelPaper.width() * pixelPaper.height() / 2) {
            qWarning().noquote() << name << "has missing or misplaced paper raster:"
                                << pixelPaper << "black pixels" << blackPixels << "white pixels" << whitePixels;
            return false;
        }
        return saved && metricsSaved && fieldsFit;
    };
    QVERIFY(saveScreenshot(u"balance-light"_s));
    action("workspaceFocus")->trigger();
    QVERIFY(saveScreenshot(u"focus-light"_s));
    action("darkTheme")->trigger();
    QVERIFY(saveScreenshot(u"focus-dark"_s));
    action("workspaceProperties")->trigger();
    QVERIFY(saveScreenshot(u"focus-properties-dark"_s));
    action("workspaceProperties")->trigger();
    action("workspaceBalance")->trigger();
    QVERIFY(saveScreenshot(u"balance-dark"_s));
    QCOMPARE(toZpl(window_->document()), screenshotSource);
    QCOMPARE(canvas->selectedIndex(), 5);
}

QTEST_MAIN(WorkspaceTests)
#include "test_workspace.moc"
