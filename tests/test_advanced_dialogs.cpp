#include "advanced_text_dialog.hpp"
#include "label_settings_dialog.hpp"
#include "main_window.hpp"
#include "document_session.hpp"
#include "label_canvas.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
void select(QDialog& dialog, const QString& name, const QVariant& value)
{
    auto* combo = dialog.findChild<QComboBox*>(name);
    QVERIFY(combo);
    const auto index = combo->findData(value);
    QVERIFY(index >= 0);
    combo->setCurrentIndex(index);
}

void finish(QDialog& dialog, QDialogButtonBox::StandardButton button)
{
    auto* box = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(box);
    box->button(button)->click();
}

DesignDocument textDocument()
{
    DesignDocument document;
    document.width = 400; document.height = 300;
    auto text = makeElement(ElementType::Text, {150, 160});
    text.text = u"ABCD"_s;
    document.elements = {text};
    return document;
}
} // namespace

class AdvancedDialogTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void textCancelLeavesElementUntouched();
    void textAcceptUsesOriginDirectionSpacingAndInheritance();
    void labelCancelAndAcceptAreIsolated();
    void mainWindowAppliesDialogsAndUndoesWholeChanges();
    void inheritedRotationResizesEffectiveFontAxes();
};

void AdvancedDialogTests::textCancelLeavesElementUntouched()
{
    const auto element = textDocument().elements.front();
    AdvancedTextDialog dialog(element, CanvasUnit::Millimeters, 300);
    select(dialog, u"textOrigin"_s, true);
    select(dialog, u"textAnchor"_s, 1);
    select(dialog, u"textDirection"_s, u"R"_s);
    dialog.findChild<QSpinBox*>(u"characterSpacing"_s)->setValue(15);
    dialog.findChild<QCheckBox*>(u"textInheritOrientation"_s)->setChecked(true);
    finish(dialog, QDialogButtonBox::Cancel);
    QCOMPARE(dialog.result(), QDialog::Rejected);
    const auto after = dialog.editedElement();
    QCOMPARE(elementZpl(after), elementZpl(element));
    QCOMPARE(after.x, element.x); QCOMPARE(after.y, element.y);
}

void AdvancedDialogTests::textAcceptUsesOriginDirectionSpacingAndInheritance()
{
    const auto element = textDocument().elements.front();
    AdvancedTextDialog dialog(element, CanvasUnit::Millimeters, 300);
    select(dialog, u"textOrigin"_s, true);
    select(dialog, u"textAnchor"_s, 1);
    select(dialog, u"textOrientation"_s, u"B"_s);
    auto* inherit = dialog.findChild<QCheckBox*>(u"textInheritOrientation"_s);
    inherit->setChecked(true);
    QVERIFY(!dialog.findChild<QComboBox*>(u"textOrientation"_s)->isEnabled());
    auto* spacing = dialog.findChild<QSpinBox*>(u"characterSpacing"_s);
    spacing->setValue(4);
    select(dialog, u"textDirection"_s, u"V"_s);
    QVERIFY(!spacing->isEnabled());
    select(dialog, u"textDirection"_s, u"R"_s);
    QVERIFY(spacing->isEnabled());
    finish(dialog, QDialogButtonBox::Ok);
    QCOMPARE(dialog.result(), QDialog::Accepted);
    const auto after = dialog.editedElement();
    QVERIFY(after.baselineOrigin); QCOMPARE(after.originJustification, 1);
    QVERIFY(after.useDefaultOrientation); QCOMPARE(after.orientation, u'B');
    QCOMPARE(after.textDirection, u'R'); QCOMPARE(after.characterSpacing, 4);
    QCOMPARE(after.x, element.x); QCOMPARE(after.y, element.y);
    const auto zpl = elementZpl(after);
    QVERIFY(zpl.contains(u"^FT150,160,1"));
    QVERIFY(zpl.contains(u"^FPR,4"));
}

