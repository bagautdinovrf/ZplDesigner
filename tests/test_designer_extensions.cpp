#include "main_window.hpp"
#include "document_session.hpp"
#include "dot_spin_box.hpp"
#include "label_canvas.hpp"
#include "qr_payload_dialog.hpp"
#include "table_dialog.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;
class DesignerExtensionsTests final : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void unitsRoundWithoutChangingStoredDots();
    void layerFlagsProtectEditingAndKeepPrintIndependent();
    void layerOrderKeepsSelectionAndUndo();
    void propertyUnitsDoNotChangeZpl();
    void tableDialogKeepsCellsAndDimensions();
    void qrFormsEscapeAndValidate();
    void projectWindowPreservesUnappliedSource();
    void hiddenObjectsAreSkippedWhenSelectingBehind();
    void extendedPaletteAndBarcodeSettings();
    void whiteGraphicPropertiesRoundTrip();
    void dataMatrixDefaultsToGs1ForSamplePaletteAndDrop();
private:
    QString previousApplicationName_;
    QString workspaceSettingsDirectory_;
    bool previousTestMode_ = false;
};

void DesignerExtensionsTests::init()
{
    previousApplicationName_ = QCoreApplication::applicationName();
    previousTestMode_ = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"designer-extensions-%1"_s
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    workspaceSettingsDirectory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

void DesignerExtensionsTests::cleanup()
{
    QFile::remove(QDir(workspaceSettingsDirectory_).filePath(u"workspace.ini"_s));
    QDir().rmdir(workspaceSettingsDirectory_);
    QCoreApplication::setApplicationName(previousApplicationName_);
    QStandardPaths::setTestModeEnabled(previousTestMode_);
}

void DesignerExtensionsTests::unitsRoundWithoutChangingStoredDots() {
    DotSpinBox spin;
    spin.setLocale(QLocale::c()); spin.setRange(-4096, 4096); spin.setValue(100);
    QSignalSpy changes(&spin, &QSpinBox::valueChanged);
    spin.setDisplayUnits(CanvasUnit::Millimeters, 203);
    QCOMPARE(spin.value(), 100); QCOMPARE(changes.size(), 0);
    QVERIFY(spin.text().contains(u"12.512"));
    spin.show(); spin.setFocus();
    auto* editor = spin.findChild<QLineEdit*>(); QVERIFY(editor);
    editor->selectAll(); QTest::keyClicks(editor, "25.4"); QTest::keyClick(editor, Qt::Key_Return);
    QCOMPARE(spin.value(), 203);
    changes.clear();
    spin.setDisplayUnits(CanvasUnit::Dots, 600); spin.setDisplayUnits(CanvasUnit::Millimeters, 600);
    QCOMPARE(spin.value(), 203); QCOMPARE(changes.size(), 0);
    QVERIFY(spin.text().contains(u"8.594"));
}

void DesignerExtensionsTests::layerFlagsProtectEditingAndKeepPrintIndependent() {
    MainWindow window; window.show();
    DesignDocument document; document.width = 400; document.height = 250;
    auto box = makeElement(ElementType::Box, {50, 60}); box.width = 80; box.height = 50;
    document.elements = {box}; window.setDocument(document);
    auto* canvas = window.findChild<LabelCanvas*>(); QVERIFY(canvas); canvas->setSelectedIndex(0);
    QVERIFY(window.findChild<QListWidget*>(u"layerList"_s)->isVisible());
    auto* locked = window.findChild<QCheckBox*>(u"layerLocked"_s); QVERIFY(locked); locked->click();
    QVERIFY(window.document().elements[0].locked);
    QTest::keyClick(canvas, Qt::Key_Right); QCOMPARE(window.document().elements[0].x, 50);
    QTest::keyClick(canvas, Qt::Key_Delete); QCOMPARE(window.document().elements.size(), 1);
    auto* x = window.findChild<QSpinBox*>(u"elementX"_s); QVERIFY(x); QVERIFY(!x->isEnabled());
    locked->click(); QVERIFY(!window.document().elements[0].locked);
    const auto originalPrint = renderPreview(window.document()); QVERIFY(originalPrint);
    window.findChild<QCheckBox*>(u"layerVisible"_s)->click();
    QVERIFY(!window.document().elements[0].editorVisible); QVERIFY(window.document().elements[0].printable);
    const auto hiddenPrint = renderPreview(window.document()); QVERIFY(hiddenPrint);
    QCOMPARE(hiddenPrint->image, originalPrint->image);
    const auto hiddenEditor = renderEditorPreview(window.document()); QVERIFY(hiddenEditor);
    QVERIFY(hiddenEditor->image != hiddenPrint->image);
    window.findChild<QCheckBox*>(u"layerVisible"_s)->click();
    window.findChild<QCheckBox*>(u"layerPrintable"_s)->click();
    const auto excludedPrint = renderPreview(window.document()); QVERIFY(excludedPrint);
    const auto visibleEditor = renderEditorPreview(window.document()); QVERIFY(visibleEditor);
    QCOMPARE(visibleEditor->image, originalPrint->image);
    QVERIFY(excludedPrint->image != visibleEditor->image);
    window.setDocument({});
}

void DesignerExtensionsTests::layerOrderKeepsSelectionAndUndo() {
    MainWindow window;
    DesignDocument document;
    for (int i = 0; i < 4; ++i) { auto box = makeElement(ElementType::Box, {20 + i * 30, 20}); box.name = QString::number(i); document.elements.append(box); }
    const auto first = document.elements[0].id, second = document.elements[1].id;
    window.setDocument(document);
    auto* canvas = window.findChild<LabelCanvas*>(); canvas->setSelectedIndices({0, 1});
    window.findChild<QPushButton*>(u"layerBringFront"_s)->click();
    QCOMPARE(window.document().elements[2].id, first); QCOMPARE(window.document().elements[3].id, second);
    QCOMPARE(canvas->selectedIndices().size(), 2);
    window.findChild<QAction*>(u"undoAction"_s)->trigger();
    QCOMPARE(window.document().elements[0].id, first); QCOMPARE(window.document().elements[1].id, second);
    window.setDocument({});
}

void DesignerExtensionsTests::propertyUnitsDoNotChangeZpl() {
    MainWindow window;
    DesignDocument document; document.dpi = 300; document.elements = {makeElement(ElementType::Box)};
    window.setDocument(document); window.findChild<LabelCanvas*>()->setSelectedIndex(0);
    const auto original = toZpl(window.document());
    auto* units = window.findChild<QComboBox*>(u"propertyUnits"_s); QVERIFY(units);
    units->setCurrentIndex(units->findData(int(CanvasUnit::Dots)));
    units->setCurrentIndex(units->findData(int(CanvasUnit::Millimeters)));
    QCOMPARE(toZpl(window.document()), original);
    auto* width = window.findChild<QSpinBox*>(u"elementWidth"_s);
    QCOMPARE(width->value(), 240); QVERIFY(width->text().contains(u"мм"));
    window.setDocument({});
}

void DesignerExtensionsTests::tableDialogKeepsCellsAndDimensions() {
    auto table = makeElement(ElementType::Table); table.tableCells[0] = u"{{name}}"_s;
    TableDialog dialog(table, CanvasUnit::Dots, 203);
    auto* rows = dialog.findChild<QSpinBox*>(u"tableRows"_s);
    auto* cells = dialog.findChild<QTableWidget*>(u"tableCells"_s);
    rows->setValue(3); QCOMPARE(cells->rowCount(), 3); QCOMPARE(cells->item(0, 0)->text(), u"{{name}}"_s);
    cells->setCurrentCell(2, 1); cells->item(2, 1)->setText(u"Last"_s);
    dialog.findChild<QSpinBox*>(u"tableRowHeight"_s)->setValue(80);
    const auto edited = dialog.editedElement();
    QCOMPARE(edited.tableCells[5], u"Last"_s); QCOMPARE(edited.tableRowHeights[2], 80);
    QCOMPARE(edited.height, edited.tableRowHeights[0] + edited.tableRowHeights[1] + 80);
    QCOMPARE(table.tableCells.size(), 4);
}

void DesignerExtensionsTests::qrFormsEscapeAndValidate() {
    const auto wifi = makeQrPayload(QrPayloadType::Wifi,
        {{u"ssid"_s, u"A;B"_s}, {u"password"_s, u"x:y\\z"_s}, {u"security"_s, u"WPA"_s}});
    QVERIFY(wifi); QCOMPARE(*wifi, u"WIFI:T:WPA;S:A\\;B;P:x\\:y\\\\z;;"_s);
    const auto vcard = makeQrPayload(QrPayloadType::Contact,
        {{u"name"_s, u"A;B"_s}, {u"company"_s, u"One\nTwo"_s}});
    QVERIFY(vcard); QVERIFY(vcard->contains(u"FN:A\\;B")); QVERIFY(vcard->contains(u"ORG:One\\nTwo"));
    QVERIFY(!makeQrPayload(QrPayloadType::Url, {{u"url"_s, u"javascript:alert(1)"_s}}));
    const auto url = makeQrPayload(QrPayloadType::Url, {{u"url"_s, u"https://example.com/hello"_s}});
    QVERIFY(url); QCOMPARE(*url, u"https://example.com/hello"_s);
    const auto raw = makeQrPayload(QrPayloadType::Text, {{u"text"_s, u"{{serial}}"_s}});
    QVERIFY(raw); QCOMPARE(*raw, u"{{serial}}"_s);
}

void DesignerExtensionsTests::projectWindowPreservesUnappliedSource() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    MainWindow window; window.setDocument({});
    auto* code = window.findChild<QPlainTextEdit*>(u"zplEditor"_s);
    const auto pending = u"^XA^FO10,10^FDunfinished"_s;
    code->setPlainText(pending);
    const auto path = directory.filePath(u"unfinished.qtzpl"_s);
    QVERIFY(window.saveFile(path));
    QVERIFY(!window.findChild<DocumentSession*>()->modified());
    window.setDocument(sampleDocument());
    QVERIFY(window.openFile(path));
    QCOMPARE(code->toPlainText(), pending);
    QVERIFY(window.findChild<DocumentSession*>()->hasPendingSource());
    QVERIFY(!window.findChild<DocumentSession*>()->modified());
    window.setDocument({});
}

