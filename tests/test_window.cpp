#include "main_window.hpp"
#include "label_canvas.hpp"
#include "preview_widget.hpp"
#include "placeholder_values_dialog.hpp"
#include "canvas_quick_panel.hpp"
#include "guide_list_dialog.hpp"
#include "document_session.hpp"
#include "dot_spin_box.hpp"
#include "print_dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTableWidget>
#include <QToolButton>
#include <QUuid>
#include <QtTest>

#include <memory>

using Designer::DesignDocument;
using Designer::ElementType;
using Designer::LabelCanvas;
using Designer::MainWindow;

namespace {
class PrintTestSettings final {
public:
    PrintTestSettings()
        : previousName_(QCoreApplication::applicationName()),
          previousTestMode_(QStandardPaths::isTestModeEnabled())
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("window-printing-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        directory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    ~PrintTestSettings()
    {
        QFile::remove(preferencesPath());
        QFile::remove(QDir(directory_).filePath(QStringLiteral("printer-profiles.json")));
        QDir().rmdir(directory_);
        QCoreApplication::setApplicationName(previousName_);
        QStandardPaths::setTestModeEnabled(previousTestMode_);
    }
    QString preferencesPath() const { return QDir(directory_).filePath(QStringLiteral("printing.ini")); }

private:
    QString previousName_;
    bool previousTestMode_;
    QString directory_;
};
} // namespace

class WindowTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void propertyEditsParticipateInToolbarHistory();
    void rapidCanvasNudgesAccumulateInDocument();
    void designTabPreservesZplAndUpdatesDesign();
    void importedImageSurvivesZplSaveAndReopen();
    void pendingSourceBlocksDesignUndoUntilApplied();
    void sizePropertiesChangeTextAndCodes_data();
    void sizePropertiesChangeTextAndCodes();
    void circleHeightChangesDiameter();
    void previewActionAppliesPendingSourceAndFollowsEdits();
    void previewTabAppliesPendingSourceAndRejectsInvalidSource();
    void previewRenderFailureClearsImage();
    void immediateResizeAfterFontEditUsesFreshBounds();
    void gs1PropertiesSaveAndRejectInvalidData();
    void barcodePaletteCreatesTwoFamilies();
    void barcodeTypePreservesContentAndHistory();
    void barcodeTypeAppliesFormatConstraints();
    void barcodeTypeRejectsIneligibleEdits();
    void barcodeTypePreservesImportedCommentsAndSavedRendering();
    void millimetreFormatIsUndoableAndKeepsElementGeometry();
    void scaledLabelFollowsDpiAndFormat();
    void newDocumentIs100MillimetresAt300Dpi();
    void groupActionsPreserveSelectionAndHistory();
    void pendingSourceBlocksGroupActions();
    void layerNavigationTracksCanvasSelection();
    void dataMatrixTemplateValuesUpdatePreviewWithoutReplacingSource();
    void gridCheckboxChangesCanvasOnly();
    void layoutSettingsSaveReopenWithoutChangingPrintOutput();
    void layoutEditsPreservePendingSource();
    void quickPanelUnitsAndGuideListCancelPreserveCoordinates();
    void importedBlockAndCode128PropertiesAreEditable();
    void propertyUnitsReachGeometryAndAdvancedTextFields();
    void labelFormatFollowsPropertyUnits();
    void printActionOpensBothModesAndConnectionsWithoutSubmitting();
    void printDialogModesFollowConnection();
    void printDialogRestoresPngPreferences();
    void printDialogLoadsAndSavesPngProfileWithoutSubmitting();

private:
    DesignDocument initial_;
    std::unique_ptr<MainWindow> window_;
    LabelCanvas* canvas_ = nullptr;
    QPlainTextEdit* code_ = nullptr;
    QAction* undo_ = nullptr;
    QAction* redo_ = nullptr;
    QString previousApplicationName_;
    QString workspaceSettingsDirectory_;
    bool previousTestMode_ = false;
};

