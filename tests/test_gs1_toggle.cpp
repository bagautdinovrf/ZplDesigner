#include "main_window.hpp"
#include "label_canvas.hpp"
#include "preview_widget.hpp"
#include "data_matrix_values.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QGraphicsView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUuid>
#include <QtTest>

#include <memory>

using namespace Qt::StringLiterals;
using Designer::DesignDocument;
using Designer::ElementType;

namespace {
class ResizeCounter final : public QObject {
public:
    int count = 0;

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Resize) ++count;
        return QObject::eventFilter(object, event);
    }
};
} // namespace

class Gs1ToggleTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void defaultSampleStartsWithValidGs1AndUndo();
    void invalidGs1InFitModeKeepsEventLoopResponsive_data();
    void invalidGs1InFitModeKeepsEventLoopResponsive();
    void invalidDataMatrixKeepsOtherElementsAndBlocksCleanExport();
    void validDataCanToggleAndUndo();
    void validTemplatesCanEnable_data();
    void validTemplatesCanEnable();
    void invalidResolvedTemplatesCanEnableButCannotRender_data();
    void invalidResolvedTemplatesCanEnableButCannotRender();
    void disablingInvalidGs1RestoresOrdinaryDataMatrix();
    void pendingSourcePreventsGs1ToggleAndPreservesEdits();

private:
    void loadDataMatrix(const QString& text, bool gs1 = false,
                        const QMap<QString, QString>& values = {});

    std::unique_ptr<Designer::MainWindow> window_;
    Designer::LabelCanvas* canvas_ = nullptr;
    QCheckBox* mode_ = nullptr;
    QAction* undo_ = nullptr;
    QString previousApplicationName_;
    QString workspaceSettingsDirectory_;
    bool previousTestMode_ = false;
};

void Gs1ToggleTests::init()
{
    previousApplicationName_ = QCoreApplication::applicationName();
    previousTestMode_ = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"gs1-toggle-tests-%1"_s
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    workspaceSettingsDirectory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    window_ = std::make_unique<Designer::MainWindow>();
    window_->show();
    QApplication::processEvents();
    canvas_ = window_->findChild<Designer::LabelCanvas*>(u"labelCanvas"_s);
    mode_ = window_->findChild<QCheckBox*>(u"dataMatrixGs1"_s);
    undo_ = window_->findChild<QAction*>(u"undoAction"_s);
    QVERIFY(canvas_);
    QVERIFY(mode_);
    QVERIFY(undo_);
}

void Gs1ToggleTests::cleanup()
{
    if (window_) {
        window_->setDocument({});
        window_.reset();
    }
    QFile::remove(QDir(workspaceSettingsDirectory_).filePath(u"workspace.ini"_s));
    QDir().rmdir(workspaceSettingsDirectory_);
    QCoreApplication::setApplicationName(previousApplicationName_);
    QStandardPaths::setTestModeEnabled(previousTestMode_);
}

void Gs1ToggleTests::loadDataMatrix(const QString& text, bool gs1,
                                  const QMap<QString, QString>& values)
{
    DesignDocument document;
    auto element = Designer::makeElement(ElementType::DataMatrix);
    element.text = text;
    element.gs1 = gs1;
    document.elements = {element};
    document.previewValues = values;
    window_->setDocument(document);
    canvas_->setSelectedIndex(0);
}