void AdvancedDialogTests::labelCancelAndAcceptAreIsolated()
{
    const auto document = textDocument();
    LabelSettingsDialog rejected(document);
    rejected.findChild<QCheckBox*>(u"labelMirror"_s)->setChecked(true);
    select(rejected, u"labelDefaultOrientation"_s, u"R"_s);
    finish(rejected, QDialogButtonBox::Cancel);
    QCOMPARE(toZpl(rejected.editedDocument()), toZpl(document));

    LabelSettingsDialog accepted(document);
    for (const auto& name : {u"labelMirror"_s, u"labelInverted"_s, u"labelReverse"_s})
        accepted.findChild<QCheckBox*>(name)->setChecked(true);
    select(accepted, u"labelDefaultOrientation"_s, u"B"_s);
    select(accepted, u"labelDefaultJustification"_s, 1);
    finish(accepted, QDialogButtonBox::Ok);
    QCOMPARE(accepted.result(), QDialog::Accepted);
    const auto after = accepted.editedDocument();
    QVERIFY(after.printMirror && after.printInverted && after.labelReverse);
    QCOMPARE(after.defaultOrientation, u'B'); QCOMPARE(after.defaultJustification, 1);
    QCOMPARE(after.elements.front().x, 150); QCOMPARE(after.elements.front().y, 160);
    QCOMPARE(after.width, document.width); QCOMPARE(after.dpi, document.dpi);
    const auto zpl = toZpl(after);
    QVERIFY(zpl.contains(u"^PMY")); QVERIFY(zpl.contains(u"^POI")); QVERIFY(zpl.contains(u"^LRY"));
    QVERIFY(zpl.contains(u"^FWB,1"));
}

void AdvancedDialogTests::mainWindowAppliesDialogsAndUndoesWholeChanges()
{
    MainWindow window; window.setDocument(textDocument());
    auto* canvas = window.findChild<LabelCanvas*>(); canvas->setSelectedIndex(0);
    auto* session = window.findChild<DocumentSession*>(); QVERIFY(session);
    const auto original = toZpl(window.document());
    bool visited = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<AdvancedTextDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        visited = true;
        select(*dialog, u"textOrigin"_s, true);
        select(*dialog, u"textDirection"_s, u"R"_s);
        dialog->findChild<QSpinBox*>(u"characterSpacing"_s)->setValue(3);
        finish(*dialog, QDialogButtonBox::Ok);
    });
    window.findChild<QPushButton*>(u"advancedText"_s)->click();
    QVERIFY(visited); QVERIFY(window.document().elements[0].baselineOrigin);
    QCOMPARE(window.document().elements[0].textDirection, u'R');
    QCOMPARE(session->undoStack()->count(), 1);
    session->undoStack()->undo(); QCOMPARE(toZpl(window.document()), original);

    visited = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<LabelSettingsDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        visited = true;
        dialog->findChild<QCheckBox*>(u"labelMirror"_s)->setChecked(true);
        dialog->findChild<QCheckBox*>(u"labelInverted"_s)->setChecked(true);
        finish(*dialog, QDialogButtonBox::Ok);
    });
    window.findChild<QAction*>(u"labelSettings"_s)->trigger();
    QVERIFY(visited); QVERIFY(window.document().printMirror && window.document().printInverted);
    QCOMPARE(window.document().elements[0].x, 150);
    QCOMPARE(session->undoStack()->count(), 1);
    session->undoStack()->undo(); QCOMPARE(toZpl(window.document()), original);
    window.setDocument({});
}

void AdvancedDialogTests::inheritedRotationResizesEffectiveFontAxes()
{
    MainWindow window;
    auto document = textDocument();
    document.defaultOrientation = u'R';
    auto& text = document.elements[0];
    text.useDefaultOrientation = true; text.orientation = u'N';
    text.fontHeight = 30; text.fontWidth = 20;
    window.setDocument(document); window.refreshPreview();
    window.findChild<LabelCanvas*>()->setSelectedIndex(0);
    auto* width = window.findChild<QSpinBox*>(u"elementWidth"_s);
    QVERIFY(width); const int originalHeight = text.fontHeight, originalWidth = text.fontWidth;
    width->setValue(width->value() * 2);
    QVERIFY(window.document().elements[0].fontHeight > originalHeight);
    QCOMPARE(window.document().elements[0].fontWidth, originalWidth);
    QCOMPARE(window.document().elements[0].orientation, u'N');
    QVERIFY(window.document().elements[0].useDefaultOrientation);
    window.setDocument({});
}

QTEST_MAIN(AdvancedDialogTests)
#include "test_advanced_dialogs.moc"
