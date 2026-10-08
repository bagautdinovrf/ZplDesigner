#include "preview_pages.hpp"
#include "preview_widget.hpp"
#include "zpl_source_editor.hpp"
#include "main_window.hpp"

#include <QComboBox>
#include <QGraphicsView>
#include <QListWidget>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest>
#include <algorithm>

using namespace Qt::StringLiterals;
using namespace Designer;

class SourceToolsTest final : public QObject {
    Q_OBJECT
private slots:
    void literalSearchWrapsAndHonorsCase();
    void replacementIsAtomicAndDoesNotRevisitInsertedText();
    void readOnlySourceCannotBeReplaced();
    void highlightingAndNavigationPreserveSource();
    void previewsSelectIndependentLabelsWithoutChangingSource();
    void everyPageIsValidatedBeforeRendering();
    void unsupportedDiagnosticsPointIntoOriginalSource();
    void unsafeSourceDiagnosticsIdentifyTheCommand();
    void pageSelectorEmitsOnlyUserChanges();
    void windowSelectsPageAndNavigatesDiagnostic();
};

void SourceToolsTest::literalSearchWrapsAndHonorsCase()
{
    ZplSourceEditor editor;
    editor.setPlainText(u"^FDa.b A.B aXb^FS"_s);
    editor.setSearchText(u"a.b"_s);
    QVERIFY(editor.findNext());
    QCOMPARE(editor.textCursor().selectedText(), u"a.b"_s);
    QVERIFY(editor.findNext());
    QCOMPARE(editor.textCursor().selectedText(), u"A.B"_s);
    QVERIFY(editor.findNext());
    QCOMPARE(editor.textCursor().selectionStart(), 3);
    QVERIFY(editor.findPrevious());
    QCOMPARE(editor.textCursor().selectedText(), u"A.B"_s);
    editor.setSearchText(u"A.B"_s, true);
    QVERIFY(editor.findNext());
    QCOMPARE(editor.textCursor().selectedText(), u"A.B"_s);
    editor.setSearchText(u"absent"_s);
    QVERIFY(!editor.findNext());
    editor.setSearchText({});
    QVERIFY(!editor.findPrevious());
}

void SourceToolsTest::replacementIsAtomicAndDoesNotRevisitInsertedText()
{
    ZplSourceEditor editor;
    const auto source = u"^XA\n^FDx x x^FS\n^XZ"_s;
    editor.setPlainText(source);
    editor.setSearchText(u"x"_s, true);
    editor.setReplacementText(u"xx"_s);
    QCOMPARE(editor.replaceAll(), 3);
    QCOMPARE(editor.toPlainText(), u"^XA\n^FDxx xx xx^FS\n^XZ"_s);
    editor.undo();
    QCOMPARE(editor.toPlainText(), source);
    QVERIFY(!editor.document()->isUndoAvailable());
    editor.setReplacementText({});
    QCOMPARE(editor.replaceAll(), 3);
    QCOMPARE(editor.toPlainText(), u"^XA\n^FD  ^FS\n^XZ"_s);
    editor.undo();
    QCOMPARE(editor.toPlainText(), source);
    editor.setReplacementText(u"value"_s);
    editor.goToSourceOffset(0);
    QVERIFY(editor.replaceCurrent());
    QCOMPARE(editor.toPlainText(), u"^XA\n^FDvalue x x^FS\n^XZ"_s);
    editor.undo();
    QCOMPARE(editor.toPlainText(), source);
}

void SourceToolsTest::readOnlySourceCannotBeReplaced()
{
    ZplSourceEditor editor;
    const auto source = u"^XA^FDtext^FS^XZ"_s;
    editor.setPlainText(source);
    editor.setReadOnly(true);
    editor.setSearchText(u"text"_s);
    editor.setReplacementText(u"changed"_s);
    QVERIFY(editor.findNext());
    QVERIFY(!editor.replaceCurrent());
    QCOMPARE(editor.replaceAll(), 0);
    QCOMPARE(editor.toPlainText(), source);
}

void SourceToolsTest::highlightingAndNavigationPreserveSource()
{
    ZplSourceEditor editor;
    const auto source = u"^XA\n^FX retained comment\n^FO10,20^FDliteral ^ZZ data\ncontinued^FS\n^ZZunknown\n^XZ"_s;
    editor.setPlainText(source);
    editor.resize(640, 400);
    editor.show();
    QApplication::processEvents();
    QVERIFY(editor.findChild<QWidget*>(u"sourceLineNumbers"_s));
    QVERIFY(!editor.document()->firstBlock().layout()->formats().isEmpty());
    const int offset = static_cast<int>(source.indexOf(u"^ZZunknown"_s));
    editor.goToSourceOffset(offset);
    QCOMPARE(editor.textCursor().position(), offset);
    editor.goToSourceOffset(-100);
    QCOMPARE(editor.textCursor().position(), 0);
    editor.goToSourceOffset(100000);
    QCOMPARE(editor.textCursor().position(), source.size());
    QCOMPARE(editor.toPlainText(), source);
    QVERIFY(!editor.document()->isUndoAvailable());
}

void SourceToolsTest::previewsSelectIndependentLabelsWithoutChangingSource()
{
    const auto source = u"^XA^PW80^LL50^FO5,5^GB20,10,2^FS^XZ\n"
                        u"^XA^PW120^LL70^FO30,20^GB40,30,3^FS^XZ"_s;
    const auto document = fromZpl(source);
    QVERIFY(document.has_value());
    QVERIFY(!document->visualEditable);
    const auto count = previewPageCount(*document);
    QVERIFY(count.has_value());
    QCOMPARE(*count, 2);
    const auto first = renderPreviewPage(*document, 0);
    const auto second = renderPreviewPage(*document, 1);
    QVERIFY(first.has_value());
    QVERIFY(second.has_value());
    QCOMPARE(first->image.size(), QSize(80, 50));
    QCOMPARE(second->image.size(), QSize(120, 70));
    QCOMPARE(renderPreview(*document)->image, first->image);
    QVERIFY(!renderPreviewPage(*document, -1));
    QVERIFY(!renderPreviewPage(*document, 2));
    QCOMPARE(toZpl(*document), source);
}