void WindowTests::init()
{
    previousApplicationName_ = QCoreApplication::applicationName();
    previousTestMode_ = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(QStringLiteral("window-tests-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    workspaceSettingsDirectory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    initial_ = {};
    initial_.width = 400;
    initial_.height = 300;
    auto box = Designer::makeElement(ElementType::Box, {40, 48});
    box.width = 80;
    box.height = 56;
    initial_.elements.append(box);
    window_ = std::make_unique<MainWindow>();
    window_->setDocument(initial_);
    window_->show();
    QApplication::processEvents();
    canvas_ = window_->findChild<LabelCanvas*>(QStringLiteral("labelCanvas"));
    code_ = window_->findChild<QPlainTextEdit*>(QStringLiteral("zplEditor"));
    undo_ = window_->findChild<QAction*>(QStringLiteral("undoAction"));
    redo_ = window_->findChild<QAction*>(QStringLiteral("redoAction"));
    QVERIFY(canvas_);
    QVERIFY(code_);
    QVERIFY(undo_);
    QVERIFY(redo_);
}

void WindowTests::cleanup()
{
    // No file dialogs or unsaved-change prompts may escape into an offscreen
    // test run, including after an assertion fails partway through a case.
    if (window_) {
        window_->setDocument(initial_);
        window_.reset();
    }
    canvas_ = nullptr;
    code_ = nullptr;
    undo_ = redo_ = nullptr;
    QFile::remove(QDir(workspaceSettingsDirectory_).filePath(QStringLiteral("workspace.ini")));
    QDir().rmdir(workspaceSettingsDirectory_);
    QCoreApplication::setApplicationName(previousApplicationName_);
    QStandardPaths::setTestModeEnabled(previousTestMode_);
}

void WindowTests::gridCheckboxChangesCanvasOnly()
{
    auto* grid = window_->findChild<QCheckBox*>(QStringLiteral("showGrid"));
    QVERIFY(grid);
    QVERIFY(grid->isChecked());
    const auto original = code_->toPlainText();
    const auto visibleGrid = canvas_->viewport()->grab().toImage();
    grid->setChecked(false);
    QApplication::processEvents();
    const auto hiddenGrid = canvas_->viewport()->grab().toImage();
    QVERIFY(visibleGrid != hiddenGrid);
    grid->setChecked(true);
    QApplication::processEvents();
    QCOMPARE(canvas_->viewport()->grab().toImage(), visibleGrid);
    QCOMPARE(code_->toPlainText(), original);
    QVERIFY(undo_->isEnabled());
    undo_->trigger();
    QVERIFY(!grid->isChecked());
    undo_->trigger();
    QVERIFY(grid->isChecked());
    QVERIFY(!undo_->isEnabled());
}

void WindowTests::layoutSettingsSaveReopenWithoutChangingPrintOutput()
{
    auto* session = window_->findChild<Designer::DocumentSession*>();
    QVERIFY(session);
    window_->showPreview();
    auto* previewWidget = window_->findChild<Designer::PreviewWidget*>();
    auto* previewView = previewWidget->findChild<QGraphicsView*>(QStringLiteral("previewView"));
    QVERIFY(previewView);
    QApplication::processEvents();
    const QImage cleanPreview = previewView->viewport()->grab().toImage();
    const QString originalZpl = Designer::toZpl(window_->document());
    const auto printed = Designer::renderPreview(window_->document());
    QVERIFY(printed);
    QSignalSpy documentChanges(session, &Designer::DocumentSession::documentChanged);
    QSignalSpy sourceChanges(code_, &QPlainTextEdit::textChanged);
    // The quick panel applies each change at once and merges one opening.
    auto* setup = window_->findChild<QPushButton*>(QStringLiteral("canvasSetup"));
    setup->click();
    auto* panel = window_->findChild<Designer::CanvasQuickPanel*>();
    QVERIFY(panel && panel->isVisible());
    QVERIFY(setup->isChecked());
    panel->findChild<QPushButton*>(QStringLiteral("quickPanelDots"))->click();
    panel->findChild<Designer::DotSpinBox*>(QStringLiteral("quickPanelGridStep"))->setValue(12);
    panel->findChild<QCheckBox*>(QStringLiteral("quickPanelMargins"))->setChecked(true);
    panel->findChild<Designer::DotSpinBox*>(QStringLiteral("quickPanelMargin"))->setValue(20);
    QCOMPARE(canvas_->layoutSettings().gridStep, 12);
    QTest::keyClick(panel, Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
    QVERIFY(!setup->isChecked());
    const int panelStep = session->undoStack()->index();
    QCOMPARE(panelStep, 1);
    // "Список…" closes the panel and opens the guide table.
    bool edited = false;
    QTimer guideList;
    guideList.setInterval(10);
    connect(&guideList, &QTimer::timeout, window_.get(), [&] {
        auto* dialog = window_->findChild<Designer::GuideListDialog*>();
        if (!dialog || !dialog->isVisible()) return;
        guideList.stop();
        QVERIFY(dialog->addGuide(true));
        dialog->moveGuide(*dialog->currentGuide(), 120);
        QVERIFY(dialog->addGuide(false));
        dialog->moveGuide(*dialog->currentGuide(), 160);
        QVERIFY(dialog->currentGuide() == Designer::CanvasGuide({false, 160}));
        edited = true;
        dialog->accept();
    });
    guideList.start();
    setup->click();
    panel->findChild<QPushButton*>(QStringLiteral("quickPanelGuideList"))->click();
    QVERIFY(!panel->isVisible());
    QTRY_VERIFY(edited);
    const auto settings = session->layoutSettings();
    QCOMPARE(settings.gridStep, 12);
    QCOMPARE(settings.margin, 20);
    QCOMPARE(settings.horizontalGuides, QList<int>{120});
    QCOMPARE(settings.verticalGuides, QList<int>{160});
    QVERIFY(settings.marginsVisible);
    QCOMPARE(canvas_->layoutSettings(), settings);
    QCOMPARE(documentChanges.count(), 0);
    QCOMPARE(sourceChanges.count(), 0);
    QApplication::processEvents();
    QCOMPARE(previewView->viewport()->grab().toImage(), cleanPreview);
    QCOMPARE(Designer::toZpl(window_->document()), originalZpl);
    undo_->trigger();
    auto panelOnly = settings;
    panelOnly.horizontalGuides.clear();
    panelOnly.verticalGuides.clear();
    QCOMPARE(session->layoutSettings(), panelOnly);
    // Four panel edits form one undo step.
    undo_->trigger();
    QVERIFY(session->layoutSettings() == Designer::CanvasLayoutSettings{});
    redo_->trigger();
    redo_->trigger();
    QCOMPARE(session->layoutSettings(), settings);

    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("layout.zpl"));
    QVERIFY(window_->saveFile(path));
    QFile zpl(path);
    QVERIFY(zpl.open(QIODevice::ReadOnly));
    QCOMPARE(zpl.readAll(), originalZpl.toUtf8());
    QVERIFY(QFile::exists(Designer::DocumentSession::layoutStoragePath(path)));
    QVERIFY(!QFile::exists(path + QStringLiteral(".layout.json")));

    // Exercise the actual PNG export action rather than a screenshot of the editor.
    const auto pngPath = directory.filePath(QStringLiteral("print.png"));
    const bool nativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    bool fileChosen = false;
    QTimer::singleShot(0, window_.get(), [&] {
        auto* fileDialog = window_->findChild<QFileDialog*>();
        if (!fileDialog) return;
        fileDialog->selectFile(pngPath);
        fileChosen = QMetaObject::invokeMethod(fileDialog, "accept", Qt::DirectConnection);
    });
    previewWidget->findChild<QPushButton*>(QStringLiteral("previewExportPng"))->click();
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
    QVERIFY(fileChosen);
    QCOMPARE(QImage(pngPath).convertToFormat(QImage::Format_RGB32), printed->image.convertToFormat(QImage::Format_RGB32));
    window_->setDocument(initial_);
    QVERIFY(window_->openFile(path));
    QCOMPARE(session->layoutSettings(), settings);
    QCOMPARE(canvas_->layoutSettings(), settings);
    QVERIFY(!session->modified());
}

void WindowTests::layoutEditsPreservePendingSource()
{
    auto* session = window_->findChild<Designer::DocumentSession*>();
    QVERIFY(session);
    code_->setPlainText(QStringLiteral("^XA^PW333^LL222^FO20,20^GB100,40,2^FS^XZ"));
    const QString pending = code_->toPlainText();
    auto settings = session->layoutSettings();
    settings.verticalGuides = {80};
    session->setLayoutSettings(settings);
    QCOMPARE(code_->toPlainText(), pending);
    QVERIFY(!undo_->isEnabled());
    QVERIFY(window_->applySource());
    QCOMPARE(Designer::toZpl(window_->document()), pending);
    QCOMPARE(session->layoutSettings(), settings);
    QVERIFY(undo_->isEnabled());
}

void WindowTests::quickPanelUnitsAndGuideListCancelPreserveCoordinates()
{
    auto* session = window_->findChild<Designer::DocumentSession*>();
    auto original = session->layoutSettings();
    original.gridStep = 7;
    original.margin = 11;
    original.horizontalGuides = {37};
    session->setLayoutSettings(original);
    const int history = session->undoStack()->index();
    window_->findChild<QPushButton*>(QStringLiteral("canvasSetup"))->click();
    auto* panel = window_->findChild<Designer::CanvasQuickPanel*>();
    QVERIFY(panel && panel->isVisible());
    auto* millimeters = panel->findChild<QPushButton*>(QStringLiteral("quickPanelMillimeters"));
    auto* dots = panel->findChild<QPushButton*>(QStringLiteral("quickPanelDots"));
    for (int i = 0; i < 5; ++i) {
        dots->click();
        millimeters->click();
    }
    // Unit switches only change the display; back where it started, the
    // panel leaves no history step at all.
    QCOMPARE(session->layoutSettings(), original);
    QCOMPARE(session->undoStack()->index(), history);
    auto* margin = panel->findChild<Designer::DotSpinBox*>(QStringLiteral("quickPanelMargin"));
    QCOMPARE(margin->maximum(), (std::min(window_->document().width, window_->document().height) - 1) / 2);
    QVERIFY(!margin->isEnabled());
    panel->close();

    Designer::GuideListDialog dialog(original, {400, 300}, 300, window_.get());
    QVERIFY(dialog.addGuide(false));
    QCOMPARE(dialog.settings().verticalGuides, QList<int>{200});
    QVERIFY(dialog.addGuide(false));
    QCOMPARE(dialog.settings().verticalGuides, QList<int>({200, 207}));
    dialog.moveGuide({false, 207}, 5000);
    QCOMPARE(dialog.settings().verticalGuides, QList<int>({200, 400}));
    dialog.removeGuide({true, 37});
    QVERIFY(dialog.settings().horizontalGuides.isEmpty());
    dialog.reject();
    QCOMPARE(session->layoutSettings(), original);
    QCOMPARE(session->undoStack()->index(), history);
}

void WindowTests::propertyEditsParticipateInToolbarHistory()
{
    canvas_->setSelectedIndex(0);
    auto* x = window_->findChild<QSpinBox*>(QStringLiteral("elementX"));
    auto* width = window_->findChild<QSpinBox*>(QStringLiteral("elementWidth"));
    QVERIFY(x);
    QVERIFY(width);
    QVERIFY(x->isEnabled());
    x->setValue(72);
    width->setValue(120);
    QCOMPARE(window_->document().elements[0].x, 72);
    QCOMPARE(window_->document().elements[0].width, 120);
    QVERIFY(undo_->isEnabled());

    undo_->trigger();
    QCOMPARE(window_->document().elements[0].x, 72);
    QCOMPARE(window_->document().elements[0].width, 80);
    QCOMPARE(width->value(), 80);
    undo_->trigger();
    QCOMPARE(window_->document().elements[0].x, 40);
    QCOMPARE(x->value(), 40);
    QVERIFY(redo_->isEnabled());
    redo_->trigger();
    QCOMPARE(window_->document().elements[0].x, 72);
    QCOMPARE(x->value(), 72);
}

void WindowTests::rapidCanvasNudgesAccumulateInDocument()
{
    canvas_->setSelectedIndex(0);
    canvas_->setFocus();
    // Do not wait for the preview debounce timer between these key events.
    QTest::keyClick(canvas_, Qt::Key_Right);
    QTest::keyClick(canvas_, Qt::Key_Right);
    QTest::keyClick(canvas_, Qt::Key_Right);
    QTest::keyClick(canvas_, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(canvas_, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(window_->document().elements[0].x, 53);
    QCOMPARE(window_->document().elements[0].y, 58);
    QCOMPARE(canvas_->selectedIndex(), 0);
    undo_->trigger();
    QCOMPARE(window_->document().elements[0].x, 53);
    QCOMPARE(window_->document().elements[0].y, 48);
}

void WindowTests::designTabPreservesZplAndUpdatesDesign()
{
    auto* tabs = window_->findChild<QTabWidget*>(QStringLiteral("editorTabs"));
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);
    const QString source = QStringLiteral("^XA\n^PW320\n^LL240\n^FO24,32^GB100,48,2^FS\n^XZ\n");
    code_->setPlainText(source);
    QCOMPARE(window_->document().width, 400);
    tabs->setCurrentWidget(canvas_);
    QCOMPARE(window_->document().width, 320);
    QCOMPARE(window_->document().height, 240);
    QVERIFY(window_->document().visualEditable);
    QCOMPARE(window_->document().elements.size(), 1);
    QCOMPARE(window_->document().elements[0].x, 24);
    QCOMPARE(window_->document().elements[0].y, 32);
    QCOMPARE(Designer::toZpl(window_->document()), source);
    QCOMPARE(code_->toPlainText(), source);
    QVERIFY(undo_->isEnabled());
    undo_->trigger();
    QCOMPARE(Designer::toZpl(window_->document()), Designer::toZpl(initial_));
    QCOMPARE(code_->toPlainText(), Designer::toZpl(initial_));
}

void WindowTests::importedImageSurvivesZplSaveAndReopen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("logo.png"));
    const QString zplPath = directory.filePath(QStringLiteral("label.zpl"));
    QImage image(13, 9, QImage::Format_ARGB32);
    image.fill(Qt::white);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (x == 0 || y == 0 || x == y)
                image.setPixelColor(x, y, Qt::black);
        }
    }
    QVERIFY(image.save(imagePath));
    QVERIFY(window_->importImage(imagePath, {160, 120}));
    QCOMPARE(window_->document().elements.size(), 2);
    QCOMPARE(window_->document().elements.last().type, ElementType::Image);
    const auto before = Designer::renderPreview(window_->document());
    QVERIFY(before.has_value());
    QVERIFY(window_->saveFile(zplPath));
    QFile saved(zplPath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray bytes = saved.readAll();
    saved.close();
    QVERIFY(bytes.contains("^GFA,"));
    QVERIFY(window_->openFile(zplPath));
    QCOMPARE(Designer::toZpl(window_->document()).toUtf8(), bytes);
    QVERIFY(window_->document().visualEditable);
    QCOMPARE(window_->document().elements.size(), 2);
    QCOMPARE(window_->document().elements.last().type, ElementType::Image);
    const auto reopened = Designer::renderPreview(window_->document());
    QVERIFY(reopened.has_value());
    QCOMPARE(reopened->image, before->image);
    QVERIFY(!undo_->isEnabled());
}

