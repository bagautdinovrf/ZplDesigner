#include "canvas_quick_panel.hpp"
#include "designer_theme.hpp"
#include "document_session.hpp"
#include "label_canvas.hpp"
#include "main_window.hpp"
#include "preview_widget.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QtTest>

#include <memory>
#include <optional>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
const QString initialSource = u"^XA\n^PW400\n^LL300\n^FO40,48^GB80,56,2^FS\n^XZ\n"_s;

using SourceRanges = QList<QPair<int, int>>;

SourceRanges objectHighlightRanges(const QPlainTextEdit* editor)
{
    SourceRanges ranges;
    for (const auto& extra : editor->extraSelections()) {
        if (extra.cursor.hasSelection())
            ranges.append({extra.cursor.selectionStart(), extra.cursor.selectionEnd() - extra.cursor.selectionStart()});
    }
    return ranges;
}

QPair<int, int> fieldRange(const QString& source, const QString& origin)
{
    const int start = static_cast<int>(source.indexOf(origin));
    const int end = start < 0 ? -1 : static_cast<int>(source.indexOf(u"^FS", start));
    return {start, end < 0 ? 0 : end + 3 - start};
}

void clickSourcePosition(QPlainTextEdit* editor, qsizetype position)
{
    editor->horizontalScrollBar()->setValue(0);
    QTextCursor target(editor->document());
    target.setPosition(static_cast<int>(position));
    QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier,
                      editor->cursorRect(target).center());
}

QColor previewColorAt(QGraphicsView* view, QPoint point)
{
    const auto image = view->viewport()->grab().toImage();
    const auto position = view->mapFromScene(point);
    const auto ratio = image.devicePixelRatio();
    const QPoint pixel(qRound(position.x() * ratio), qRound(position.y() * ratio));
    return image.rect().contains(pixel) ? image.pixelColor(pixel) : QColor{};
}
}

class SplitEditorTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void splitViewsAndSwappingKeepTheSameSourceEditor();
    void liveSourcePreservesCursorAndNativeUndo();
    void designChangesUpdateCodeAndSessionUndo();
    void codeUpdatesTheVisiblePreview();
    void invalidSourceSurvivesModesSidesAndThemes();
    void unsupportedSourceRemainsLossless_data();
    void unsupportedSourceRemainsLossless();
    void liveApplyStartsWhenEnteringSplitMode();
    void openingProjectRestartsPendingLiveApply();
    void preferencesRestoreModeAndSides();
    void focusSplitKeepsPropertiesOutsideTheEditors();
    void sourceSelectionHighlightsIntersectingObjects();
    void sourceSelectionSurvivesLiveApply();
    void sourceSelectionTracksGeneratedDesign();
    void sourceSelectionAfterRevertingRestoredDraft();
    void canvasAndLayersHighlightSeparateSourceFields();
    void groupsAndTablesHighlightAllOwnedFields();
    void selectionSyncToggleKeepsIndependentSelections();
    void selectionSyncPreferenceSurvivesReopening();
    void geometryLabelPreferencesSurviveReopening();
    void sourceHighlightsPreserveCursorThemeAndNativeUndo();
    void sourceHighlightsClearPendingAndReturnAfterApply();
    void sourceCaretClicksSelectExactFieldsAndRowFallbacks();
    void sourceCaretKeyboardKeepsMultiRangeSelection();
    void sourceCaretReclickSelectsAfterCanvasChanges();
    void sourceCaretDoesNotSyncWhenDisabledOrUnfocused();
    void sourceCaretWaitsForPendingSourceToApply();
    void sourceCaretDoesNotRetargetAfterPropertyShortensText();
    void corruptedPreferencesUseSafeDefaults_data();
    void corruptedPreferencesUseSafeDefaults();
    void optionalSplitScreenshots();

private:
    void createWindow();
    QAction* action(const char* name) const;
    std::unique_ptr<MainWindow> window_;
    DesignDocument initial_;
    QComboBox* layout_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QPlainTextEdit* code_ = nullptr;
    QWidget* sourcePage_ = nullptr;
    LabelCanvas* canvas_ = nullptr;
    PreviewWidget* preview_ = nullptr;
    DocumentSession* session_ = nullptr;
    QString previousName_;
    QString settingsDirectory_;
    bool previousTestMode_ = false;
};

QAction* SplitEditorTests::action(const char* name) const
{
    return window_->findChild<QAction*>(QString::fromLatin1(name));
}

void SplitEditorTests::createWindow()
{
    window_ = std::make_unique<MainWindow>();
    window_->setDocument(initial_);
    window_->resize(1440, 900);
    window_->show();
    QApplication::processEvents();
    layout_ = window_->findChild<QComboBox*>(u"editorLayout"_s);
    splitter_ = window_->findChild<QSplitter*>(u"editorSplitter"_s);
    tabs_ = window_->findChild<QTabWidget*>(u"editorTabs"_s);
    code_ = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    sourcePage_ = window_->findChild<QWidget*>(u"sourcePage"_s);
    canvas_ = window_->findChild<LabelCanvas*>();
    preview_ = window_->findChild<PreviewWidget*>();
    session_ = window_->findChild<DocumentSession*>();
}

void SplitEditorTests::init()
{
    previousName_ = QCoreApplication::applicationName();
    previousTestMode_ = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"split-editor-tests-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    settingsDirectory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    installDesignerStyle();
    setDesignerTheme(DesignerTheme::Light);
    const auto parsed = fromZpl(initialSource);
    QVERIFY(parsed);
    initial_ = *parsed;
    createWindow();
    QVERIFY(layout_); QVERIFY(splitter_); QVERIFY(tabs_); QVERIFY(code_);
    QVERIFY(sourcePage_); QVERIFY(canvas_); QVERIFY(preview_); QVERIFY(session_);
    QVERIFY(action("swapEditorSides"));
    QVERIFY(action("darkTheme"));
    QCOMPARE(layout_->count(), 3);
    auto settings = session_->layoutSettings();
    settings.unit = CanvasUnit::Dots;
    session_->setLayoutSettings(settings);
}

void SplitEditorTests::cleanup()
{
    if (window_) {
        window_->setDocument(initial_);
        session_->discardRecovery();
        window_.reset();
    }
    QFile::remove(QDir(settingsDirectory_).filePath(u"workspace.ini"_s));
    QDir().rmdir(settingsDirectory_);
    QCoreApplication::setApplicationName(previousName_);
    QStandardPaths::setTestModeEnabled(previousTestMode_);
    setDesignerTheme(DesignerTheme::Light);
}