void SourceToolsTest::everyPageIsValidatedBeforeRendering()
{
    DesignDocument document;
    document.visualEditable = false;
    document.originalZpl = u"^XA^PW80^LL50^XZ\n^XA^PW5000^LL70^XZ"_s;
    QVERIFY(!previewPageCount(document));
    QVERIFY(!renderPreviewPage(document, 0));
    QVERIFY(!renderPreviewPage(document, 1));
    document.originalZpl.clear();
    for (int i = 0; i < 17; ++i) document.originalZpl += u"^XA^PW80^LL50^XZ\n"_s;
    QVERIFY(!previewPageCount(document));
    QVERIFY(!renderPreviewPage(document, 0));
}

void SourceToolsTest::unsupportedDiagnosticsPointIntoOriginalSource()
{
    const auto source = u"^XA^PW80^LL50^XZ\n^XA^PW80^LL50^ZZretained^XZ"_s;
    const auto document = fromZpl(source);
    QVERIFY(document.has_value());
    const auto diagnostics = sourceDiagnostics(*document, 1);
    const int offset = static_cast<int>(source.indexOf(u"^ZZ"_s));
    QVERIFY(std::ranges::any_of(diagnostics, [offset](const SourceDiagnostic& diagnostic) {
        return diagnostic.offset == offset && !diagnostic.text.isEmpty();
    }));
    QCOMPARE(toZpl(*document), source);
}

void SourceToolsTest::unsafeSourceDiagnosticsIdentifyTheCommand()
{
    for (const auto& command : {u"^FO5000,10"_s, u"^PW5000"_s}) {
        DesignDocument document;
        document.visualEditable = false;
        document.originalZpl = u"^XA^PW80^LL50"_s + command + u"^FDx^FS^XZ"_s;
        const int offset = static_cast<int>(document.originalZpl.indexOf(command));
        const auto diagnostics = sourceDiagnostics(document);
        QVERIFY(std::ranges::any_of(diagnostics, [offset](const SourceDiagnostic& diagnostic) {
            return diagnostic.offset == offset && !diagnostic.text.isEmpty();
        }));
    }
}

void SourceToolsTest::pageSelectorEmitsOnlyUserChanges()
{
    PreviewWidget preview;
    QSignalSpy requested(&preview, &PreviewWidget::pageRequested);
    preview.setPages(3, 1);
    QCOMPARE(preview.pageCount(), 3);
    QCOMPARE(preview.currentPage(), 1);
    QCOMPARE(requested.count(), 0);
    auto* selector = preview.findChild<QComboBox*>(u"previewPageSelector"_s);
    QVERIFY(selector);
    selector->setCurrentIndex(2);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.first().first().toInt(), 2);
    preview.setPages(1, 8);
    QCOMPARE(preview.currentPage(), 0);
    QCOMPARE(requested.count(), 1);
}

void SourceToolsTest::windowSelectsPageAndNavigatesDiagnostic()
{
    const auto source = u"^XA^PW80^LL50^FO5,5^GB20,10,2^FS^XZ\n"
                        u"^XA^PW120^LL70^FO30,20^GB40,30,3^FS^ZZretained^XZ"_s;
    const auto document = fromZpl(source);
    QVERIFY(document.has_value());
    MainWindow window;
    window.setDocument(*document);
    window.show();
    window.showPreview();
    QApplication::processEvents();
    auto* preview = window.findChild<PreviewWidget*>(u"previewWidget"_s);
    auto* sourceEditor = window.findChild<ZplSourceEditor*>(u"zplEditor"_s);
    auto* tabs = window.findChild<QTabWidget*>(u"editorTabs"_s);
    auto* diagnostics = window.findChild<QListWidget*>(u"sourceDiagnostics"_s);
    QVERIFY(preview);
    QVERIFY(sourceEditor);
    QVERIFY(tabs);
    QVERIFY(diagnostics);
    QCOMPARE(tabs->currentWidget(), preview);
    auto* selector = preview->findChild<QComboBox*>(u"previewPageSelector"_s);
    auto* view = preview->findChild<QGraphicsView*>(u"previewView"_s);
    QVERIFY(selector);
    QVERIFY(view);
    QCOMPARE(view->sceneRect(), QRectF(0, 0, 80, 50));
    selector->setCurrentIndex(1);
    QCOMPARE(view->sceneRect(), QRectF(0, 0, 120, 70));
    QCOMPARE(sourceEditor->toPlainText(), source);
    QCOMPARE(toZpl(window.document()), source);
    const int offset = static_cast<int>(source.indexOf(u"^ZZ"_s));
    QListWidgetItem* target = nullptr;
    for (int i = 0; i < diagnostics->count(); ++i)
        if (diagnostics->item(i)->data(Qt::UserRole).toInt() == offset) target = diagnostics->item(i);
    QVERIFY(target);
    // Activate through the same signal used by keyboard and double-click UI.
    QVERIFY(QMetaObject::invokeMethod(diagnostics, "itemActivated", Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem*, target)));
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(sourceEditor->textCursor().position(), offset);
    QCOMPARE(sourceEditor->toPlainText(), source);
}

QTEST_MAIN(SourceToolsTest)
#include "test_source_tools.moc"