void DesignerExtensionsTests::hiddenObjectsAreSkippedWhenSelectingBehind() {
    MainWindow window; window.show();
    DesignDocument document; document.width = 300; document.height = 200;
    for (int i = 0; i < 3; ++i) { auto box = makeElement(ElementType::Box, {50, 50}); box.width = 100; box.height = 70; document.elements.append(box); }
    document.elements[1].editorVisible = false;
    window.setDocument(document); window.refreshPreview();
    auto* canvas = window.findChild<LabelCanvas*>(); canvas->actualSize();
    QApplication::processEvents();
    const auto point = canvas->mapFromScene(QPointF(80, 80));
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, point); QCOMPARE(canvas->selectedIndex(), 2);
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier, point); QCOMPARE(canvas->selectedIndex(), 0);
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier, point); QCOMPARE(canvas->selectedIndex(), 2);
    window.setDocument({});
}

void DesignerExtensionsTests::extendedPaletteAndBarcodeSettings() {
    MainWindow window; window.show();
    const auto* palette = window.findChild<QListWidget*>(u"elementPalette"_s); QVERIFY(palette);
    QCOMPARE(palette->count(), 12); // Pointer, nine drawing elements (including EAC), and two barcode families.
    QCOMPARE(palette->item(0)->data(Qt::UserRole).toInt(), -1);
    QCOMPARE(palette->item(0)->text(), u"Выбор объектов"_s);
    DesignDocument document; auto matrix = makeElement(ElementType::DataMatrix);
    matrix.gs1 = false; matrix.text = u"ABC123"_s; document.elements = {matrix};
    window.setDocument(document); window.findChild<LabelCanvas*>()->setSelectedIndex(0);
    bool controlsFound = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* rectangular = dialog->findChild<QCheckBox*>(u"dataMatrixRectangular"_s);
        auto* rows = dialog->findChild<QSpinBox*>(u"dataMatrixRows"_s);
        auto* columns = dialog->findChild<QSpinBox*>(u"dataMatrixColumns"_s);
        controlsFound = rectangular && rows && columns;
        if (controlsFound) { rectangular->setChecked(true); rows->setValue(8); columns->setValue(32); dialog->accept(); }
        else dialog->reject();
    });
    window.findChild<QPushButton*>(u"barcodeOptions"_s)->click();
    QVERIFY(controlsFound); QVERIFY(window.document().elements.first().dataMatrixRectangular);
    QCOMPARE(window.document().elements.first().dataMatrixRows, 8);
    const auto reopened = fromZpl(toZpl(window.document())); QVERIFY(reopened); QVERIFY(reopened->visualEditable);
    QCOMPARE(reopened->elements.first().dataMatrixColumns, 32);
    window.findChild<QAction*>(u"undoAction"_s)->trigger();
    QVERIFY(!window.document().elements.first().dataMatrixRectangular);
    window.setDocument({});
}