void SplitEditorTests::splitViewsAndSwappingKeepTheSameSourceEditor()
{
    QCOMPARE(layout_->currentIndex(), 0);
    QVERIFY(sourcePage_ == tabs_->widget(1) || tabs_->widget(1)->isAncestorOf(sourcePage_));
    layout_->setCurrentIndex(1);
    QApplication::processEvents();
    QVERIFY(code_->isVisible());
    QVERIFY(canvas_->isVisible());
    QVERIFY(!preview_->isVisible());
    QCOMPARE(splitter_->orientation(), Qt::Horizontal);
    QCOMPARE(splitter_->count(), 2);
    QCOMPARE(splitter_->widget(0), sourcePage_);
    QCOMPARE(splitter_->widget(1), tabs_);
    QVERIFY(action("swapEditorSides")->isEnabled());
    const auto source = code_->toPlainText();
    QTextCursor cursor = code_->textCursor();
    cursor.setPosition(4);
    cursor.setPosition(12, QTextCursor::KeepAnchor);
    code_->setTextCursor(cursor);
    QSignalSpy sourceChanges(code_, &QPlainTextEdit::textChanged);
    QSignalSpy documentChanges(session_, &DocumentSession::documentChanged);

    action("swapEditorSides")->trigger();
    QCOMPARE(splitter_->widget(0), tabs_);
    QCOMPARE(splitter_->widget(1), sourcePage_);
    layout_->setCurrentIndex(2);
    QApplication::processEvents();
    QVERIFY(code_->isVisible());
    QVERIFY(preview_->isVisible());
    QVERIFY(!canvas_->isVisible());
    QCOMPARE(splitter_->widget(1), sourcePage_);
    layout_->setCurrentIndex(0);
    tabs_->setCurrentIndex(1);
    QApplication::processEvents();
    QVERIFY(code_->isVisible());
    QVERIFY(sourcePage_ == tabs_->widget(1) || tabs_->widget(1)->isAncestorOf(sourcePage_));
    QCOMPARE(window_->findChild<QPlainTextEdit*>(u"zplEditor"_s), code_);
    QCOMPARE(code_->toPlainText(), source);
    QCOMPARE(code_->textCursor().position(), cursor.position());
    QCOMPARE(code_->textCursor().anchor(), cursor.anchor());
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(documentChanges.count(), 0);
}

void SplitEditorTests::liveSourcePreservesCursorAndNativeUndo()
{
    layout_->setCurrentIndex(1);
    QTextCursor cursor = code_->textCursor();
    const int offset = static_cast<int>(initialSource.indexOf(u"40,48"_s));
    QVERIFY(offset >= 0);
    cursor.setPosition(offset);
    cursor.setPosition(offset + 2, QTextCursor::KeepAnchor);
    cursor.insertText(u"68"_s);
    code_->setTextCursor(cursor);
    const auto entered = code_->toPlainText();
    const auto position = code_->textCursor().position();
    QSignalSpy sourceChanges(code_, &QPlainTextEdit::textChanged);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 68);
    QCOMPARE(toZpl(window_->document()), entered);
    QCOMPARE(code_->toPlainText(), entered);
    QCOMPARE(code_->textCursor().position(), position);
    QCOMPARE(sourceChanges.count(), 0);
    QVERIFY(code_->document()->isUndoAvailable());

    code_->setFocus();
    QTRY_VERIFY(code_->hasFocus());
    QVERIFY(action("undoAction"));
    QVERIFY(action("undoAction")->isEnabled());
    action("undoAction")->trigger();
    QCOMPARE(code_->toPlainText(), initialSource);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 40);
    QVERIFY(code_->document()->isRedoAvailable());
    code_->redo();
    QCOMPARE(code_->toPlainText(), entered);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 68);
}

void SplitEditorTests::designChangesUpdateCodeAndSessionUndo()
{
    layout_->setCurrentIndex(1);
    action("swapEditorSides")->trigger();
    canvas_->setSelectedIndex(0);
    auto* x = window_->findChild<QSpinBox*>(u"elementX"_s);
    QVERIFY(x);
    const int historyIndex = session_->undoStack()->index();
    x->setValue(72);
    QCOMPARE(window_->document().elements.first().x, 72);
    QCOMPARE(code_->toPlainText(), toZpl(window_->document()));
    QVERIFY(code_->toPlainText().contains(u"^FO72,48"_s));
    QVERIFY(!session_->hasPendingSource());
    QCOMPARE(session_->undoStack()->index(), historyIndex + 1);
    session_->undoStack()->undo();
    QCOMPARE(window_->document().elements.first().x, 40);
    QCOMPARE(code_->toPlainText(), initialSource);
    session_->undoStack()->redo();
    QCOMPARE(window_->document().elements.first().x, 72);
    QCOMPARE(code_->toPlainText(), toZpl(window_->document()));
    QTest::qWait(450);
    QCOMPARE(session_->undoStack()->index(), historyIndex + 1);
    QVERIFY(!session_->hasPendingSource());
}

void SplitEditorTests::codeUpdatesTheVisiblePreview()
{
    layout_->setCurrentIndex(2);
    auto* view = preview_->findChild<QGraphicsView*>(u"previewView"_s);
    auto* actualSize = preview_->findChild<QPushButton*>(u"previewActualSize"_s);
    auto* exportPng = preview_->findChild<QPushButton*>(u"previewExportPng"_s);
    QVERIFY(view); QVERIFY(actualSize); QVERIFY(exportPng);
    const auto source = u"^XA\n^PW360\n^LL220\n^FO16,24^GB88,40,2^FS\n^XZ\n"_s;
    code_->setPlainText(source);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QTRY_COMPARE_WITH_TIMEOUT(view->sceneRect().size(), QSizeF(360, 220), 2500);
    QCOMPARE(toZpl(window_->document()), source);
    QVERIFY(preview_->isVisible());
    QVERIFY(code_->isVisible());
    QVERIFY(exportPng->isEnabled());
    actualSize->click();
    QTRY_COMPARE_WITH_TIMEOUT(previewColorAt(view, {16, 24}), QColor(Qt::black), 1500);
    QCOMPARE(previewColorAt(view, {60, 60}), QColor(Qt::white));

    auto changed = source;
    changed.replace(u"^FO16,24"_s, u"^FO72,28"_s);
    code_->setPlainText(changed);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QTRY_COMPARE_WITH_TIMEOUT(previewColorAt(view, {72, 28}), QColor(Qt::black), 1500);
    QCOMPARE(previewColorAt(view, {16, 24}), QColor(Qt::white));
    QCOMPARE(view->transform().m11(), 1.0);
    QCOMPARE(code_->toPlainText(), changed);
}

void SplitEditorTests::invalidSourceSurvivesModesSidesAndThemes()
{
    layout_->setCurrentIndex(2);
    const auto committed = toZpl(window_->document());
    const int historyIndex = session_->undoStack()->index();
    QTextCursor cursor = code_->textCursor();
    cursor.select(QTextCursor::Document);
    cursor.insertText(u"invalid ZPL"_s);
    cursor.setPosition(2);
    cursor.setPosition(8, QTextCursor::KeepAnchor);
    code_->setTextCursor(cursor);
    const auto invalid = code_->toPlainText();
    const auto* notice = window_->findChild<QLabel*>(u"sourceStatus"_s);
    auto* exportPng = preview_->findChild<QPushButton*>(u"previewExportPng"_s);
    QVERIFY(notice); QVERIFY(exportPng);
    QTRY_VERIFY_WITH_TIMEOUT(notice->text().contains(u"не применён"_s), 2500);
    QVERIFY(session_->hasPendingSource());
    QCOMPARE(session_->pendingSource(), invalid);
    QCOMPARE(toZpl(window_->document()), committed);
    QVERIFY(!exportPng->isEnabled());
    QSignalSpy sourceChanges(code_, &QPlainTextEdit::textChanged);
    QSignalSpy documentChanges(session_, &DocumentSession::documentChanged);
    auto* style = qApp->style();
    const auto font = code_->font();

    action("swapEditorSides")->trigger();
    action("darkTheme")->trigger();
    layout_->setCurrentIndex(1);
    layout_->setCurrentIndex(0);
    layout_->setCurrentIndex(2);
    action("darkTheme")->trigger();
    QTest::qWait(450);
    QCOMPARE(window_->findChild<QPlainTextEdit*>(u"zplEditor"_s), code_);
    QCOMPARE(code_->toPlainText(), invalid);
    QCOMPARE(code_->textCursor().position(), cursor.position());
    QCOMPARE(code_->textCursor().anchor(), cursor.anchor());
    QCOMPARE(code_->font(), font);
    QCOMPARE(qApp->style(), style);
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(documentChanges.count(), 0);
    QCOMPARE(toZpl(window_->document()), committed);
    QCOMPARE(session_->undoStack()->index(), historyIndex);
    QVERIFY(session_->hasPendingSource());
    QVERIFY(code_->document()->isUndoAvailable());
    QVERIFY(code_->isVisible());
    QVERIFY(preview_->isVisible());
    QVERIFY(!exportPng->isEnabled());

    code_->undo();
    QCOMPARE(code_->toPlainText(), committed);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QTRY_VERIFY_WITH_TIMEOUT(exportPng->isEnabled(), 2500);
    QCOMPARE(toZpl(window_->document()), committed);
}