void WindowTests::pendingSourceBlocksDesignUndoUntilApplied()
{
    canvas_->setSelectedIndex(0);
    auto* x = window_->findChild<QSpinBox*>(QStringLiteral("elementX"));
    QVERIFY(x);
    x->setValue(72);
    QVERIFY(undo_->isEnabled());
    const QString committed = Designer::toZpl(window_->document());
    const QString pending = QStringLiteral("^XA\n^PW360\n^LL200\n^FO16,24^GB88,40,2^FS\n^XZ\n");
    code_->setPlainText(pending);
    QVERIFY(!undo_->isEnabled());
    QVERIFY(!redo_->isEnabled());
    undo_->trigger();
    QCOMPARE(code_->toPlainText(), pending);
    QCOMPARE(Designer::toZpl(window_->document()), committed);
    QCOMPARE(window_->document().elements[0].x, 72);

    QVERIFY(window_->applySource());
    QCOMPARE(Designer::toZpl(window_->document()), pending);
    QVERIFY(undo_->isEnabled());
    undo_->trigger();
    QCOMPARE(Designer::toZpl(window_->document()), committed);
    QCOMPARE(code_->toPlainText(), committed);
}

void WindowTests::sizePropertiesChangeTextAndCodes_data()
{
    QTest::addColumn<int>("type");
    for (const auto type : {ElementType::Text, ElementType::Code128, ElementType::QrCode, ElementType::DataMatrix})
        QTest::newRow(qPrintable(Designer::elementTypeName(type))) << static_cast<int>(type);
}

void WindowTests::sizePropertiesChangeTextAndCodes()
{
    QFETCH(int, type);
    DesignDocument document;
    document.width = 1000; document.height = 800;
    auto element = Designer::makeElement(static_cast<ElementType>(type), {32, 32});
    element.gs1 = false; // This test resizes ordinary barcode content.
    element.text = QStringLiteral("12345");
    element.moduleWidth = 2;
    document.elements = {element};
    window_->setDocument(document);
    canvas_->setSelectedIndex(0);
    auto* width = window_->findChild<QSpinBox*>(QStringLiteral("elementWidth"));
    auto* height = window_->findChild<QSpinBox*>(QStringLiteral("elementHeight"));
    QVERIFY(width->isVisible());
    QVERIFY(height->isVisible());
    const int initialWidth = width->value();
    const auto originalZpl = Designer::toZpl(document);
    width->setValue(initialWidth * 2);
    const auto& resized = window_->document().elements.first();
    if (element.type == ElementType::Text) QVERIFY(resized.fontWidth > element.fontWidth);
    else QCOMPARE(resized.moduleWidth, 4);
    QVERIFY(width->value() > initialWidth);
    QVERIFY(Designer::toZpl(window_->document()) != originalZpl);
    const auto reopened = Designer::fromZpl(Designer::toZpl(window_->document()));
    QVERIFY(reopened.has_value());
    QVERIFY(reopened->visualEditable);
    undo_->trigger();
    QCOMPARE(Designer::toZpl(window_->document()), originalZpl);
    QVERIFY(!undo_->isEnabled());
}

void WindowTests::circleHeightChangesDiameter()
{
    auto document = initial_;
    document.elements = {Designer::makeElement(ElementType::Circle, {40, 40})};
    window_->setDocument(document);
    canvas_->setSelectedIndex(0);
    auto* height = window_->findChild<QSpinBox*>(QStringLiteral("elementHeight"));
    auto* width = window_->findChild<QSpinBox*>(QStringLiteral("elementWidth"));
    height->setValue(144);
    QCOMPARE(window_->document().elements.first().width, 144);
    QCOMPARE(window_->document().elements.first().height, 144);
    QCOMPARE(width->value(), 144);
    QCOMPARE(height->value(), 144);
    undo_->trigger();
    QCOMPARE(window_->document().elements.first().width, 100);
}

void WindowTests::previewActionAppliesPendingSourceAndFollowsEdits()
{
    auto* action = window_->findChild<QAction*>(QStringLiteral("previewAction"));
    auto* viewTabs = window_->findChild<QTabBar*>(QStringLiteral("workspaceViewTabs"));
    auto* tabs = window_->findChild<QTabWidget*>(QStringLiteral("editorTabs"));
    QVERIFY(action);
    QVERIFY(viewTabs);
    QVERIFY(viewTabs->isVisible());
    QCOMPARE(viewTabs->count(), 3);
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    auto* preview = window_->findChild<Designer::PreviewWidget*>(QStringLiteral("previewWidget"));
    QVERIFY(preview);
    QCOMPARE(tabs->widget(2), preview);
    QVERIFY(!preview->isWindow());
    QVERIFY(!preview->isVisible());
    const QString source = QStringLiteral("^XA\n^PW320\n^LL240\n^FO24,32^GB100,48,2^FS\n^XZ\n");
    code_->setPlainText(source);
    action->trigger();
    QCOMPARE(tabs->currentWidget(), preview);
    QCOMPARE(viewTabs->currentIndex(), 2);
    QVERIFY(preview->isVisible());
    QCOMPARE(Designer::toZpl(window_->document()), source);
    auto* view = preview->findChild<QGraphicsView*>(QStringLiteral("previewView"));
    QVERIFY(view);
    preview->findChild<QPushButton*>(QStringLiteral("previewActualSize"))->click();
    auto previewImage = [view] {
        QApplication::processEvents();
        const QImage screenshot = view->viewport()->grab().toImage();
        const qreal dpr = screenshot.devicePixelRatio();
        const QPoint origin = view->mapFromScene(QPointF(0, 0));
        const QSize size = view->sceneRect().size().toSize();
        auto dots = screenshot.copy(qRound(origin.x() * dpr), qRound(origin.y() * dpr),
            qRound(size.width() * dpr), qRound(size.height() * dpr)).scaled(size);
        // Pixels now represent printer dots, no longer physical screen pixels.
        dots.setDevicePixelRatio(1.0);
        return dots;
    };
    auto expected = Designer::renderPreview(window_->document());
    QVERIFY(expected.has_value());
    QCOMPARE(previewImage().convertToFormat(QImage::Format_RGB32), expected->image.convertToFormat(QImage::Format_RGB32));
    canvas_->setSelectedIndex(0);
    window_->findChild<QSpinBox*>(QStringLiteral("elementWidth"))->setValue(144);
    expected = Designer::renderPreview(window_->document());
    QVERIFY(expected.has_value());
    QCOMPARE(previewImage().convertToFormat(QImage::Format_RGB32), expected->image.convertToFormat(QImage::Format_RGB32));
    auto* exportButton = preview->findChild<QPushButton*>(QStringLiteral("previewExportPng"));
    QVERIFY(exportButton);
    code_->setPlainText(QStringLiteral("invalid ZPL"));
    QVERIFY(!exportButton->isEnabled());
    const auto committed = Designer::toZpl(window_->document());
    action->trigger();
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(viewTabs->currentIndex(), 1);
    QCOMPARE(Designer::toZpl(window_->document()), committed);
    QVERIFY(!exportButton->isEnabled());
}

void WindowTests::previewTabAppliesPendingSourceAndRejectsInvalidSource()
{
    auto* tabs = window_->findChild<QTabWidget*>(QStringLiteral("editorTabs"));
    auto* viewTabs = window_->findChild<QTabBar*>(QStringLiteral("workspaceViewTabs"));
    auto* preview = window_->findChild<Designer::PreviewWidget*>(QStringLiteral("previewWidget"));
    QVERIFY(tabs);
    QVERIFY(viewTabs);
    QVERIFY(viewTabs->isVisible());
    QVERIFY(preview);
    QCOMPARE(tabs->indexOf(preview), 2);
    QTest::mouseClick(viewTabs, Qt::LeftButton, Qt::NoModifier, viewTabs->tabRect(1).center());
    QCOMPARE(tabs->currentIndex(), 1);
    const QString source = QStringLiteral("^XA\n^PW280\n^LL200\n^FO16,24^GB88,40,2^FS\n^XZ\n");
    code_->setPlainText(source);
    QTest::mouseClick(viewTabs, Qt::LeftButton, Qt::NoModifier, viewTabs->tabRect(2).center());
    QCOMPARE(tabs->currentWidget(), preview);
    QCOMPARE(Designer::toZpl(window_->document()), source);
    auto* view = preview->findChild<QGraphicsView*>(QStringLiteral("previewView"));
    auto* exportButton = preview->findChild<QPushButton*>(QStringLiteral("previewExportPng"));
    QVERIFY(view);
    QVERIFY(exportButton);
    QCOMPARE(view->sceneRect().size(), QSizeF(280, 200));
    QVERIFY(exportButton->isEnabled());

    QTest::mouseClick(viewTabs, Qt::LeftButton, Qt::NoModifier, viewTabs->tabRect(1).center());
    QCOMPARE(tabs->currentIndex(), 1);
    const QString invalidSource = QStringLiteral("invalid ZPL");
    code_->setPlainText(invalidSource);
    QTest::mouseClick(viewTabs, Qt::LeftButton, Qt::NoModifier, viewTabs->tabRect(2).center());
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(viewTabs->currentIndex(), 1);
    QCOMPARE(code_->toPlainText(), invalidSource);
    QCOMPARE(Designer::toZpl(window_->document()), source);
    QVERIFY(!preview->isVisible());
    QVERIFY(!exportButton->isEnabled());
}