void DesignerExtensionsTests::whiteGraphicPropertiesRoundTrip() {
    MainWindow window; DesignDocument document; document.elements = {makeElement(ElementType::Diagonal)};
    window.setDocument(document); window.findChild<LabelCanvas*>()->setSelectedIndex(0);
    window.findChild<QCheckBox*>(u"whiteGraphic"_s)->click();
    auto* direction = window.findChild<QComboBox*>(u"diagonalDirection"_s);
    direction->setCurrentIndex(direction->findData(u"L"_s));
    QVERIFY(window.document().elements.first().graphicWhite);
    const auto reopened = fromZpl(toZpl(window.document())); QVERIFY(reopened); QVERIFY(reopened->visualEditable);
    QCOMPARE(reopened->elements.first().diagonalDirection, QChar(u'L'));
    QVERIFY(reopened->elements.first().graphicWhite);
    window.setDocument({});
}

void DesignerExtensionsTests::dataMatrixDefaultsToGs1ForSamplePaletteAndDrop() {
    MainWindow window; window.show();
    int matrices = 0;
    for (const auto& element : window.document().elements)
        if (element.type == ElementType::DataMatrix) { ++matrices; QVERIFY(element.gs1); }
    QVERIFY(matrices > 0);
    window.setDocument({});
    auto* palette = window.findChild<QListWidget*>(u"elementPalette"_s);
    QListWidgetItem* matrixItem = nullptr;
    for (int i = 0; i < palette->count(); ++i)
        if (palette->item(i)->data(Qt::UserRole).toInt() == int(ElementType::DataMatrix)) matrixItem = palette->item(i);
    QVERIFY(matrixItem);
    palette->itemDoubleClicked(matrixItem);
    QCOMPARE(window.document().elements.size(), 1); QVERIFY(window.document().elements.first().gs1);
    auto* canvas = window.findChild<LabelCanvas*>(); canvas->actualSize();
    QApplication::processEvents();
    QMimeData mime; mime.setData("application/x-qtzpl-element", QByteArray::number(int(ElementType::DataMatrix)));
    const QPoint position = canvas->mapFromScene(QPointF(110, 120));
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &enter); QVERIFY(enter.isAccepted());
    QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &drop); QVERIFY(drop.isAccepted());
    QCOMPARE(window.document().elements.size(), 2); QVERIFY(window.document().elements.last().gs1);
    QVERIFY(renderPreview(window.document()));
    window.setDocument({});
}
QTEST_MAIN(DesignerExtensionsTests)
#include "test_designer_extensions.moc"