void SplitEditorTests::unsupportedSourceRemainsLossless_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<int>("pages");
    QTest::newRow("unknown-command")
        << u"^XA\n^PW300\n^LL160\n^ZZretained\n^FO12,20^GB80,32,2^FS\n^XZ\n"_s << 1;
    QTest::newRow("multiple-labels")
        << u"^XA^PW300^LL160^FO12,20^GB80,32,2^FS^XZ\n"
           u"^XA^PW300^LL160^FO24,32^GB64,40,3^FS^XZ\n"_s << 2;
}

void SplitEditorTests::unsupportedSourceRemainsLossless()
{
    QFETCH(QString, source);
    QFETCH(int, pages);
    layout_->setCurrentIndex(2);
    code_->setPlainText(source);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QVERIFY(!window_->document().visualEditable);
    QCOMPARE(toZpl(window_->document()), source);
    QCOMPARE(code_->toPlainText(), source);
    QTRY_COMPARE_WITH_TIMEOUT(preview_->pageCount(), pages, 2500);
    QVERIFY(!code_->isReadOnly());
    const int historyIndex = session_->undoStack()->index();
    layout_->setCurrentIndex(1);
    window_->addElement(ElementType::Box, {80, 80});
    QCOMPARE(toZpl(window_->document()), source);
    QCOMPARE(session_->undoStack()->index(), historyIndex);
    action("swapEditorSides")->trigger();
    layout_->setCurrentIndex(2);
    QCOMPARE(code_->toPlainText(), source);
    QCOMPARE(preview_->pageCount(), pages);
    QVERIFY(code_->isVisible());
    QVERIFY(preview_->isVisible());
}

void SplitEditorTests::liveApplyStartsWhenEnteringSplitMode()
{
    tabs_->setCurrentIndex(1);
    auto changed = initialSource;
    changed.replace(u"^PW400"_s, u"^PW360"_s);
    code_->setPlainText(changed);
    QTest::qWait(450);
    QVERIFY(session_->hasPendingSource());
    QCOMPARE(window_->document().width, 400);
    layout_->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(window_->document().width, 360);
    QCOMPARE(code_->toPlainText(), changed);
    QVERIFY(canvas_->isVisible());
    QVERIFY(code_->isVisible());
}

void SplitEditorTests::preferencesRestoreModeAndSides()
{
    layout_->setCurrentIndex(2);
    action("swapEditorSides")->trigger();
    QCOMPARE(splitter_->widget(1), sourcePage_);
    window_.reset();
    createWindow();
    QCOMPARE(layout_->currentIndex(), 2);
    QCOMPARE(splitter_->widget(0), tabs_);
    QCOMPARE(splitter_->widget(1), sourcePage_);
    QVERIFY(code_->isVisible());
    QVERIFY(preview_->isVisible());
    layout_->setCurrentIndex(1);
    QCOMPARE(splitter_->widget(1), sourcePage_);
    QVERIFY(canvas_->isVisible());
}

void SplitEditorTests::openingProjectRestartsPendingLiveApply()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    layout_->setCurrentIndex(2);
    auto changed = initialSource;
    changed.replace(u"^PW400"_s, u"^PW360"_s);
    code_->setPlainText(changed);
    QVERIFY(session_->hasPendingSource());
    const auto path = directory.filePath(u"pending-source.qtzpl"_s);
    QVERIFY(window_->saveFile(path));
    // Projects persist the draft independently of the last committed label.
    // Saving is synchronous, so no debounce has applied this valid draft yet.
    QVERIFY(session_->hasPendingSource());
    QCOMPARE(window_->document().width, 400);
    window_->setDocument(initial_);
    QVERIFY(!session_->hasPendingSource());
    QVERIFY(window_->openFile(path));
    QCOMPARE(code_->toPlainText(), changed);
    QVERIFY(code_->isVisible());
    QVERIFY(preview_->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(window_->document().width, 360);
    QCOMPARE(code_->toPlainText(), changed);
    const auto* view = preview_->findChild<QGraphicsView*>(u"previewView"_s);
    QVERIFY(view);
    QTRY_COMPARE_WITH_TIMEOUT(view->sceneRect().size(), QSizeF(360, 300), 2500);
}

void SplitEditorTests::focusSplitKeepsPropertiesOutsideTheEditors()
{
    layout_->setCurrentIndex(1);
    QVERIFY(action("workspaceFocus"));
    QVERIFY(action("workspaceProperties"));
    action("workspaceFocus")->trigger();
    if (!action("workspaceProperties")->isChecked()) action("workspaceProperties")->trigger();
    auto* inspector = window_->findChild<QTabWidget*>(u"inspectorTabs"_s);
    auto* slot = window_->findChild<QWidget*>(u"inspectorSlot"_s);
    auto* floatingCard = window_->findChild<QWidget*>(u"focusInspectorCard"_s);
    QVERIFY(inspector); QVERIFY(slot); QVERIFY(floatingCard);
    QApplication::processEvents();
    QVERIFY(action("workspaceFocus")->isChecked());
    QVERIFY(inspector->isVisible());
    QVERIFY(slot->isVisible());
    QVERIFY(slot->isAncestorOf(inspector));
    QVERIFY(!splitter_->isAncestorOf(inspector));
    QVERIFY(!floatingCard->isVisible());
    const auto editorsBounds = [this] {
        return QRect(splitter_->mapTo(window_.get(), QPoint{}), splitter_->size());
    };
    const auto inspectorBounds = [this, inspector] {
        return QRect(inspector->mapTo(window_.get(), QPoint{}), inspector->size());
    };
    QVERIFY(!editorsBounds().intersects(inspectorBounds()));
    QVERIFY(code_->isVisible());
    QVERIFY(code_->isEnabled());
    QVERIFY(!code_->isReadOnly());
    QVERIFY(canvas_->isVisible());
    QVERIFY(canvas_->isEnabled());

    window_->resize(1050, 680);
    QApplication::processEvents();
    QVERIFY(!editorsBounds().intersects(inspectorBounds()));
    QVERIFY(code_->isVisible());
    QVERIFY(canvas_->isVisible());
    QVERIFY(layout_->isVisible());
    // Reserve space for the native arrow and frame as well as the full label.
    const int textWidth = layout_->fontMetrics().horizontalAdvance(layout_->currentText());
    QVERIFY(layout_->width() >= textWidth + 32);
    QVERIFY(layout_->parentWidget()->rect().contains(layout_->geometry()));
    canvas_->setSelectedIndex(0);
    auto* x = window_->findChild<QSpinBox*>(u"elementX"_s);
    QVERIFY(x);
    QVERIFY(x->isEnabled());
    x->setValue(72);
    QCOMPARE(window_->document().elements.first().x, 72);
    QCOMPARE(code_->toPlainText(), toZpl(window_->document()));
    auto changed = code_->toPlainText();
    changed.replace(u"^FO72,48"_s, u"^FO88,48"_s);
    code_->setPlainText(changed);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 88);
    QVERIFY(canvas_->isEnabled());
}