void WindowTests::previewRenderFailureClearsImage()
{
    window_->showPreview();
    auto* dialog = window_->findChild<Designer::PreviewWidget*>();
    QVERIFY(dialog);
    auto broken = initial_;
    broken.elements.first().width = 5000;
    window_->setDocument(broken);
    auto* exportButton = dialog->findChild<QPushButton*>(QStringLiteral("previewExportPng"));
    QVERIFY(exportButton);
    QVERIFY(!exportButton->isEnabled());
    auto* diagnostics = dialog->findChild<QPlainTextEdit*>(QStringLiteral("previewDiagnostics"));
    QVERIFY(diagnostics);
    QVERIFY(!diagnostics->toPlainText().isEmpty());
}

void WindowTests::immediateResizeAfterFontEditUsesFreshBounds()
{
    auto document = initial_;
    auto text = Designer::makeElement(ElementType::Text, {32, 32});
    text.text = QStringLiteral("AB");
    document.elements = {text};
    window_->setDocument(document);
    canvas_->setSelectedIndex(0);
    canvas_->actualSize();
    canvas_->setSnapEnabled(false);
    window_->findChild<QSpinBox*>(QStringLiteral("fontHeight"))->setValue(64);
    // Begin a new gesture without allowing the 90 ms preview timer to run.
    const auto expectedPreview = Designer::renderPreview(window_->document());
    QVERIFY(expectedPreview.has_value());
    const QRectF bounds(expectedPreview->bounds.first());
    const QPoint handle = canvas_->mapFromScene(QPointF(bounds.right(), bounds.center().y()));
    const QPoint destination = handle + QPoint(qRound(bounds.width()), 0);
    QTest::mousePress(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, handle);
    QTest::mouseRelease(canvas_->viewport(), Qt::LeftButton, Qt::NoModifier, destination);
    QCOMPARE(window_->document().elements.first().fontHeight, 64);
    QCOMPARE(window_->document().elements.first().fontWidth, 128);
    undo_->trigger();
    QCOMPARE(window_->document().elements.first().fontHeight, 64);
    QCOMPARE(window_->document().elements.first().fontWidth, 0);
}

void WindowTests::gs1PropertiesSaveAndRejectInvalidData()
{
    QVERIFY(!window_->findChild<QPushButton*>(QStringLiteral("addGs1DataMatrix")));
    window_->addElement(ElementType::DataMatrix);
    QCOMPARE(window_->document().elements.size(), 2);
    QVERIFY(window_->document().elements.last().gs1);
    QCOMPARE(canvas_->selectedIndex(), 1);
    auto* mode = window_->findChild<QCheckBox*>(QStringLiteral("dataMatrixGs1"));
    auto* content = window_->findChild<QPlainTextEdit*>(QStringLiteral("elementContent"));
    QVERIFY(mode->isVisible()); QVERIFY(mode->isChecked());
    content->setPlainText(QStringLiteral("(01)09506000134352(10)LOT1(21)SERIAL1"));
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("gs1.zpl"));
    QVERIFY(window_->saveFile(path));
    const auto source = Designer::toZpl(window_->document());
    QVERIFY(source.contains(QStringLiteral("^BXN,3,200,0,0,1,|^FH_^FD|1010950600013435210LOT1_1D21SERIAL1^FS")));
    content->setPlainText(QStringLiteral("(01)123"));
    QVERIFY(!window_->saveFile(path));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), source); file.close();
    QVERIFY(window_->openFile(path));
    QVERIFY(window_->document().visualEditable);
    QVERIFY(window_->document().elements.last().gs1);
    canvas_->setSelectedIndex(1);
    QVERIFY(mode->isChecked());
    mode->setChecked(false);
    QVERIFY(!window_->document().elements.last().gs1);
    undo_->trigger(); QVERIFY(window_->document().elements.last().gs1);
}

void WindowTests::barcodePaletteCreatesTwoFamilies()
{
    auto* palette = window_->findChild<QListWidget*>(QStringLiteral("elementPalette"));
    auto* type = window_->findChild<QComboBox*>(QStringLiteral("barcodeType"));
    QVERIFY(palette);
    QVERIFY(type);
    canvas_->setSelectedIndex(0);
    QVERIFY(!type->isVisible());

    const QList<ElementType> linearTypes{ElementType::Code128, ElementType::Ean13, ElementType::Ean8,
        ElementType::UpcA, ElementType::UpcE, ElementType::Code39, ElementType::Code93,
        ElementType::Codabar, ElementType::Interleaved2of5, ElementType::Industrial2of5, ElementType::Gs1DataBar};
    const QList<ElementType> matrixTypes{ElementType::DataMatrix, ElementType::QrCode, ElementType::Pdf417,
        ElementType::MicroPdf417, ElementType::Aztec, ElementType::MaxiCode};
    QList<ElementType> barcodeEntries;
    QList<ElementType> otherEntries;
    int pointers = 0;
    for (int row = 0; row < palette->count(); ++row) {
        auto* item = palette->item(row);
        const int value = item->data(Qt::UserRole).toInt();
        if (value < 0) {
            QCOMPARE(value, -1);
            ++pointers;
            const auto source = Designer::toZpl(window_->document());
            palette->itemDoubleClicked(item);
            QCOMPARE(Designer::toZpl(window_->document()), source);
            QVERIFY(!undo_->isEnabled());
            continue;
        }
        const auto elementType = static_cast<ElementType>(value);
        if (!Designer::isBarcodeType(elementType)) {
            otherEntries.append(elementType);
            continue;
        }
        barcodeEntries.append(elementType);
        QCOMPARE(item->text(), elementType == ElementType::Code128
            ? QStringLiteral("1D-код") : QStringLiteral("2D-код"));
        palette->itemDoubleClicked(item);
        QCOMPARE(window_->document().elements.size(), 2);
        QCOMPARE(window_->document().elements.last().type, elementType);
        QCOMPARE(window_->document().elements.last().gs1, elementType == ElementType::DataMatrix);
        QCOMPARE(canvas_->selectedIndex(), 1);
        QVERIFY(type->isVisible());
        QVERIFY(type->isEnabled());
        QCOMPARE(type->currentData().toInt(), static_cast<int>(elementType));
        const auto expectedTypes = elementType == ElementType::Code128 ? linearTypes : matrixTypes;
        QCOMPARE(type->count(), expectedTypes.size());
        for (const auto expected : expectedTypes)
            QVERIFY2(type->findData(static_cast<int>(expected)) >= 0, qPrintable(Designer::elementTypeName(expected)));
        QVERIFY(Designer::validateForExport(window_->document()));
        undo_->trigger();
        QCOMPARE(window_->document().elements.size(), 1);
        QVERIFY(!undo_->isEnabled());
    }
    QCOMPARE(pointers, 1);
    QCOMPARE(barcodeEntries, (QList<ElementType>{ElementType::Code128, ElementType::DataMatrix}));
    QCOMPARE(otherEntries, (QList<ElementType>{ElementType::Text, ElementType::Box, ElementType::Line,
        ElementType::Circle, ElementType::Ellipse, ElementType::Image, ElementType::Table, ElementType::Diagonal}));
}