void Gs1ToggleTests::defaultSampleStartsWithValidGs1AndUndo()
{
    const auto initial = window_->document();
    int matrixIndex = -1;
    for (qsizetype i = 0; i < initial.elements.size(); ++i)
        if (initial.elements[i].type == ElementType::DataMatrix) matrixIndex = static_cast<int>(i);
    QVERIFY(matrixIndex >= 0);
    const auto& matrix = initial.elements[matrixIndex];
    QCOMPARE(matrix.text, u"(01)09506000134352(21)NL-00001"_s);
    QVERIFY(matrix.gs1);
    QVERIFY(Designer::makeElement(ElementType::DataMatrix).gs1);
    const auto before = Designer::renderPreview(initial);
    QVERIFY(before.has_value());
    canvas_->setSelectedIndex(matrixIndex);
    QVERIFY(mode_->isChecked());
    QTest::mouseClick(mode_, Qt::LeftButton, Qt::NoModifier, QPoint(8, mode_->height() / 2));
    QTest::qWait(150);
    QVERIFY(!mode_->isChecked());
    QVERIFY(!window_->document().elements[matrixIndex].gs1);
    QCOMPARE(window_->document().elements[matrixIndex].text, matrix.text);
    const auto encoded = Designer::renderPreview(window_->document());
    QVERIFY(encoded.has_value());
    QVERIFY(encoded->image != before->image);
    const auto editor = Designer::renderEditorPreview(window_->document());
    QVERIFY(editor.has_value());
    QVERIFY(editor->elementErrors.isEmpty());
    window_->showPreview();
    const auto* preview = window_->findChild<Designer::PreviewWidget*>(u"previewWidget"_s);
    QVERIFY(preview);
    const auto* exportPng = preview->findChild<QPushButton*>(u"previewExportPng"_s);
    QVERIFY(exportPng);
    QVERIFY(exportPng->isEnabled());
    auto* code = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    QVERIFY(code);
    QVERIFY(!code->isReadOnly());
    QVERIFY(code->toPlainText().contains(u"^BX"_s));
    QVERIFY(undo_->isEnabled());
    undo_->trigger();
    QVERIFY(mode_->isChecked());
    const auto undone = Designer::renderPreview(window_->document());
    QVERIFY(undone.has_value());
    QCOMPARE(undone->image, before->image);
    window_->findChild<QAction*>(u"redoAction"_s)->trigger();
    QVERIFY(!mode_->isChecked());
    window_->refreshPreview();
    QVERIFY(exportPng->isEnabled());
}

void Gs1ToggleTests::invalidGs1InFitModeKeepsEventLoopResponsive_data()
{
    QTest::addColumn<QSize>("windowSize");
    QTest::newRow("minimum") << QSize(1050, 680);
    QTest::newRow("compact") << QSize(1180, 760);
    QTest::newRow("hd") << QSize(1280, 720);
    QTest::newRow("laptop") << QSize(1366, 768);
    QTest::newRow("default") << QSize(1440, 900);
    QTest::newRow("wide") << QSize(1600, 900);
    QTest::newRow("maximized") << QSize{};
}

void Gs1ToggleTests::invalidGs1InFitModeKeepsEventLoopResponsive()
{
    QFETCH(QSize, windowSize);
    if (windowSize.isEmpty()) window_->showMaximized();
    else window_->resize(windowSize);
    // Keep the launch-time fit mode active. Neither selecting the preview tab
    // nor actualSize() may alter the resize/scrollbar path exercised here.
    QTest::qWait(150);
    const auto* preview = window_->findChild<Designer::PreviewWidget*>(u"previewWidget"_s);
    QVERIFY(preview);
    QVERIFY(!preview->isVisible());
    auto initial = window_->document();
    int matrixIndex = -1;
    for (qsizetype i = 0; i < initial.elements.size(); ++i)
        if (initial.elements[i].type == ElementType::DataMatrix) matrixIndex = static_cast<int>(i);
    QVERIFY(matrixIndex >= 0);
    initial.elements[matrixIndex].text = u"NL-00001"_s;
    initial.elements[matrixIndex].gs1 = false;
    window_->setDocument(initial);
    canvas_->setSelectedIndex(matrixIndex);
    QTest::qWait(150);
    QVERIFY(mode_->isVisible());
    QVERIFY(!mode_->isChecked());
    ResizeCounter resizes;
    canvas_->viewport()->installEventFilter(&resizes);
    QTest::mouseClick(mode_, Qt::LeftButton, Qt::NoModifier, QPoint(8, mode_->height() / 2));
    QTest::qWait(200);
    QVERIFY(mode_->isChecked());
    QVERIFY(window_->document().elements[matrixIndex].gs1);
    QCOMPARE(window_->document().elements[matrixIndex].text, initial.elements[matrixIndex].text);
    QCOMPARE(window_->document().elements.size(), initial.elements.size());
    QVERIFY(undo_->isEnabled());
    const auto* status = window_->findChild<QLabel*>(u"gs1Status"_s);
    QVERIFY(status);
    QVERIFY(!status->text().isEmpty());
    // A new wrapped notice can resize the canvas, but scrollbar layout must
    // settle rather than repeatedly feeding resizeEvent back into fitLabel.
    QVERIFY2(resizes.count < 32, "GS1 notice caused repeated canvas viewport resizing");
    const int settled = resizes.count;
    QTest::qWait(100);
    QCOMPARE(resizes.count, settled);
    QVERIFY(!preview->isVisible());
    QVERIFY(!Designer::renderPreview(window_->document()).has_value());
    const auto editor = Designer::renderEditorPreview(window_->document());
    QVERIFY(editor.has_value());
    QVERIFY(editor->elementErrors.contains(matrixIndex));
    QVERIFY(!editor->image.isNull());
    QVERIFY(!canvas_->viewport()->grab().isNull());
}