void SplitEditorTests::corruptedPreferencesUseSafeDefaults_data()
{
    QTest::addColumn<QString>("layoutValue");
    QTest::addColumn<QStringList>("sizes");
    QTest::newRow("unknown-mode-and-negative-size") << u"99"_s << QStringList{u"-1"_s, u"500"_s};
    QTest::newRow("negative-mode-and-too-small-size") << u"-1"_s << QStringList{u"49"_s, u"500"_s};
    QTest::newRow("non-numeric-values") << u"broken"_s << QStringList{u"left"_s, u"right"_s};
    QTest::newRow("oversized-pane") << u"3"_s << QStringList{u"10001"_s, u"500"_s};
    QTest::newRow("missing-pane") << u"3"_s << QStringList{u"500"_s};
}

void SplitEditorTests::corruptedPreferencesUseSafeDefaults()
{
    QFETCH(QString, layoutValue);
    QFETCH(QStringList, sizes);
    window_.reset();
    {
        QSettings settings(workspacePreferencesPath(), QSettings::IniFormat);
        settings.setValue(u"editor/layout"_s, layoutValue);
        settings.setValue(u"editor/codeOnLeft"_s, u"broken"_s);
        settings.setValue(u"editor/sizes"_s, sizes);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    const auto restored = loadWorkspacePreferences();
    QCOMPARE(restored.editorLayout, EditorLayout::Single);
    QVERIFY(restored.codeOnLeft);
    QCOMPARE(restored.editorSizes, (QList<int>{500, 500}));
    createWindow();
    QCOMPARE(layout_->currentIndex(), 0);
    layout_->setCurrentIndex(1);
    QCOMPARE(splitter_->widget(0), sourcePage_);
    QVERIFY(code_->isVisible());
    QVERIFY(canvas_->isVisible());
}

void SplitEditorTests::optionalSplitScreenshots()
{
    const auto destination = qEnvironmentVariable("QTZPL_SPLIT_SCREENSHOT_DIR");
    if (destination.isEmpty()) return;
    QVERIFY(QDir().mkpath(destination));
    const auto reference = fromZpl(u"^XA\n^PW600\n^LL320\n^CI28\n"
        u"^FO20,20^GB560,280,2^FS\n"
        u"^FO44,44^A0N,32,0^FDPRECISION PARTS^FS\n"
        u"^FO44,90^A0N,22,0^FDSKU: QT-2048 / LOT: 07^FS\n"
        u"^FO44,142^BXN,5,200^FDQT-2048-07^FS\n"
        u"^FO224,148^A0N,28,0^FDQTY 100^FS\n"
        u"^FO224,192^A0N,22,0^FDINSPECTED^FS\n^XZ"_s);
    QVERIFY(reference);
    window_->setDocument(*reference);
    const auto source = toZpl(window_->document());
    const QDir directory(destination);
    QJsonObject metrics;
    const auto capture = [&](const QString& name) {
        QTest::qWait(450);
        if (canvas_->isVisible()) canvas_->fitLabel();
        QApplication::processEvents();
        if (name.startsWith(u"focus-code-left-design-properties")) {
            const SourceRanges expected{fieldRange(source, u"^FO44,44"_s)};
            if (objectHighlightRanges(code_) != expected) {
                qWarning() << "Selected object's source highlight disappeared after event processing";
                return false;
            }
        }
        QJsonObject widgets;
        for (const auto* objectName : {"editorLayout", "editorSplitter", "sourcePage", "zplEditor",
                                      "labelCanvas", "previewWidget", "inspectorTabs"}) {
            const auto* widget = window_->findChild<QWidget*>(QString::fromLatin1(objectName));
            if (!widget) continue;
            const QRect bounds(widget->mapTo(window_.get(), QPoint{}), widget->size());
            widgets.insert(QString::fromLatin1(objectName), QJsonObject{
                {u"x"_s, bounds.x()}, {u"y"_s, bounds.y()},
                {u"width"_s, bounds.width()}, {u"height"_s, bounds.height()},
                {u"visible"_s, widget->isVisible()}});
        }
        metrics.insert(name, widgets);
        return window_->grab().save(directory.filePath(name + u".png"_s));
    };
    for (int theme = 0; theme < 2; ++theme) {
        if (theme) action("darkTheme")->trigger();
        const auto suffix = theme ? u"-dark"_s : u"-light"_s;
        layout_->setCurrentIndex(0);
        tabs_->setCurrentIndex(0);
        QVERIFY(capture(u"single-design"_s + suffix));
        tabs_->setCurrentIndex(1);
        QVERIFY(capture(u"single-source"_s + suffix));
        for (int mode = 1; mode <= 2; ++mode) {
            layout_->setCurrentIndex(mode);
            if (splitter_->widget(0) != sourcePage_) action("swapEditorSides")->trigger();
            const auto view = mode == 1 ? u"design"_s : u"preview"_s;
            QVERIFY(capture(u"code-left-"_s + view + suffix));
            action("swapEditorSides")->trigger();
            QVERIFY(capture(u"code-right-"_s + view + suffix));
        }
        layout_->setCurrentIndex(1);
        if (splitter_->widget(0) != sourcePage_) action("swapEditorSides")->trigger();
        action("workspaceFocus")->trigger();
        if (!action("workspaceProperties")->isChecked()) action("workspaceProperties")->trigger();
        canvas_->setSelectedIndex(1);
        QVERIFY(capture(u"focus-code-left-design-properties"_s + suffix));
        canvas_->setSelectedIndex(-1);
        action("workspaceBalance")->trigger();
    }
    QFile output(directory.filePath(u"split-editor-geometry.json"_s));
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(output.write(QJsonDocument(metrics).toJson()) > 0);
    QCOMPARE(code_->toPlainText(), source);
    QCOMPARE(toZpl(window_->document()), source);
}

void SplitEditorTests::sourceSelectionHighlightsIntersectingObjects()
{
    const auto source = u"^XA\n^PW400\n^LL300\n^FXComment^FS\n"
        "^FO20,20^GB30,20,2^FS^FO80,20^GB30,20,2^FS\n"
        "^FO140,20\n^GB30,20,2\n^FS\n^XZ\n"_s;
    const auto parsed = fromZpl(source);
    QVERIFY(parsed && parsed->visualEditable);
    QCOMPARE(parsed->elements.size(), 3);
    window_->setDocument(*parsed);
    layout_->setCurrentIndex(1);
    QSignalSpy sourceChanges(code_, &QPlainTextEdit::textChanged);
    QSignalSpy documentChanges(session_, &DocumentSession::documentChanged);
    const auto select = [this](qsizetype start, qsizetype end) {
        QTextCursor cursor(code_->document());
        cursor.setPosition(static_cast<int>(start));
        cursor.setPosition(static_cast<int>(end), QTextCursor::KeepAnchor);
        code_->setTextCursor(cursor);
    };
    const auto first = source.indexOf(u"^FO20"_s);
    const auto second = source.indexOf(u"^FO80"_s);
    const auto third = source.indexOf(u"^FO140"_s);
    select(first + 3, first + 5);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    select(first, third); // Endpoint at the next line does not include it.
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{0, 1}));
    select(second, source.indexOf(u"^XZ"_s));
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{1, 2}));
    select(third + 10, third + 14); // A command on a later line of the field.
    QCOMPARE(canvas_->selectedIndices(), QList<int>{2});
    layout_->setCurrentIndex(0);
    tabs_->setCurrentIndex(0);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{2});
    tabs_->setCurrentIndex(1);
    select(source.indexOf(u"^FX"_s), first);
    QVERIFY(canvas_->selectedIndices().isEmpty());
    select(first, second);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    // A programmatic, unfocused collapse retains the previous selection-only
    // contract; actual clicks and keyboard caret movement are covered below.
    // The canvas is hidden on the single source tab, so focus a visible control.
    layout_->setFocus();
    QTRY_VERIFY(layout_->hasFocus());
    select(first, first);
    QVERIFY(canvas_->selectedIndices().isEmpty());
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(documentChanges.count(), 0);
    QCOMPARE(code_->toPlainText(), source);
    QCOMPARE(toZpl(window_->document()), source);
}