void WindowTests::barcodeTypePreservesContentAndHistory()
{
    auto document = initial_;
    auto element = Designer::makeElement(ElementType::Code128, {72, 56});
    element.text = QStringLiteral("CUSTOM-123");
    element.name = QStringLiteral("Код партии");
    element.groupId = QUuid::createUuid();
    element.literalText = true;
    element.editorVisible = false;
    element.printable = false;
    element.moduleWidth = 4;
    element.height = 108;
    element.orientation = u'R';
    document.elements = {element};
    window_->setDocument(document);
    canvas_->setSelectedIndex(0);
    auto* type = window_->findChild<QComboBox*>(QStringLiteral("barcodeType"));
    QVERIFY(type);
    type->setCurrentIndex(type->findData(static_cast<int>(ElementType::Code93)));
    const auto switched = window_->document().elements.first();
    QCOMPARE(switched.type, ElementType::Code93);
    QCOMPARE(switched.text, element.text);
    QCOMPARE(switched.name, element.name);
    QCOMPARE(switched.id, element.id);
    QCOMPARE(switched.groupId, element.groupId);
    QCOMPARE(switched.literalText, element.literalText);
    QCOMPARE(switched.editorVisible, element.editorVisible);
    QCOMPARE(switched.printable, element.printable);
    QCOMPARE(switched.locked, element.locked);
    QCOMPARE(QPoint(switched.x, switched.y), QPoint(element.x, element.y));
    QCOMPARE(switched.moduleWidth, element.moduleWidth);
    QCOMPARE(switched.height, element.height);
    QCOMPARE(switched.orientation, element.orientation);
    QCOMPARE(canvas_->selectedIndex(), 0);
    QVERIFY(undo_->isEnabled());
    undo_->trigger();
    QCOMPARE(window_->document().elements.first().type, ElementType::Code128);
    QCOMPARE(window_->document().elements.first().text, element.text);
    QCOMPARE(type->currentData().toInt(), static_cast<int>(ElementType::Code128));
    QVERIFY(!undo_->isEnabled());
    redo_->trigger();
    QCOMPARE(window_->document().elements.first().type, ElementType::Code93);
    QCOMPARE(type->currentData().toInt(), static_cast<int>(ElementType::Code93));
    QCOMPARE(window_->document().elements.first().id, element.id);
}

void WindowTests::barcodeTypeAppliesFormatConstraints()
{
    auto* type = window_->findChild<QComboBox*>(QStringLiteral("barcodeType"));
    QVERIFY(type);
    for (const auto target : {ElementType::Code39, ElementType::Codabar}) {
        auto document = initial_;
        auto element = Designer::makeElement(ElementType::Code128);
        element.text = QStringLiteral("123456");
        element.interpretationAbove = true;
        document.elements = {element};
        window_->setDocument(document);
        canvas_->setSelectedIndex(0);
        type->setCurrentIndex(type->findData(static_cast<int>(target)));
        QCOMPARE(window_->document().elements.first().type, target);
        QVERIFY(!window_->document().elements.first().humanReadable);
        QVERIFY(!window_->document().elements.first().interpretationAbove);
        QCOMPARE(window_->document().elements.first().text, element.text);
        undo_->trigger();
        QVERIFY(window_->document().elements.first().humanReadable);
        QVERIFY(window_->document().elements.first().interpretationAbove);
        QVERIFY(!undo_->isEnabled());
    }

    window_->setDocument(initial_);
    window_->addElement(ElementType::DataMatrix);
    const auto original = window_->document().elements.last();
    type->setCurrentIndex(type->findData(static_cast<int>(ElementType::QrCode)));
    QVERIFY(!window_->document().elements.last().gs1);
    QCOMPARE(window_->document().elements.last().text, original.text);
    QVERIFY(!window_->findChild<QCheckBox*>(QStringLiteral("dataMatrixGs1"))->isVisible());
    undo_->trigger();
    QVERIFY(window_->document().elements.last().gs1);
    redo_->trigger();
    type->setCurrentIndex(type->findData(static_cast<int>(ElementType::DataMatrix)));
    QVERIFY(!window_->document().elements.last().gs1);
    QCOMPARE(window_->document().elements.last().text, original.text);

    auto document = initial_;
    auto matrix = Designer::makeElement(ElementType::QrCode, {24, 32});
    matrix.orientation = u'R';
    matrix.useDefaultOrientation = true;
    document.elements = {matrix};
    window_->setDocument(document);
    canvas_->setSelectedIndex(0);
    type->setCurrentIndex(type->findData(static_cast<int>(ElementType::MaxiCode)));
    const auto& maxi = window_->document().elements.first();
    QCOMPARE(maxi.type, ElementType::MaxiCode);
    QCOMPARE(maxi.orientation, QChar(u'N'));
    QVERIFY(!maxi.useDefaultOrientation);
    QCOMPARE(QSize(maxi.width, maxi.height), QSize(201, 195));
    QCOMPARE(maxi.text, matrix.text);
    undo_->trigger();
    QCOMPARE(window_->document().elements.first().orientation, QChar(u'R'));
    QVERIFY(window_->document().elements.first().useDefaultOrientation);
    QVERIFY(!undo_->isEnabled());
}

void WindowTests::barcodeTypeRejectsIneligibleEdits()
{
    auto document = initial_;
    document.elements.append(Designer::makeElement(ElementType::Code128));
    auto* type = window_->findChild<QComboBox*>(QStringLiteral("barcodeType"));
    QVERIFY(type);
    window_->setDocument(document);
    canvas_->setSelectedIndex(1);
    canvas_->setSelectedIndices({0, 1});
    QVERIFY(!type->isVisible());
    type->setCurrentIndex(type->findData(static_cast<int>(ElementType::Code93)));
    QCOMPARE(window_->document().elements.last().type, ElementType::Code128);
    QVERIFY(!undo_->isEnabled());

    document.elements.last().locked = true;
    window_->setDocument(document);
    canvas_->setSelectedIndex(1);
    QVERIFY(!type->isEnabled());
    type->setCurrentIndex(type->findData(static_cast<int>(ElementType::Code93)));
    QCOMPARE(window_->document().elements.last().type, ElementType::Code128);
    QVERIFY(!undo_->isEnabled());

    document.elements.last().locked = false;
    window_->setDocument(document);
    canvas_->setSelectedIndex(1);
    const auto committed = Designer::toZpl(window_->document());
    const QString pending = QStringLiteral("^XA^PW360^LL200^XZ");
    code_->setPlainText(pending);
    QVERIFY(!type->isEnabled());
    type->setCurrentIndex(type->findData(static_cast<int>(ElementType::Code93)));
    QCOMPARE(Designer::toZpl(window_->document()), committed);
    QCOMPARE(code_->toPlainText(), pending);
    QVERIFY(!undo_->isEnabled());
}

void WindowTests::barcodeTypePreservesImportedCommentsAndSavedRendering()
{
    const auto imported = Designer::fromZpl(QStringLiteral(
        "^XA\n^PW600\n^LL300\n^FO8,8^GB8,8,1^FS\n^FXKeep this field note^FS\n"
        "^FO24,32^BY2,3,80^BCN,80,Y,N,N,A^FD12345678^FS\n^XZ\n"));
    QVERIFY(imported);
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    QCOMPARE(imported->elements.size(), 2);
    window_->setDocument(*imported);
    canvas_->setSelectedIndex(1);
    auto* type = window_->findChild<QComboBox*>(QStringLiteral("barcodeType"));
    QVERIFY(type);
    type->setCurrentIndex(type->findData(static_cast<int>(ElementType::Code93)));
    QCOMPARE(window_->document().elements.last().sourcePrefix, imported->elements.last().sourcePrefix);
    const auto source = Designer::toZpl(window_->document());
    QVERIFY(source.contains(QStringLiteral("^FXKeep this field note^FS")));
    QVERIFY(source.contains(QStringLiteral("^BA")));
    QVERIFY(!source.contains(QStringLiteral("^BC")));
    const auto preview = Designer::renderPreview(window_->document());
    QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("converted.zpl"));
    QVERIFY(window_->saveFile(path));
    QVERIFY(window_->openFile(path));
    QVERIFY(window_->document().visualEditable);
    QCOMPARE(window_->document().elements.last().type, ElementType::Code93);
    QCOMPARE(window_->document().elements.last().text, QStringLiteral("12345678"));
    QCOMPARE(Designer::toZpl(window_->document()), source);
    const auto reopened = Designer::renderPreview(window_->document());
    QVERIFY2(reopened.has_value(), reopened ? "" : qPrintable(reopened.error()));
    QCOMPARE(reopened->image, preview->image);
}

void WindowTests::millimetreFormatIsUndoableAndKeepsElementGeometry()
{
    auto* labelSetup = window_->findChild<QPushButton*>(QStringLiteral("labelSetup"));
    auto* format = window_->findChild<QPushButton*>(QStringLiteral("labelFormatSetup"));
    auto* inspector = window_->findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
    QVERIFY(labelSetup); QVERIFY(format); QVERIFY(inspector);
    labelSetup->click();
    QCOMPARE(inspector->currentIndex(), 1);
    QVERIFY(format->isVisible());
    bool dialogSeen = false;
    QTimer::singleShot(0, window_.get(), [&] {
        auto* dialog = window_->findChild<QDialog*>(QStringLiteral("labelSetupDialog"));
        if (!dialog) return;
        dialogSeen = true;
        dialog->findChild<QComboBox*>(QStringLiteral("labelSetupDpi"))->setCurrentIndex(1);
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("labelWidthMm"))->setValue(58);
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("labelHeightMm"))->setValue(40);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    format->click();
    QVERIFY(dialogSeen);
    QCOMPARE(window_->document().dpi, 300);
    QCOMPARE(window_->document().width, qRound(58.0 * 300 / 25.4));
    QCOMPARE(window_->document().height, qRound(40.0 * 300 / 25.4));
    QCOMPARE(window_->document().elements.first().x, initial_.elements.first().x);
    QCOMPARE(window_->document().elements.first().width, initial_.elements.first().width);
    undo_->trigger();
    QCOMPARE(window_->document().width, initial_.width);
    QCOMPARE(window_->document().dpi, initial_.dpi);
}

