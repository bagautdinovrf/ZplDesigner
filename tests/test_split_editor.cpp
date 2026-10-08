#include "designer_theme.hpp"
#include "document_session.hpp"
#include "label_canvas.hpp"
#include "main_window.hpp"
#include "preview_widget.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QUuid>
#include <QtTest>

#include <memory>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
const QString initialSource = u"^XA\n^PW400\n^LL300\n^FO40,48^GB80,56,2^FS\n^XZ\n"_s;

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

QTEST_MAIN(SplitEditorTests)
#include "test_split_editor.moc"