void SplitEditorTests::sourceSelectionSurvivesLiveApply()
{
    layout_->setCurrentIndex(1);
    auto cursor = code_->textCursor();
    const int start = static_cast<int>(initialSource.indexOf(u"40,48"_s));
    cursor.setPosition(start);
    cursor.setPosition(start + 2, QTextCursor::KeepAnchor);
    cursor.insertText(u"68"_s);
    code_->setTextCursor(cursor);
    QVERIFY(session_->hasPendingSource());
    cursor.setPosition(start);
    cursor.setPosition(start + 2, QTextCursor::KeepAnchor);
    code_->setTextCursor(cursor);
    QVERIFY(canvas_->selectedIndices().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QCOMPARE(window_->document().elements[0].x, 68);
    QCOMPARE(code_->textCursor().selectionStart(), start);
    QCOMPARE(code_->textCursor().selectionEnd(), start + 2);
}

void SplitEditorTests::sourceSelectionTracksGeneratedDesign()
{
    DesignDocument document;
    document.elements = {makeElement(ElementType::Box, {20, 20}),
                         makeElement(ElementType::Box, {180, 20})};
    window_->setDocument(document);
    layout_->setCurrentIndex(1);
    const auto selectSecond = [this] {
        const int start = static_cast<int>(code_->toPlainText().indexOf(u"^FO180,"_s));
        QVERIFY(start >= 0);
        QTextCursor cursor(code_->document());
        cursor.setPosition(start);
        cursor.setPosition(start + 6, QTextCursor::KeepAnchor);
        code_->setTextCursor(cursor);
    };
    selectSecond();
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    canvas_->setSelectedIndex(0);
    auto* x = window_->findChild<QSpinBox*>(u"elementX"_s);
    QVERIFY(x);
    x->setValue(100);
    QCOMPARE(window_->document().elements[0].x, 100);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    auto cursor = code_->textCursor();
    cursor.clearSelection();
    code_->setTextCursor(cursor);
    selectSecond();
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    QVERIFY(!session_->hasPendingSource());
}

void SplitEditorTests::sourceSelectionAfterRevertingRestoredDraft()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto crlf = initialSource;
    crlf.replace(u"\n"_s, u"\r\n"_s);
    const auto parsed = fromZpl(crlf);
    QVERIFY(parsed && parsed->visualEditable);
    window_->setDocument(*parsed);
    tabs_->setCurrentIndex(1);
    auto draft = initialSource;
    draft.replace(u"40,48"_s, u"68,48"_s);
    code_->setPlainText(draft);
    QVERIFY(session_->hasPendingSource());
    const auto path = directory.filePath(u"selection-draft.qtzpl"_s);
    QVERIFY(window_->saveFile(path));
    window_->setDocument(initial_);
    QVERIFY(window_->openFile(path));
    QVERIFY(session_->hasPendingSource());
    code_->setPlainText(initialSource);
    QVERIFY(!session_->hasPendingSource());
    QTextCursor cursor(code_->document());
    const int start = static_cast<int>(initialSource.indexOf(u"^FO"_s));
    cursor.setPosition(start);
    cursor.setPosition(start + 6, QTextCursor::KeepAnchor);
    code_->setTextCursor(cursor);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QCOMPARE(toZpl(window_->document()), crlf);
}

void SplitEditorTests::canvasAndLayersHighlightSeparateSourceFields()
{
    DesignDocument document;
    document.elements = {makeElement(ElementType::Box, {20, 20}),
                         makeElement(ElementType::Box, {120, 20}),
                         makeElement(ElementType::Box, {220, 20})};
    window_->setDocument(document);
    layout_->setCurrentIndex(1);
    // Let the queued initial syntax highlighting and preview finish first.
    QTest::qWait(150);
    const auto source = code_->toPlainText();
    QTextCursor cursor(code_->document());
    cursor.setPosition(static_cast<int>(source.indexOf(u"^PW")));
    cursor.setPosition(cursor.position() + 3, QTextCursor::KeepAnchor);
    code_->setTextCursor(cursor);
    canvas_->setFocus();
    QTRY_VERIFY(canvas_->hasFocus());
    QSignalSpy sourceChanges(code_, &QPlainTextEdit::textChanged);
    QSignalSpy cursorSelections(code_, &QPlainTextEdit::selectionChanged);
    QSignalSpy documentChanges(session_, &DocumentSession::documentChanged);
    QSignalSpy canvasSelections(canvas_, &LabelCanvas::selectionSetChanged);
    const int undoIndex = session_->undoStack()->index();

    canvas_->setSelectedIndices({0, 2});
    QCOMPARE(objectHighlightRanges(code_), (SourceRanges{
        fieldRange(source, u"^FO20,"_s), fieldRange(source, u"^FO220,"_s)}));
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{0, 2}));
    QCOMPARE(canvasSelections.count(), 1);
    QCOMPARE(code_->textCursor().position(), cursor.position());
    QCOMPARE(code_->textCursor().anchor(), cursor.anchor());
    QVERIFY(canvas_->hasFocus());

    auto* layers = window_->findChild<QListWidget*>(u"layerList"_s);
    QVERIFY(layers);
    layers->setCurrentRow(1, QItemSelectionModel::ClearAndSelect);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    QCOMPARE(objectHighlightRanges(code_), SourceRanges{fieldRange(source, u"^FO120,"_s)});
    QCOMPARE(code_->textCursor().position(), cursor.position());
    QCOMPARE(code_->textCursor().anchor(), cursor.anchor());
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(cursorSelections.count(), 0);
    QCOMPARE(documentChanges.count(), 0);
    QCOMPARE(session_->undoStack()->index(), undoIndex);
    QCOMPARE(code_->toPlainText(), source);
    canvas_->setSelectedIndices({});
    QVERIFY(objectHighlightRanges(code_).isEmpty());
}