void WindowTests::scaledLabelFollowsDpiAndFormat()
{
    auto* scale = window_->findChild<QCheckBox*>(QStringLiteral("scaleLabelContent"));
    auto* dpi = window_->findChild<QComboBox*>(QStringLiteral("dpi"));
    auto* format = window_->findChild<QPushButton*>(QStringLiteral("labelFormatSetup"));
    QVERIFY(scale); QVERIFY(dpi); QVERIFY(format);
    QVERIFY(!scale->isChecked());
    scale->setChecked(true);
    {
        const QSettings saved(QDir(workspaceSettingsDirectory_).filePath(QStringLiteral("workspace.ini")), QSettings::IniFormat);
        QVERIFY(saved.value(QStringLiteral("label/scaleContent")).toBool());
    }

    // Another resolution keeps the millimetres; the box follows the label.
    dpi->setCurrentIndex(dpi->findData(300));
    const auto& before = initial_.elements.first();
    QCOMPARE(window_->document().dpi, 300);
    QCOMPARE(window_->document().width, qRound(initial_.width * 300.0 / 203));
    QCOMPARE(window_->document().height, qRound(initial_.height * 300.0 / 203));
    const double scaleX = double(window_->document().width) / initial_.width;
    QCOMPARE(window_->document().elements.first().x, qRound(before.x * scaleX));
    QCOMPARE(window_->document().elements.first().width,
             qRound((before.x + before.width) * scaleX) - qRound(before.x * scaleX));
    undo_->trigger();
    QCOMPARE(window_->document().dpi, 203);
    QCOMPARE(window_->document().width, initial_.width);
    QCOMPARE(window_->document().elements.first().x, before.x);
    QCOMPARE(dpi->currentData().toInt(), 203);

    bool dialogSeen = false;
    QTimer::singleShot(0, window_.get(), [&] {
        auto* dialog = window_->findChild<QDialog*>(QStringLiteral("labelSetupDialog"));
        if (!dialog) return;
        auto* dialogScale = dialog->findChild<QCheckBox*>(QStringLiteral("labelSetupScaleContent"));
        dialogSeen = dialogScale && dialogScale->isChecked();
        auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("labelWidthMm"));
        width->setValue(width->value() * 2);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    format->click();
    QVERIFY(dialogSeen);
    QCOMPARE(window_->document().width, 2 * initial_.width);
    QCOMPARE(window_->document().height, initial_.height);
    QCOMPARE(window_->document().elements.first().x, 2 * before.x);
    QCOMPARE(window_->document().elements.first().width, 2 * before.width);
    QCOMPARE(window_->document().elements.first().height, before.height);
}

void WindowTests::newDocumentIs100MillimetresAt300Dpi()
{
    auto* fresh = window_->findChild<QAction*>(QStringLiteral("newDocumentAction"));
    QVERIFY(fresh);
    fresh->trigger();
    QCOMPARE(window_->document().dpi, 300);
    QCOMPARE(window_->document().width, qRound(100.0 * 300 / 25.4));
    QCOMPARE(window_->document().height, qRound(100.0 * 300 / 25.4));
    QVERIFY(window_->document().elements.isEmpty());
    auto* dpi = window_->findChild<QComboBox*>(QStringLiteral("dpi"));
    QVERIFY(dpi);
    QCOMPARE(dpi->currentData().toInt(), 300);
}

void WindowTests::groupActionsPreserveSelectionAndHistory()
{
    auto document = initial_;
    document.elements.append(Designer::makeElement(ElementType::Box, {180, 150}));
    window_->setDocument(document);
    canvas_->setSelectedIndices({0, 1});
    auto* layers = window_->findChild<QListWidget*>(QStringLiteral("layerList"));
    QCOMPARE(layers->selectedItems().size(), 2);
    QVERIFY(!window_->findChild<QSpinBox*>(QStringLiteral("elementX"))->isVisible());
    window_->findChild<QAction*>(QStringLiteral("alignLeft"))->trigger();
    QCOMPARE(window_->document().elements[1].x, 40);
    window_->refreshPreview();
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{0, 1}));
    QCOMPARE(layers->selectedItems().size(), 2);
    undo_->trigger();
    QCOMPARE(window_->document().elements[1].x, 180);
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{0, 1}));
    QTest::keyClick(canvas_, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(window_->document().elements[0].x, 50);
    QCOMPARE(window_->document().elements[1].x, 190);
    undo_->trigger();
    QCOMPARE(window_->document().elements[0].x, 40);
    QCOMPARE(window_->document().elements[1].x, 180);
    window_->findChild<QAction*>(QStringLiteral("duplicateElements"))->trigger();
    QCOMPARE(window_->document().elements.size(), 4);
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{2, 3}));
    window_->findChild<QAction*>(QStringLiteral("deleteElements"))->trigger();
    QCOMPARE(window_->document().elements.size(), 2);
    QVERIFY(canvas_->selectedIndices().isEmpty());
    undo_->trigger(); QCOMPARE(window_->document().elements.size(), 4);
    undo_->trigger(); QCOMPARE(window_->document().elements.size(), 2);
}

void WindowTests::pendingSourceBlocksGroupActions()
{
    window_->addElement(ElementType::Box, {160, 120});
    canvas_->setSelectedIndices({0, 1});
    const auto committed = Designer::toZpl(window_->document());
    const QString pending = QStringLiteral("^XA^PW360^LL200^XZ");
    code_->setPlainText(pending);
    for (const auto name : {"alignLeft", "duplicateElements", "deleteElements"})
        window_->findChild<QAction*>(QLatin1String(name))->trigger();
    QTest::keyClick(canvas_, Qt::Key_Right);
    QCOMPARE(Designer::toZpl(window_->document()), committed);
    QCOMPARE(code_->toPlainText(), pending);
}

void WindowTests::layerNavigationTracksCanvasSelection()
{
    window_->addElement(ElementType::Box, {180, 150});
    auto* layers = window_->findChild<QListWidget*>(QStringLiteral("layerList"));
    QVERIFY(layers->isVisible());
    canvas_->setSelectedIndex(0);
    QCOMPARE(layers->currentRow(), 1);
    layers->setFocus();
    QTest::keyClick(layers, Qt::Key_Up);
    QCOMPARE(canvas_->selectedIndex(), 1);
    QTest::mouseClick(layers->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      layers->visualItemRect(layers->item(1)).center());
    QCOMPARE(canvas_->selectedIndices().size(), 2);
    QCOMPARE(canvas_->selectedIndex(), 0);
    window_->refreshPreview();
    QCOMPARE(layers->currentRow(), 1);
    QCOMPARE(layers->selectedItems().size(), 2);
    window_->findChild<QAction*>(QStringLiteral("alignLeft"))->trigger();
    QCOMPARE(layers->currentRow(), 1);
    QCOMPARE(layers->selectedItems().size(), 2);
}

void WindowTests::dataMatrixTemplateValuesUpdatePreviewWithoutReplacingSource()
{
    window_->addElement(ElementType::DataMatrix);
    QVERIFY(window_->document().elements.last().gs1);
    auto* content = window_->findChild<QPlainTextEdit*>(QStringLiteral("elementContent"));
    const QString templated = QStringLiteral("(01){{gtin}}(21)SN-[[serial]]");
    content->setPlainText(templated);
    window_->refreshPreview();
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("template.zpl"));
    QVERIFY(window_->saveFile(path));
    const auto originalSource = Designer::toZpl(window_->document());
    bool dialogSeen = false;
    QTimer::singleShot(0, window_.get(), [&] {
        auto* dialog = window_->findChild<Designer::PlaceholderValuesDialog*>();
        if (!dialog) return;
        dialogSeen = true;
        auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("placeholderValuesTable"));
        for (int row = 0; row < table->rowCount(); ++row) {
            auto* edit = qobject_cast<QLineEdit*>(table->cellWidget(row, 1));
            edit->setText(table->item(row, 0)->data(Qt::UserRole).toString() == QStringLiteral("gtin")
                ? QStringLiteral("09506000134352") : QStringLiteral("PREVIEW123"));
        }
        dialog->findChild<QPushButton*>(QStringLiteral("placeholderValuesApply"))->click();
    });
    window_->findChild<QPushButton*>(QStringLiteral("placeholderValues"))->click();
    QVERIFY(dialogSeen);
    QCOMPARE(window_->document().elements.last().text, templated);
    QCOMPARE(Designer::toZpl(window_->document()), originalSource);
    QVERIFY(!window_->windowTitle().contains(QStringLiteral(" *")));
    const auto preview = Designer::renderPreview(window_->document());
    QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
    QVERIFY(window_->saveFile(path));
    QVERIFY(window_->openFile(path));
    QVERIFY(window_->document().visualEditable);
    QCOMPARE(window_->document().elements.last().text, templated);
    QVERIFY(window_->document().previewValues.isEmpty());
}