void Gs1ToggleTests::invalidDataMatrixKeepsOtherElementsAndBlocksCleanExport()
{
    // Keep the pixel comparison independent of the changing startup sample.
    DesignDocument initial;
    initial.width = 812;
    initial.height = 406;
    auto frame = Designer::makeElement(ElementType::Box, {24, 24});
    frame.width = 764; frame.height = 358; frame.thickness = 2;
    auto title = Designer::makeElement(ElementType::Text, {48, 48});
    title.text = u"Контрольный текст"_s; title.fontHeight = 36;
    auto matrix = Designer::makeElement(ElementType::DataMatrix, {612, 180});
    matrix.text = u"NL-00001"_s; matrix.moduleWidth = 6; matrix.gs1 = false;
    initial.elements = {frame, title, matrix};
    constexpr int matrixIndex = 2;
    window_->setDocument(initial);
    QVERIFY(!initial.elements[matrixIndex].gs1);
    canvas_->setSelectedIndex(matrixIndex);
    window_->showPreview();
    const auto* preview = window_->findChild<Designer::PreviewWidget*>(u"previewWidget"_s);
    QVERIFY(preview);
    const auto* view = preview->findChild<QGraphicsView*>(u"previewView"_s);
    const auto* exportPng = preview->findChild<QPushButton*>(u"previewExportPng"_s);
    const auto* code = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    QVERIFY(view);
    QVERIFY(exportPng);
    QVERIFY(code);
    QVERIFY(exportPng->isEnabled());
    const auto initialScene = view->sceneRect();
    const auto before = Designer::renderPreview(initial);
    QVERIFY(before.has_value());
    const auto source = Designer::toZpl(initial);
    auto* tabs = window_->findChild<QTabWidget*>(u"editorTabs"_s);
    QVERIFY(tabs);
    tabs->setCurrentWidget(canvas_);
    canvas_->actualSize();
    QApplication::processEvents();
    // This assertion checks canvas pixels, independently of layout behavior.
    // Keep the viewport geometry stable so a wrapped notice cannot change the
    // centering or device-pixel phase of the same raster. The data-driven fit
    // test above deliberately leaves geometry unconstrained to cover resizing.
    canvas_->setFixedSize(canvas_->size());
    QApplication::processEvents();
    const QSize viewportSize = canvas_->viewport()->size();
    const QPoint labelOrigin = canvas_->mapFromScene(QPointF(0, 0));
    const auto labelPixels = [this, &initial] {
        QApplication::processEvents();
        const QImage screenshot = canvas_->viewport()->grab().toImage();
        const qreal dpr = screenshot.devicePixelRatio();
        const QPoint origin = canvas_->mapFromScene(QPointF(0, 0));
        return screenshot.copy(qRound(origin.x() * dpr), qRound(origin.y() * dpr),
            qRound(initial.width * dpr), qRound(initial.height * dpr));
    };
    const auto canvasBefore = labelPixels();
    const qreal dpr = canvas_->viewport()->devicePixelRatioF();
    const QRect validContent(qRound(24 * dpr), qRound(24 * dpr), qRound(560 * dpr), qRound(150 * dpr));
    QTest::mouseClick(mode_, Qt::LeftButton, Qt::NoModifier, QPoint(8, mode_->height() / 2));
    // Check the settled UI after the deferred preview timer could have fired.
    QTest::qWait(150);

    QVERIFY(mode_->isChecked());
    QVERIFY(window_->document().elements[matrixIndex].gs1);
    QCOMPARE(window_->document().elements.size(), initial.elements.size());
    QCOMPARE(window_->document().elements[matrixIndex].text, u"NL-00001"_s);
    QVERIFY(undo_->isEnabled());
    const auto* status = window_->findChild<QLabel*>(u"gs1Status"_s);
    QVERIFY(status);
    QVERIFY(!status->text().isEmpty());
    QCOMPARE(canvas_->viewport()->size(), viewportSize);
    QCOMPARE(canvas_->mapFromScene(QPointF(0, 0)), labelOrigin);
    // Text and frame at the top-left remain exactly visible on the canvas;
    // only the invalid DataMatrix at the right is replaced by an error marker.
    QCOMPARE(labelPixels().copy(validContent), canvasBefore.copy(validContent));
    const auto editor = Designer::renderEditorPreview(window_->document());
    QVERIFY(editor.has_value());
    QVERIFY(editor->elementErrors.contains(matrixIndex));
    QCOMPARE(editor->bounds.size(), initial.elements.size());
    QCOMPARE(editor->image.copy(QRect(24, 24, 560, 150)), before->image.copy(QRect(24, 24, 560, 150)));
    for (qsizetype i = 0; i < initial.elements.size(); ++i) {
        if (i != matrixIndex)
            QCOMPARE(Designer::elementZpl(window_->document().elements[i]), Designer::elementZpl(initial.elements[i]));
    }
    QVERIFY(!Designer::renderPreview(window_->document()).has_value());
    QVERIFY(!Designer::validateForExport(window_->document()).has_value());
    QVERIFY(code->isReadOnly());
    QVERIFY(code->toPlainText().isEmpty());
    QVERIFY(!code->placeholderText().isEmpty());
    QVERIFY(!exportPng->isEnabled());
    QCOMPARE(view->sceneRect(), QRectF(0, 0, 1, 1));

    mode_->setChecked(false);
    window_->refreshPreview();
    const auto after = Designer::renderPreview(window_->document());
    QVERIFY(after.has_value());
    QCOMPARE(after->image, before->image);
    QCOMPARE(Designer::toZpl(window_->document()), source);
    QCOMPARE(labelPixels(), canvasBefore);
    QCOMPARE(view->sceneRect(), initialScene);
    QVERIFY(exportPng->isEnabled());
    QVERIFY(!code->isReadOnly());
    QCOMPARE(code->toPlainText(), source);

    // Invalid mode is a normal undoable property change, including after the
    // checkbox is switched off to recover the original clean label.
    undo_->trigger();
    QVERIFY(mode_->isChecked());
    QCOMPARE(window_->document().elements[matrixIndex].text, u"NL-00001"_s);
    undo_->trigger();
    QVERIFY(!mode_->isChecked());
    QCOMPARE(Designer::toZpl(window_->document()), source);
    window_->findChild<QAction*>(u"redoAction"_s)->trigger();
    QVERIFY(mode_->isChecked());

    auto* content = window_->findChild<QPlainTextEdit*>(u"elementContent"_s);
    QVERIFY(content);
    content->setPlainText(u"(01)09506000134352(21)SERIAL1"_s);
    window_->refreshPreview();
    QVERIFY(mode_->isChecked());
    QVERIFY(Designer::renderPreview(window_->document()).has_value());
    const auto corrected = Designer::renderEditorPreview(window_->document());
    QVERIFY(corrected.has_value());
    QVERIFY(corrected->elementErrors.isEmpty());
    QVERIFY(exportPng->isEnabled());
    QCOMPARE(view->sceneRect(), initialScene);
    QVERIFY(!code->isReadOnly());
    QVERIFY(code->toPlainText().contains(u"^BX"_s));
}