void SplitEditorTests::groupsAndTablesHighlightAllOwnedFields()
{
    DesignDocument document;
    auto groupedBox = makeElement(ElementType::Box, {20, 20});
    auto unrelated = makeElement(ElementType::Box, {320, 20});
    auto table = makeElement(ElementType::Table, {20, 160});
    table.tableCells = {u"Альфа"_s, {}, u"Бета"_s, {}};
    groupedBox.groupId = table.groupId = QUuid::createUuid();
    document.elements = {groupedBox, unrelated, table};
    window_->setDocument(document);
    layout_->setCurrentIndex(1);
    const auto source = code_->toPlainText();
    canvas_->setSelectedIndex(0);
    QVERIFY(canvas_->selectedIndices().contains(0));
    QVERIFY(canvas_->selectedIndices().contains(2));
    QVERIFY(!canvas_->selectedIndices().contains(1));
    const auto ranges = objectHighlightRanges(code_);
    const auto fields = tableElements(table);
    QCOMPARE(ranges.size(), fields.size() + 1);
    QCOMPARE(ranges.first(), fieldRange(source, u"^FO20,20"_s));
    for (qsizetype index = 0; index < fields.size(); ++index) {
        const auto& range = ranges[index + 1];
        QCOMPARE(source.sliced(range.first, range.second), elementZpl(fields[index]).trimmed());
        QVERIFY(!source.sliced(range.first, range.second).contains(u"^FX"));
        QVERIFY(range.first >= ranges[index].first + ranges[index].second);
    }
    QVERIFY(!ranges.contains(fieldRange(source, u"^FO320,"_s)));
}

void SplitEditorTests::selectionSyncToggleKeepsIndependentSelections()
{
    DesignDocument document;
    document.elements = {makeElement(ElementType::Box, {20, 20}),
                         makeElement(ElementType::Box, {180, 20})};
    window_->setDocument(document);
    layout_->setCurrentIndex(1);
    auto* sync = action("syncEditorSelection");
    QVERIFY(sync);
    QVERIFY(sync->isCheckable());
    QVERIFY(sync->isChecked());
    bool hasIconButton = false;
    for (const auto* button : window_->findChildren<QToolButton*>())
        if (button->defaultAction() == sync && !button->icon().isNull()) hasIconButton = true;
    QVERIFY(hasIconButton);
    const auto source = code_->toPlainText();
    canvas_->setSelectedIndex(0);
    QVERIFY(!objectHighlightRanges(code_).isEmpty());
    sync->setChecked(false);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QVERIFY(objectHighlightRanges(code_).isEmpty());

    QTextCursor cursor(code_->document());
    const auto second = fieldRange(source, u"^FO180,"_s);
    cursor.setPosition(second.first);
    cursor.setPosition(second.first + second.second, QTextCursor::KeepAnchor);
    code_->setTextCursor(cursor);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    canvas_->setSelectedIndex(1);
    QVERIFY(objectHighlightRanges(code_).isEmpty());
    QCOMPARE(code_->textCursor().position(), cursor.position());
    QCOMPARE(code_->textCursor().anchor(), cursor.anchor());
    canvas_->setSelectedIndex(0);
    canvas_->setFocus();
    QTRY_VERIFY(canvas_->hasFocus());
    sync->setChecked(true);
    // An old source selection must not displace the currently focused design.
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QCOMPARE(objectHighlightRanges(code_), SourceRanges{fieldRange(source, u"^FO20,"_s)});
    QCOMPARE(code_->textCursor().position(), cursor.position());
    QCOMPARE(code_->textCursor().anchor(), cursor.anchor());

    sync->setChecked(false);
    code_->setFocus();
    QTRY_VERIFY(code_->hasFocus());
    sync->setChecked(true);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    QCOMPARE(objectHighlightRanges(code_), SourceRanges{second});
    QCOMPARE(code_->textCursor().position(), cursor.position());
    QCOMPARE(code_->textCursor().anchor(), cursor.anchor());
    QCOMPARE(code_->toPlainText(), source);
    QVERIFY(!session_->modified());
}

void SplitEditorTests::selectionSyncPreferenceSurvivesReopening()
{
    auto* sync = action("syncEditorSelection");
    QVERIFY(sync && sync->isChecked());
    sync->setChecked(false);
    {
        QSettings saved(QDir(settingsDirectory_).filePath(u"workspace.ini"_s), QSettings::IniFormat);
        QCOMPARE(saved.value(u"editor/selectionSync"_s).toBool(), false);
    }
    window_.reset();
    createWindow();
    sync = action("syncEditorSelection");
    QVERIFY(sync && !sync->isChecked());
    layout_->setCurrentIndex(1);
    canvas_->setSelectedIndex(0);
    QVERIFY(objectHighlightRanges(code_).isEmpty());
    sync->setChecked(true);
    window_.reset();
    createWindow();
    sync = action("syncEditorSelection");
    QVERIFY(sync && sync->isChecked());
    canvas_->setSelectedIndex(0);
    QVERIFY(!objectHighlightRanges(code_).isEmpty());
}

void SplitEditorTests::geometryLabelPreferencesSurviveReopening()
{
    using Labels = QPair<bool, bool>; // {sizes, coordinates}
    // Drives the canvas quick panel; returns the checkbox states it opened with.
    const auto editLabels = [this](bool sizes, bool coordinates, bool docked) -> std::optional<Labels> {
        window_->findChild<QPushButton*>(u"canvasSetup"_s)->click();
        auto* panel = window_->findChild<CanvasQuickPanel*>();
        if (!panel || !panel->isVisible()) return std::nullopt;
        auto* sizesCheck = panel->findChild<QCheckBox*>(u"quickPanelGeometrySizes"_s);
        auto* coordinatesCheck = panel->findChild<QCheckBox*>(u"quickPanelGeometryCoordinates"_s);
        const Labels shown{sizesCheck->isChecked(), coordinatesCheck->isChecked()};
        sizesCheck->setChecked(sizes);
        coordinatesCheck->setChecked(coordinates);
        panel->findChild<QPushButton*>(docked ? u"quickPanelRulersDocked"_s : u"quickPanelRulersAtLabel"_s)->click();
        panel->close();
        return shown;
    };
    const auto saved = [this] {
        return QSettings(QDir(settingsDirectory_).filePath(u"workspace.ini"_s), QSettings::IniFormat);
    };
    const auto savedLabels = [&] {
        const auto settings = saved();
        return Labels(settings.value(u"canvas/geometrySizes"_s, true).toBool(),
                      settings.value(u"canvas/geometryCoordinates"_s, true).toBool());
    };
    QVERIFY(!action("showGeometrySizes"));
    QVERIFY(!action("showGeometryCoordinates"));
    // Rulers start at the canvas edges.
    QVERIFY(window_->findChild<LabelCanvas*>()->rulersDocked());
    const int historyIndex = session_->undoStack()->index();
    auto shown = editLabels(false, true, false);
    QVERIFY(shown);
    QCOMPARE(*shown, Labels(true, true));
    QCOMPARE(savedLabels(), Labels(false, true));
    QVERIFY(!saved().value(u"canvas/rulersDocked"_s).toBool());
    QVERIFY(!window_->findChild<LabelCanvas*>()->rulersDocked());
    // Workspace preferences never enter the label's undo history.
    QCOMPARE(session_->undoStack()->index(), historyIndex);
    window_.reset();
    createWindow();
    QVERIFY(!window_->findChild<LabelCanvas*>()->rulersDocked());
    shown = editLabels(true, true, true);
    QVERIFY(shown);
    QCOMPARE(*shown, Labels(false, true));
    QCOMPARE(savedLabels(), Labels(true, true));
    QVERIFY(saved().value(u"canvas/rulersDocked"_s).toBool());
}