void WindowTests::propertyUnitsReachGeometryAndAdvancedTextFields()
{
    auto document = initial_;
    auto text = Designer::makeElement(ElementType::Text, {40, 48});
    text.text = QStringLiteral("Operator label");
    text.fontHeight = 24;
    text.fontWidth = 20;
    text.blockWidth = 220;
    text.blockMaxLines = 2;
    text.blockLineSpacing = 8;
    text.blockHangingIndent = 16;
    document.elements = {text};
    window_->setDocument(document);
    canvas_->setSelectedIndex(0);
    auto* units = window_->findChild<QComboBox*>(QStringLiteral("propertyUnits"));
    auto* advanced = window_->findChild<QToolButton*>(QStringLiteral("advancedPropertiesToggle"));
    auto* session = window_->findChild<Designer::DocumentSession*>();
    QVERIFY(units); QVERIFY(advanced); QVERIFY(session);
    advanced->setChecked(true);
    QList<Designer::DotSpinBox*> fields;
    for (const auto* name : {"elementWidth", "elementHeight",
                             "fontWidth", "blockWidth", "blockLineSpacing", "blockHangingIndent"}) {
        auto* field = window_->findChild<Designer::DotSpinBox*>(QString::fromLatin1(name));
        QVERIFY2(field, name);
        field->setLocale(QLocale::c());
        fields.append(field);
    }
    // These are the printer-dot numbers written to ZPL whatever units sizes use.
    QList<Designer::DotSpinBox*> dotFields;
    for (const auto* name : {"elementX", "elementY", "fontHeight"}) {
        auto* field = window_->findChild<Designer::DotSpinBox*>(QString::fromLatin1(name));
        QVERIFY2(field, name);
        field->setLocale(QLocale::c());
        dotFields.append(field);
    }
    const QString original = Designer::toZpl(window_->document());
    QSignalSpy documentChanges(session, &Designer::DocumentSession::documentChanged);
    // Fields now live in several sections. Units must reach both the two-column
    // geometry and the expanded details without rewriting any printer dots.
    for (int repetition = 0; repetition < 2; ++repetition) {
        units->setCurrentIndex(units->findData(static_cast<int>(Designer::CanvasUnit::Dots)));
        for (auto* field : fields)
            QCOMPARE(field->cleanText(), QString::number(field->value()));
        units->setCurrentIndex(units->findData(static_cast<int>(Designer::CanvasUnit::Millimeters)));
        for (auto* field : fields)
            QCOMPARE(field->cleanText(), QString::number(field->value() * 25.4 / document.dpi, 'f', 3));
        // Coordinates and the font height explicitly say dots and keep those units.
        for (auto* field : dotFields)
            QCOMPARE(field->cleanText(), QString::number(field->value()));
        QCOMPARE(dotFields[0]->value(), text.x);
        QCOMPARE(dotFields[1]->value(), text.y);
        QCOMPARE(Designer::toZpl(window_->document()), original);
    }
    QCOMPARE(documentChanges.count(), 0);

    auto* fontWidth = window_->findChild<Designer::DotSpinBox*>(QStringLiteral("fontWidth"));
    QVERIFY(fontWidth->isVisible());
    auto* editor = fontWidth->findChild<QLineEdit*>();
    QVERIFY(editor);
    editor->selectAll();
    QTest::keyClicks(editor, "5.08");
    QTest::keyClick(editor, Qt::Key_Return);
    QCOMPARE(window_->document().elements.first().fontWidth, qRound(5.08 * document.dpi / 25.4));
    QVERIFY(Designer::toZpl(window_->document()) != original);
    undo_->trigger();
    QCOMPARE(window_->document().elements.first().fontWidth, text.fontWidth);
    QCOMPARE(Designer::toZpl(window_->document()), original);

    // Still in millimetres: a typed coordinate is taken as printer dots.
    auto* xEditor = dotFields[0]->findChild<QLineEdit*>();
    QVERIFY(xEditor);
    xEditor->selectAll();
    QTest::keyClicks(xEditor, "56");
    QTest::keyClick(xEditor, Qt::Key_Return);
    QCOMPARE(window_->document().elements.first().x, 56);
    undo_->trigger();
    QCOMPARE(Designer::toZpl(window_->document()), original);
}

void WindowTests::labelFormatFollowsPropertyUnits()
{
    auto* units = window_->findChild<QComboBox*>(QStringLiteral("propertyUnits"));
    auto* inspector = window_->findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
    auto* width = window_->findChild<Designer::DotSpinBox*>(QStringLiteral("labelWidth"));
    auto* height = window_->findChild<Designer::DotSpinBox*>(QStringLiteral("labelHeight"));
    auto* dpi = window_->findChild<QComboBox*>(QStringLiteral("dpi"));
    QVERIFY(units); QVERIFY(inspector); QVERIFY(width); QVERIFY(height); QVERIFY(dpi);
    width->setLocale(QLocale::c());
    height->setLocale(QLocale::c());
    const QString original = Designer::toZpl(window_->document());
    const auto millimetres = [](int dots, int resolution) { return QString::number(dots * 25.4 / resolution, 'f', 3); };

    units->setCurrentIndex(units->findData(static_cast<int>(Designer::CanvasUnit::Dots)));
    QCOMPARE(width->cleanText(), QString::number(initial_.width));
    QCOMPARE(height->cleanText(), QString::number(initial_.height));
    // The Label page has no unit selector of its own, so the suffix stays visible.
    QVERIFY(width->text().endsWith(QStringLiteral(" dots")));
    units->setCurrentIndex(units->findData(static_cast<int>(Designer::CanvasUnit::Millimeters)));
    QCOMPARE(width->cleanText(), millimetres(initial_.width, initial_.dpi));
    QCOMPARE(height->cleanText(), millimetres(initial_.height, initial_.dpi));
    QVERIFY(width->text().endsWith(QStringLiteral(" мм")));
    QCOMPARE(Designer::toZpl(window_->document()), original);

    // Millimetres typed for the label are stored as whole printer dots.
    inspector->setCurrentIndex(1);
    auto* editor = width->findChild<QLineEdit*>();
    QVERIFY(editor);
    editor->selectAll();
    QTest::keyClicks(editor, "50.8");
    QTest::keyClick(editor, Qt::Key_Return);
    QCOMPARE(window_->document().width, qRound(50.8 * initial_.dpi / 25.4));
    undo_->trigger();
    QCOMPARE(window_->document().width, initial_.width);

    // A new resolution keeps the dots and recalculates the millimetre text.
    dpi->setCurrentIndex(dpi->findData(300));
    QCOMPARE(window_->document().dpi, 300);
    QCOMPARE(width->value(), initial_.width);
    QCOMPARE(width->cleanText(), millimetres(initial_.width, 300));
    undo_->trigger();
    QCOMPARE(window_->document().dpi, initial_.dpi);
    QCOMPARE(width->cleanText(), millimetres(initial_.width, initial_.dpi));
    QCOMPARE(Designer::toZpl(window_->document()), original);
}

void WindowTests::importedBlockAndCode128PropertiesAreEditable()
{
    const auto imported = Designer::fromZpl(QStringLiteral(
        "^XA^FXdemo^CI28^PW600^LL1200^LH0,0^PON"
        "^FO26,510^A0B,26,33^FB650,1,0,C,0^FDItem {{batch}}\\&^FS"
        "^BY3^FO252,198^BCB,178,N,N,N,D^FD(01){{gtin}}(10){{batch}}^FS^PQ1^XZ"));
    QVERIFY(imported);
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    code_->setPlainText(Designer::toZpl(*imported));
    QVERIFY(window_->applySource());
    canvas_->setSelectedIndex(0);
    auto* advanced = window_->findChild<QToolButton*>(QStringLiteral("advancedPropertiesToggle"));
    QVERIFY(advanced);
    advanced->setChecked(true);
    auto* blockWidth = window_->findChild<QSpinBox*>(QStringLiteral("blockWidth"));
    auto* alignment = window_->findChild<QComboBox*>(QStringLiteral("blockAlignment"));
    QVERIFY(blockWidth && alignment);
    QVERIFY(blockWidth->isVisible());
    QVERIFY(alignment->isVisible());
    QCOMPARE(blockWidth->value(), 650);
    QCOMPARE(alignment->currentData().toString(), QStringLiteral("C"));
    blockWidth->setValue(640);
    QCOMPARE(window_->document().elements[0].blockWidth, 640);
    QVERIFY(Designer::toZpl(window_->document()).contains(QStringLiteral("^FB640,1,0,C,0")));
    undo_->trigger();
    QCOMPARE(window_->document().elements[0].blockWidth, 650);
    canvas_->setSelectedIndex(1);
    auto* mode = window_->findChild<QComboBox*>(QStringLiteral("code128Mode"));
    QVERIFY(mode);
    QCOMPARE(mode->currentData().toString(), QStringLiteral("D"));
    mode->setCurrentIndex(mode->findData(QStringLiteral("A")));
    QCOMPARE(window_->document().elements[1].code128Mode, QChar(u'A'));
    undo_->trigger();
    QCOMPARE(window_->document().elements[1].code128Mode, QChar(u'D'));
    QVERIFY(window_->findChild<QPushButton*>(QStringLiteral("placeholderValues"))->isVisible());
    auto* values = window_->findChild<QAction*>(QStringLiteral("placeholderValuesAction"));
    QVERIFY(values);
    bool valuesSeen = false;
    QTimer::singleShot(0, window_.get(), [&] {
        auto* dialog = window_->findChild<Designer::PlaceholderValuesDialog*>();
        if (!dialog) return;
        auto* batch = dialog->findChild<QLineEdit*>(QStringLiteral("placeholderValue_batch"));
        auto* gtin = dialog->findChild<QLineEdit*>(QStringLiteral("placeholderValue_gtin"));
        if (!batch || !gtin) { dialog->reject(); return; }
        batch->setText(QStringLiteral("B42"));
        gtin->setText(QStringLiteral("09506000134352"));
        auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("placeholderValuesApply"));
        valuesSeen = apply && apply->isEnabled();
        if (valuesSeen) apply->click(); else dialog->reject();
    });
    values->trigger();
    QVERIFY(valuesSeen);
    QCOMPARE(window_->document().previewValues.value(QStringLiteral("batch")), QStringLiteral("B42"));
    QCOMPARE(window_->document().elements[0].text, imported->elements[0].text);
    const auto preview = Designer::renderPreview(window_->document());
    QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
}