void Gs1ToggleTests::validDataCanToggleAndUndo()
{
    const auto text = u"(01)09506000134352(21)SERIAL1"_s;
    loadDataMatrix(text);
    QTest::mouseClick(mode_, Qt::LeftButton, Qt::NoModifier, QPoint(8, mode_->height() / 2));
    QVERIFY(mode_->isChecked());
    QVERIFY(window_->document().elements.first().gs1);
    QCOMPARE(window_->document().elements.first().text, text);
    QVERIFY(Designer::renderPreview(window_->document()).has_value());
    QVERIFY(undo_->isEnabled());
    undo_->trigger();
    QVERIFY(!mode_->isChecked());
    QVERIFY(!window_->document().elements.first().gs1);
    QCOMPARE(window_->document().elements.first().text, text);
    QVERIFY(!undo_->isEnabled());
    window_->findChild<QAction*>(u"redoAction"_s)->trigger();
    QVERIFY(mode_->isChecked());
    QTest::mouseClick(mode_, Qt::LeftButton, Qt::NoModifier, QPoint(8, mode_->height() / 2));
    QVERIFY(!window_->document().elements.first().gs1);
    QCOMPARE(window_->document().elements.first().text, text);
}

void Gs1ToggleTests::validTemplatesCanEnable_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("parameter");
    QTest::addColumn<QString>("value");
    QTest::newRow("structured-unresolved") << u"(01){{gtin}}(21)SERIAL1"_s << QString{} << QString{};
    QTest::newRow("whole-code-unresolved") << u"[[datamatrix]]"_s << QString{} << QString{};
    QTest::newRow("structured-resolved") << u"(01){{gtin}}(21)SERIAL1"_s << u"gtin"_s << u"09506000134352"_s;
    QTest::newRow("whole-code-resolved") << u"[[datamatrix]]"_s << u"datamatrix"_s << u"(01)09506000134352(21)SERIAL1"_s;
}