void SplitEditorTests::sourceHighlightsPreserveCursorThemeAndNativeUndo()
{
    layout_->setCurrentIndex(1);
    QTextCursor cursor(code_->document());
    const int origin = static_cast<int>(initialSource.indexOf(u"40,48"));
    cursor.setPosition(origin);
    cursor.setPosition(origin + 2, QTextCursor::KeepAnchor);
    cursor.insertText(u"68"_s);
    code_->setTextCursor(cursor);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QVERIFY(code_->document()->isUndoAvailable());
    const auto edited = code_->toPlainText();
    cursor.setPosition(static_cast<int>(edited.indexOf(u"^PW")));
    cursor.setPosition(cursor.position() + 3, QTextCursor::KeepAnchor);
    code_->setTextCursor(cursor);
    canvas_->setFocus();
    QTRY_VERIFY(canvas_->hasFocus());
    QSignalSpy sourceChanges(code_, &QPlainTextEdit::textChanged);
    QSignalSpy cursorSelections(code_, &QPlainTextEdit::selectionChanged);
    QSignalSpy documentChanges(session_, &DocumentSession::documentChanged);
    canvas_->setSelectedIndex(0);
    const auto ranges = objectHighlightRanges(code_);
    QCOMPARE(ranges, SourceRanges{fieldRange(edited, u"^FO68,"_s)});
    for (const bool dark : {true, false}) {
        action("darkTheme")->setChecked(dark);
        QApplication::processEvents();
        QCOMPARE(objectHighlightRanges(code_), ranges);
        canvas_->setSelectedIndices({});
        QVERIFY(objectHighlightRanges(code_).isEmpty());
        canvas_->setSelectedIndex(0);
        QCOMPARE(objectHighlightRanges(code_), ranges);
        QCOMPARE(code_->textCursor().position(), cursor.position());
        QCOMPARE(code_->textCursor().anchor(), cursor.anchor());
        QVERIFY(code_->document()->isUndoAvailable());
        QVERIFY(canvas_->hasFocus());
        for (const auto& extra : code_->extraSelections())
            if (extra.cursor.hasSelection()) QVERIFY(extra.format.background().color().alpha() > 0);
    }
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(cursorSelections.count(), 0);
    QCOMPARE(documentChanges.count(), 0);
    QCOMPARE(code_->toPlainText(), edited);
    code_->undo();
    QCOMPARE(code_->toPlainText(), initialSource);
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 40);
}

void SplitEditorTests::sourceHighlightsClearPendingAndReturnAfterApply()
{
    layout_->setCurrentIndex(1);
    canvas_->setSelectedIndex(0);
    QVERIFY(!objectHighlightRanges(code_).isEmpty());
    QTextCursor cursor(code_->document());
    const int origin = static_cast<int>(initialSource.indexOf(u"40,48"));
    cursor.setPosition(origin);
    cursor.setPosition(origin + 2, QTextCursor::KeepAnchor);
    cursor.insertText(u"68"_s);
    code_->setTextCursor(cursor);
    QVERIFY(session_->hasPendingSource());
    QVERIFY(objectHighlightRanges(code_).isEmpty());
    cursor.setPosition(origin);
    cursor.setPosition(origin + 2, QTextCursor::KeepAnchor);
    code_->setTextCursor(cursor);
    QVERIFY(objectHighlightRanges(code_).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QCOMPARE(objectHighlightRanges(code_), SourceRanges{fieldRange(code_->toPlainText(), u"^FO68,"_s)});
    QCOMPARE(code_->textCursor().selectionStart(), origin);
    QCOMPARE(code_->textCursor().selectionEnd(), origin + 2);
}

void SplitEditorTests::sourceCaretClicksSelectExactFieldsAndRowFallbacks()
{
    const auto source = u"^XA\n^CI28^PW400\n^LL300\n^FXКомментарий^FS\n"
        "  ^FO20,20^A0N,20,10^FDOne^FS^FO80,20^A0N,20,10^FDTwo^FS  \n"
        "^FO140,20\n^A0N,20,10\n^FDThree^FS\n^XZ\n"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported && imported->visualEditable);
    window_->setDocument(*imported);
    layout_->setCurrentIndex(1);
    QTest::qWait(150);
    QSignalSpy documentChanges(session_, &DocumentSession::documentChanged);
    QSignalSpy sourceChanges(code_, &QPlainTextEdit::textChanged);
    const auto first = source.indexOf(u"^FO20,");
    const auto second = source.indexOf(u"^FO80,");
    const auto third = source.indexOf(u"^FO140,");

    clickSourcePosition(code_, first + 4);
    QVERIFY(code_->hasFocus());
    QVERIFY(!code_->textCursor().hasSelection());
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    clickSourcePosition(code_, second + 4);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    // Whitespace at the row edges belongs to the complete visible ZPL row.
    QTest::keyClick(code_, Qt::Key_Home);
    QVERIFY(!code_->textCursor().hasSelection());
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{0, 1}));
    QTest::keyClick(code_, Qt::Key_End);
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{0, 1}));
    clickSourcePosition(code_, source.indexOf(u"^A0", third) + 4);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{2});
    clickSourcePosition(code_, source.indexOf(u"^FD", third) + 4);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{2});
    clickSourcePosition(code_, source.indexOf(u"^FS", third) + 1);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{2});
    clickSourcePosition(code_, source.indexOf(u"^FX") + 4);
    QVERIFY(canvas_->selectedIndices().isEmpty());
    clickSourcePosition(code_, first + 4);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    clickSourcePosition(code_, source.indexOf(u"^PW") + 4);
    QVERIFY(canvas_->selectedIndices().isEmpty());
    QCOMPARE(documentChanges.count(), 0);
    QCOMPARE(sourceChanges.count(), 0);
    QCOMPARE(code_->toPlainText(), source);
    QCOMPARE(toZpl(window_->document()), source);
}

void SplitEditorTests::sourceCaretKeyboardKeepsMultiRangeSelection()
{
    const auto source = u"^XA\n^PW400\n^LL300\n"
        "^FO20,20^GB30,20,2^FS\n^FO80,20^GB30,20,2^FS\n^FO140,20^GB30,20,2^FS\n^XZ\n"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported && imported->visualEditable);
    window_->setDocument(*imported);
    layout_->setCurrentIndex(1);
    QTest::qWait(150);
    const auto first = source.indexOf(u"^FO20,");
    clickSourcePosition(code_, first + 4);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QTest::keyClick(code_, Qt::Key_Down);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    QTest::keyClick(code_, Qt::Key_Down);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{2});
    QTest::keyClick(code_, Qt::Key_Up);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    QTest::keyClick(code_, Qt::Key_Up);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});

    QTest::keyClick(code_, Qt::Key_Down, Qt::ShiftModifier);
    QVERIFY(code_->textCursor().hasSelection());
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{0, 1}));
    QTest::keyClick(code_, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(canvas_->selectedIndices(), (QList<int>{0, 1, 2}));
    // Collapsing a focused selection returns to the object at the active caret.
    QTest::keyClick(code_, Qt::Key_Right);
    QVERIFY(!code_->textCursor().hasSelection());
    QCOMPARE(canvas_->selectedIndices(), QList<int>{2});
    QCOMPARE(code_->toPlainText(), source);
    QVERIFY(!session_->modified());
}