void WindowTests::printActionOpensBothModesAndConnectionsWithoutSubmitting()
{
    const PrintTestSettings settings;
    auto* action = window_->findChild<QAction*>(QStringLiteral("printAction"));
    QVERIFY(action);
    QCOMPARE(action->shortcut(), QKeySequence(QKeySequence::Print));
    bool dialogSeen = false;
    QTimer::singleShot(0, window_.get(), [&] {
        auto* dialog = window_->findChild<Designer::PrintDialog*>();
        if (!dialog) return;
        auto* connection = dialog->findChild<QComboBox*>(QStringLiteral("printDestination"));
        auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("printMode"));
        dialogSeen = connection && connection->count() == 2 && mode && mode->count() == 2;
        dialog->reject();
    });
    action->trigger();
    QVERIFY(dialogSeen);
    QCOMPARE(Designer::toZpl(window_->document()), Designer::toZpl(initial_));
}

void WindowTests::printDialogModesFollowConnection()
{
    const PrintTestSettings settings;
    Designer::PrintDialog dialog(initial_);
    auto* connection = dialog.findChild<QComboBox*>(QStringLiteral("printDestination"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("printMode"));
    auto* explanation = dialog.findChild<QLabel*>(QStringLiteral("printExplanation"));
    QVERIFY(connection && mode && explanation);
    QCOMPARE(connection->currentData().toInt(), int(Designer::PrintConnection::InstalledPrinter));
    QCOMPARE(mode->count(), 2);
    QCOMPARE(mode->itemText(0), QStringLiteral("PNG"));
    QCOMPARE(mode->itemData(0).toInt(), int(Designer::LabelPrintMode::Image));
    QCOMPARE(mode->itemText(1), QStringLiteral("ZPL"));
    QCOMPARE(mode->itemData(1).toInt(), int(Designer::LabelPrintMode::Zpl));
    QVERIFY(explanation->text().contains(QStringLiteral("драйвер")));

    connection->setCurrentIndex(connection->findData(int(Designer::PrintConnection::Tcp)));
    QCOMPARE(mode->count(), 3);
    QCOMPARE(mode->itemText(0), QStringLiteral("ZPL"));
    QCOMPARE(mode->itemData(0).toInt(), int(Designer::LabelPrintMode::Zpl));
    QCOMPARE(mode->itemText(1), QStringLiteral("PNG"));
    QCOMPARE(mode->itemData(1).toInt(), int(Designer::LabelPrintMode::Png));
    QCOMPARE(mode->itemText(2), QStringLiteral("Преобразование в ZPL"));
    QCOMPARE(mode->itemData(2).toInt(), int(Designer::LabelPrintMode::Image));
    QCOMPARE(mode->currentData().toInt(), int(Designer::LabelPrintMode::Image));
    QVERIFY(explanation->text().contains(QStringLiteral("^GFA")));

    mode->setCurrentIndex(mode->findData(int(Designer::LabelPrintMode::Png)));
    QVERIFY(explanation->text().contains(QStringLiteral("PNG")));
    connection->setCurrentIndex(connection->findData(int(Designer::PrintConnection::InstalledPrinter)));
    QCOMPARE(mode->count(), 2);
    QCOMPARE(mode->findData(int(Designer::LabelPrintMode::Png)), -1);
    QCOMPARE(mode->currentData().toInt(), int(Designer::LabelPrintMode::Image));
    QVERIFY(explanation->text().contains(QStringLiteral("драйвер")));

    mode->setCurrentIndex(mode->findData(int(Designer::LabelPrintMode::Zpl)));
    connection->setCurrentIndex(connection->findData(int(Designer::PrintConnection::Tcp)));
    QCOMPARE(mode->currentData().toInt(), int(Designer::LabelPrintMode::Zpl));
    connection->setCurrentIndex(connection->findData(int(Designer::PrintConnection::InstalledPrinter)));
    QCOMPARE(mode->currentData().toInt(), int(Designer::LabelPrintMode::Zpl));
    dialog.reject();
    QVERIFY(!QFile::exists(settings.preferencesPath()));
}

void WindowTests::printDialogRestoresPngPreferences()
{
    const PrintTestSettings isolatedSettings;
    {
        QSettings preferences(isolatedSettings.preferencesPath(), QSettings::IniFormat);
        preferences.setValue(QStringLiteral("connection"), int(Designer::PrintConnection::Tcp));
        preferences.setValue(QStringLiteral("mode"), int(Designer::LabelPrintMode::Png));
        preferences.setValue(QStringLiteral("host"), QStringLiteral("192.0.2.15"));
        preferences.setValue(QStringLiteral("port"), 9200);
        preferences.sync();
        QCOMPARE(preferences.status(), QSettings::NoError);
    }
    Designer::PrintDialog dialog(initial_);
    auto* connection = dialog.findChild<QComboBox*>(QStringLiteral("printDestination"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("printMode"));
    auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("printHost"));
    auto* port = dialog.findChild<QSpinBox*>(QStringLiteral("printPort"));
    auto* repeats = dialog.findChild<QSpinBox*>(QStringLiteral("printRepeats"));
    QVERIFY(connection && mode && host && port && repeats);
    QCOMPARE(connection->currentData().toInt(), int(Designer::PrintConnection::Tcp));
    QCOMPARE(mode->count(), 3);
    QCOMPARE(mode->currentData().toInt(), int(Designer::LabelPrintMode::Png));
    QCOMPARE(host->text(), QStringLiteral("192.0.2.15"));
    QCOMPARE(port->value(), 9200);
    QCOMPARE(repeats->value(), 1);
    dialog.reject();
    const QSettings preferences(isolatedSettings.preferencesPath(), QSettings::IniFormat);
    QCOMPARE(preferences.value(QStringLiteral("mode")).toInt(), int(Designer::LabelPrintMode::Png));
}

void WindowTests::printDialogLoadsAndSavesPngProfileWithoutSubmitting()
{
    const PrintTestSettings settings;
    Designer::PrinterProfile profile;
    profile.name = QStringLiteral("PNG receiver");
    profile.destination.connection = Designer::PrintConnection::Tcp;
    profile.destination.host = QStringLiteral("192.0.2.16");
    profile.destination.port = 9201;
    profile.mode = Designer::LabelPrintMode::Png;
    profile.dpi = 300;
    profile.parameters.darkness = 12;
    QVERIFY(Designer::savePrinterProfiles({profile}));

    Designer::PrintDialog dialog(initial_);
    auto* profiles = dialog.findChild<QComboBox*>(QStringLiteral("printProfiles"));
    auto* connection = dialog.findChild<QComboBox*>(QStringLiteral("printDestination"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("printMode"));
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("printParametersSummary"));
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("printSaveProfile"));
    QVERIFY(profiles && connection && mode && summary && save);
    QCOMPARE(connection->currentData().toInt(), int(Designer::PrintConnection::InstalledPrinter));
    QVERIFY(profiles->findData(profile.name) > 0);
    profiles->setCurrentIndex(profiles->findData(profile.name));
    QCOMPARE(connection->currentData().toInt(), int(Designer::PrintConnection::Tcp));
    QCOMPARE(mode->count(), 3);
    QCOMPARE(mode->currentData().toInt(), int(Designer::LabelPrintMode::Png));
    QVERIFY(summary->text().contains(QStringLiteral("PNG")));
    QVERIFY(summary->text().contains(QStringLiteral("не примен")));

    const auto copyName = QStringLiteral("PNG receiver copy");
    bool nameDialogSeen = false;
    QTimer::singleShot(0, &dialog, [&] {
        auto* nameDialog = dialog.findChild<QInputDialog*>();
        if (!nameDialog) return;
        nameDialogSeen = true;
        nameDialog->setTextValue(copyName);
        nameDialog->accept();
    });
    save->click();
    QVERIFY(nameDialogSeen);
    const auto saved = Designer::loadPrinterProfiles();
    QVERIFY(saved);
    QCOMPARE(saved->size(), 2);
    QCOMPARE(saved->last().name, copyName);
    QCOMPARE(saved->last().destination.connection, Designer::PrintConnection::Tcp);
    QCOMPARE(saved->last().destination.host, profile.destination.host);
    QCOMPARE(saved->last().mode, Designer::LabelPrintMode::Png);
    QCOMPARE(saved->last().dpi, profile.dpi);
    QCOMPARE(saved->last().parameters, profile.parameters);
    dialog.reject();
    QVERIFY(!QFile::exists(settings.preferencesPath()));
    QCOMPARE(Designer::toZpl(window_->document()), Designer::toZpl(initial_));
}

QTEST_MAIN(WindowTests)
#include "test_window.moc"