void Gs1ToggleTests::validTemplatesCanEnable()
{
    QFETCH(QString, text);
    QFETCH(QString, parameter);
    QFETCH(QString, value);
    QMap<QString, QString> values;
    if (!parameter.isEmpty()) values.insert(parameter, value);
    loadDataMatrix(text, false, values);
    mode_->setChecked(true);
    QVERIFY(mode_->isChecked());
    QVERIFY(window_->document().elements.first().gs1);
    QCOMPARE(window_->document().elements.first().text, text);
    QVERIFY(Designer::validateForExport(window_->document()).has_value());
    if (!values.isEmpty()) QVERIFY(Designer::renderPreview(window_->document()).has_value());
}

void Gs1ToggleTests::invalidResolvedTemplatesCanEnableButCannotRender_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("parameter");
    QTest::addColumn<QString>("value");
    QTest::newRow("invalid-gtin") << u"(01){{gtin}}(21)SERIAL1"_s << u"gtin"_s << u"09506000134353"_s;
    QTest::newRow("ordinary-whole-code") << u"[[datamatrix]]"_s << u"datamatrix"_s << u"NL-00001"_s;
}

void Gs1ToggleTests::invalidResolvedTemplatesCanEnableButCannotRender()
{
    QFETCH(QString, text);
    QFETCH(QString, parameter);
    QFETCH(QString, value);
    loadDataMatrix(text, false, {{parameter, value}});
    const auto source = Designer::toZpl(window_->document());
    const auto before = Designer::renderPreview(window_->document());
    QVERIFY(before.has_value());
    mode_->setChecked(true);
    QVERIFY(mode_->isChecked());
    QVERIFY(window_->document().elements.first().gs1);
    QCOMPARE(window_->document().elements.first().text, text);
    QCOMPARE(window_->document().previewValues.value(parameter), value);
    QVERIFY(undo_->isEnabled());
    QVERIFY(!Designer::renderPreview(window_->document()).has_value());
    const auto editor = Designer::renderEditorPreview(window_->document());
    QVERIFY(editor.has_value());
    QVERIFY(editor->elementErrors.contains(0));
    // Saving an unresolved template may remain legal; exporting the actual
    // invalid substituted payload must fail instead of producing an ordinary DM.
    QVERIFY(!Designer::resolveDataMatrixValues(window_->document()).has_value());
    undo_->trigger();
    QVERIFY(!mode_->isChecked());
    QCOMPARE(Designer::toZpl(window_->document()), source);
    const auto after = Designer::renderPreview(window_->document());
    QVERIFY(after.has_value());
    QCOMPARE(after->image, before->image);
}

void Gs1ToggleTests::disablingInvalidGs1RestoresOrdinaryDataMatrix()
{
    loadDataMatrix(u"NL-00001"_s, true);
    QVERIFY(mode_->isChecked());
    QVERIFY(!Designer::renderPreview(window_->document()).has_value());
    mode_->setChecked(false);
    QVERIFY(!mode_->isChecked());
    QVERIFY(!window_->document().elements.first().gs1);
    QCOMPARE(window_->document().elements.first().text, u"NL-00001"_s);
    QVERIFY(Designer::renderPreview(window_->document()).has_value());
}

void Gs1ToggleTests::pendingSourcePreventsGs1ToggleAndPreservesEdits()
{
    loadDataMatrix(u"NL-00001"_s);
    const auto source = Designer::toZpl(window_->document());
    auto* code = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    QVERIFY(code);
    const auto edited = source + u"\n^FX Unsaved source edit\n"_s;
    code->setPlainText(edited);
    mode_->setChecked(true);
    QVERIFY(!mode_->isChecked());
    QVERIFY(!window_->document().elements.first().gs1);
    QCOMPARE(window_->document().elements.first().text, u"NL-00001"_s);
    QCOMPARE(Designer::toZpl(window_->document()), source);
    QCOMPARE(code->toPlainText(), edited);
    QVERIFY(!undo_->isEnabled());
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(u"QtZpl Designer"_s);
    application.setOrganizationName(u"QtZpl"_s);
    Designer::installDesignerStyle();
    Gs1ToggleTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_gs1_toggle.moc"