void SplitEditorTests::sourceCaretReclickSelectsAfterCanvasChanges()
{
    const auto source = u"^XA\n^PW400\n^LL300\n"
        "^FO20,20^GB30,20,2^FS\n^FO80,20^GB30,20,2^FS\n^XZ\n"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported && imported->visualEditable);
    window_->setDocument(*imported);
    layout_->setCurrentIndex(1);
    QTest::qWait(150);
    clickSourcePosition(code_, source.indexOf(u"^FO80,") + 4);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    const int clickedPosition = code_->textCursor().position();
    canvas_->setFocus();
    QTRY_VERIFY(canvas_->hasFocus());
    canvas_->setSelectedIndex(0);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QCOMPARE(code_->textCursor().position(), clickedPosition);
    QSignalSpy caretMoves(code_, &QPlainTextEdit::cursorPositionChanged);
    clickSourcePosition(code_, clickedPosition);
    QVERIFY(code_->hasFocus());
    QCOMPARE(code_->textCursor().position(), clickedPosition);
    QCOMPARE(caretMoves.count(), 0);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    QCOMPARE(objectHighlightRanges(code_), SourceRanges{fieldRange(source, u"^FO80,"_s)});
}

void SplitEditorTests::sourceCaretDoesNotSyncWhenDisabledOrUnfocused()
{
    const auto source = u"^XA\n^PW400\n^LL300\n"
        "^FO20,20^GB30,20,2^FS\n^FO80,20^GB30,20,2^FS\n^FO140,20^GB30,20,2^FS\n^XZ\n"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported && imported->visualEditable);
    window_->setDocument(*imported);
    layout_->setCurrentIndex(1);
    QTest::qWait(150);
    canvas_->setFocus();
    QTRY_VERIFY(canvas_->hasFocus());
    canvas_->setSelectedIndex(0);
    QTextCursor cursor(code_->document());
    cursor.setPosition(static_cast<int>(source.indexOf(u"^FO80,")) + 4);
    code_->setTextCursor(cursor);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    auto* sync = action("syncEditorSelection");
    QVERIFY(sync && sync->isChecked());
    sync->setChecked(false);
    clickSourcePosition(code_, cursor.position());
    QVERIFY(code_->hasFocus());
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QTest::keyClick(code_, Qt::Key_Down);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QVERIFY(objectHighlightRanges(code_).isEmpty());
    const int thirdPosition = code_->textCursor().position();
    sync->setChecked(true);
    clickSourcePosition(code_, thirdPosition);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{2});
    QCOMPARE(code_->toPlainText(), source);
}

void SplitEditorTests::sourceCaretWaitsForPendingSourceToApply()
{
    const auto source = u"^XA\n^PW400\n^LL300\n"
        "^FO40,48^GB30,20,2^FS\n^FO180,48^GB30,20,2^FS\n^XZ\n"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported && imported->visualEditable);
    window_->setDocument(*imported);
    layout_->setCurrentIndex(1);
    QTest::qWait(150);
    clickSourcePosition(code_, source.indexOf(u"^FO40,") + 4);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QTextCursor cursor(code_->document());
    const int origin = static_cast<int>(source.indexOf(u"40,48"));
    cursor.setPosition(origin);
    cursor.setPosition(origin + 2, QTextCursor::KeepAnchor);
    cursor.insertText(u"68"_s);
    code_->setTextCursor(cursor);
    QVERIFY(session_->hasPendingSource());
    clickSourcePosition(code_, code_->toPlainText().indexOf(u"^FO180,") + 4);
    QVERIFY(!code_->textCursor().hasSelection());
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QVERIFY(objectHighlightRanges(code_).isEmpty());
    const int clickedPosition = code_->textCursor().position();
    QTRY_VERIFY_WITH_TIMEOUT(!session_->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 68);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{1});
    QCOMPARE(code_->textCursor().position(), clickedPosition);
    QVERIFY(!code_->textCursor().hasSelection());
    QCOMPARE(objectHighlightRanges(code_), SourceRanges{fieldRange(code_->toPlainText(), u"^FO180,"_s)});
}

void SplitEditorTests::sourceCaretDoesNotRetargetAfterPropertyShortensText()
{
    DesignDocument document;
    auto first = makeElement(ElementType::Text, {20, 20});
    first.text = QString(48, u'A');
    first.fontHeight = 20;
    first.fontWidth = 10;
    auto second = makeElement(ElementType::Text, {20, 100});
    second.text = QString(80, u'B');
    second.fontHeight = 20;
    second.fontWidth = 10;
    document.elements = {first, second};
    window_->setDocument(document);
    layout_->setCurrentIndex(1);
    QTest::qWait(150);
    const auto source = code_->toPlainText();
    const auto firstOrigin = source.indexOf(u"^FO20,20");
    clickSourcePosition(code_, firstOrigin + 4);
    QTest::keyClick(code_, Qt::Key_End);
    for (int move = 0; move < 4; ++move) QTest::keyClick(code_, Qt::Key_Left);
    const int previousPosition = code_->textCursor().position();
    QCOMPARE(previousPosition, source.indexOf(u"^FS", firstOrigin) - 1);
    QVERIFY(!code_->textCursor().hasSelection());
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});

    auto* content = window_->findChild<QPlainTextEdit*>(u"elementContent"_s);
    QVERIFY(content && content->isEnabled());
    content->setFocus();
    QTRY_VERIFY(content->hasFocus());
    content->selectAll();
    QTest::keyClicks(content, "X");
    QCOMPARE(window_->document().elements[0].text, u"X"_s);
    QCOMPARE(window_->document().elements[1].text, second.text);
    const auto regenerated = code_->toPlainText();
    const auto secondRange = fieldRange(regenerated, u"^FO20,100"_s);
    // The preserved numeric caret now lies in field 2. A property edit must
    // preserve its object selection instead of reinterpreting that old offset.
    QVERIFY(previousPosition >= secondRange.first);
    QVERIFY(previousPosition < secondRange.first + secondRange.second);
    QCOMPARE(code_->textCursor().position(), previousPosition);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QCOMPARE(objectHighlightRanges(code_), SourceRanges{fieldRange(regenerated, u"^FO20,20"_s)});
    QCOMPARE(content->toPlainText(), u"X"_s);
    QVERIFY(content->hasFocus());
    QVERIFY(!session_->hasPendingSource());
    QTest::qWait(150);
    QCOMPARE(canvas_->selectedIndices(), QList<int>{0});
    QCOMPARE(objectHighlightRanges(code_), SourceRanges{fieldRange(regenerated, u"^FO20,20"_s)});
}

QTEST_MAIN(SplitEditorTests)
#include "test_split_editor.moc"
