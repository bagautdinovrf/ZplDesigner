#include "gs1_dialog.hpp"
#include "gs1_data.hpp"
#include "placeholder_values_dialog.hpp"
#include "data_matrix_values.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>

using namespace Qt::StringLiterals;
using Designer::Gs1Dialog;

class Gs1DialogTests final : public QObject {
    Q_OBJECT

private slots:
    void scannerInputBecomesEditableFields();
    void editingAndReorderingRegeneratesSeparators();
    void invalidSourceIsPreservedAndCannotBeAccepted();
    void invalidTableValueCannotBeAccepted();
    void literalParenthesesSurviveFieldEditing();
    void addAndRemoveFields();
    void acceptsAiTemplatesAndPreservesBothTokenForms();
    void acceptsWholeCodePlaceholderWithoutInventingFields();
    void templatesStillRejectMalformedAndInvalidLiteralValues();
    void insertsNamedVariableIntoSelectedField();
    void valuesDialogRequiresAndValidatesConcreteValues();
    void valuesDialogSharesNamesAndPreservesRememberedValues();
    void valuesDialogChecksWholeCodeAndMalformedTemplates();
    void insertsMarkerAiAndAdditionalPlaceholders();
    void valuesDialogSeparatesMarkerDatesAndAdditionalDates();
    void builtinSeparatorDoesNotRequireValue();
};

void Gs1DialogTests::scannerInputBecomesEditableFields()
{
    const QString raw = u"]d2010950600013435210LOT-7"_s + QChar(0x1d) + u"21SERIAL-3"_s;
    Gs1Dialog dialog(raw);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* source = dialog.findChild<QPlainTextEdit*>(u"gs1Source"_s);
    auto* encoded = dialog.findChild<QPlainTextEdit*>(u"gs1Encoded"_s);
    QVERIFY(table);
    QVERIFY(source);
    QVERIFY(encoded);
    QCOMPARE(source->toPlainText(), raw);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->item(1, 0)->text(), u"10"_s);
    QCOMPARE(table->item(1, 1)->text(), u"LOT-7"_s);
    QCOMPARE(dialog.data(), u"(01)09506000134352(10)LOT-7(21)SERIAL-3"_s);
    QCOMPARE(encoded->toPlainText(), u"010950600013435210LOT-7⟨GS⟩21SERIAL-3"_s);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.accept();
    QCOMPARE(accepted.size(), 1);
}

void Gs1DialogTests::editingAndReorderingRegeneratesSeparators()
{
    Gs1Dialog dialog(u"(01)09506000134352(10)LOT-7(21)SERIAL-3"_s);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* up = dialog.findChild<QPushButton*>(u"gs1MoveUp"_s);
    auto* down = dialog.findChild<QPushButton*>(u"gs1MoveDown"_s);
    QVERIFY(table);
    QVERIFY(up);
    QVERIFY(down);
    table->item(1, 1)->setText(u"BATCH-2"_s);
    QCOMPARE(dialog.data(), u"(01)09506000134352(10)BATCH-2(21)SERIAL-3"_s);
    table->setCurrentCell(2, 1);
    QVERIFY(up->isEnabled());
    QVERIFY(!down->isEnabled());
    up->click();
    QCOMPARE(table->currentRow(), 1);
    QCOMPARE(dialog.data(), u"(01)09506000134352(21)SERIAL-3(10)BATCH-2"_s);
    const auto parsed = Designer::parseGs1(dialog.data());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->elementString,
        u"010950600013435221SERIAL-3"_s + QChar(0x1d) + u"10BATCH-2"_s);
}

void Gs1DialogTests::invalidSourceIsPreservedAndCannotBeAccepted()
{
    const QString invalid = u"unrecognised original data <keep>"_s;
    Gs1Dialog dialog(invalid);
    auto* source = dialog.findChild<QPlainTextEdit*>(u"gs1Source"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"gs1Apply"_s);
    auto* status = dialog.findChild<QLabel*>(u"gs1Status"_s);
    QVERIFY(source);
    QVERIFY(apply);
    QVERIFY(status);
    QCOMPARE(source->toPlainText(), invalid);
    QVERIFY(dialog.data().isEmpty());
    QVERIFY(!apply->isEnabled());
    QCOMPARE(status->textFormat(), Qt::PlainText);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.accept();
    QVERIFY(accepted.isEmpty());
    source->setPlainText(u"(01)09506000134352(17)271231"_s);
    QVERIFY(apply->isEnabled());
    source->setPlainText(u"(01)09506000134352(17)271332"_s);
    QVERIFY(!apply->isEnabled());
    dialog.accept();
    QVERIFY(accepted.isEmpty());
}

void Gs1DialogTests::invalidTableValueCannotBeAccepted()
{
    Gs1Dialog dialog(u"(01)09506000134352(21)SERIAL-3"_s);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"gs1Apply"_s);
    auto* encoded = dialog.findChild<QPlainTextEdit*>(u"gs1Encoded"_s);
    QVERIFY(table);
    QVERIFY(apply);
    QVERIFY(encoded);
    table->item(0, 1)->setText(u"09506000134353"_s); // Incorrect check digit.
    QVERIFY(!apply->isEnabled());
    QVERIFY(dialog.data().isEmpty());
    QVERIFY(encoded->toPlainText().isEmpty());
    QCOMPARE(table->item(1, 1)->text(), u"SERIAL-3"_s);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.accept();
    QVERIFY(accepted.isEmpty());
    table->item(0, 1)->setText(u"09506000134352"_s);
    QVERIFY(apply->isEnabled());
    dialog.accept();
    QCOMPARE(accepted.size(), 1);
}

void Gs1DialogTests::literalParenthesesSurviveFieldEditing()
{
    Gs1Dialog dialog(u"(01)09506000134352(21)SERIAL-3"_s);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* source = dialog.findChild<QPlainTextEdit*>(u"gs1Source"_s);
    QVERIFY(table);
    QVERIFY(source);
    table->item(1, 1)->setText(u"SERIAL(10)"_s);
    const auto parsed = Designer::parseGs1(dialog.data());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->fields.size(), 2);
    QCOMPARE(parsed->fields[1].ai, u"21"_s);
    QCOMPARE(parsed->fields[1].value, u"SERIAL(10)"_s);
    QCOMPARE(source->toPlainText(), dialog.data());
}

void Gs1DialogTests::addAndRemoveFields()
{
    Gs1Dialog dialog(u"(01)09506000134352"_s);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* identifiers = dialog.findChild<QComboBox*>(u"gs1Identifiers"_s);
    auto* add = dialog.findChild<QPushButton*>(u"gs1AddField"_s);
    auto* remove = dialog.findChild<QPushButton*>(u"gs1RemoveField"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"gs1Apply"_s);
    QVERIFY(table);
    QVERIFY(identifiers);
    QVERIFY(add);
    QVERIFY(remove);
    QVERIFY(apply);
    const int serial = identifiers->findData(u"21"_s);
    QVERIFY(serial >= 0);
    identifiers->setCurrentIndex(serial);
    add->click();
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->item(1, 0)->text(), u"21"_s);
    QVERIFY(!apply->isEnabled());
    table->item(1, 1)->setText(u"SERIAL-4"_s);
    QVERIFY(apply->isEnabled());
    QCOMPARE(dialog.data(), u"(01)09506000134352(21)SERIAL-4"_s);
    table->setCurrentCell(1, 1);
    remove->click();
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(dialog.data(), u"(01)09506000134352"_s);
}

void Gs1DialogTests::acceptsAiTemplatesAndPreservesBothTokenForms()
{
    const QString text = u"(01){{gtin}}(10)LOT-{{batch}}(21)[[serial]]"_s;
    Gs1Dialog dialog(text);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* status = dialog.findChild<QLabel*>(u"gs1Status"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"gs1Apply"_s);
    auto* encoded = dialog.findChild<QPlainTextEdit*>(u"gs1Encoded"_s);
    QVERIFY(table);
    QVERIFY(status);
    QVERIFY(apply);
    QVERIFY(encoded);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->item(0, 1)->text(), u"{{gtin}}"_s);
    QCOMPARE(table->item(2, 1)->text(), u"[[serial]]"_s);
    QCOMPARE(dialog.data(), text);
    QVERIFY(apply->isEnabled());
    QVERIFY(status->text().contains(u"после подстановки"_s));
    QVERIFY(encoded->toPlainText().contains(u"{{gtin}}"_s));
    QVERIFY(encoded->toPlainText().contains(u"[[serial]]"_s));
    table->item(1, 1)->setText(u"BATCH-[[batch]]"_s);
    QCOMPARE(dialog.data(), u"(01){{gtin}}(10)BATCH-[[batch]](21)[[serial]]"_s);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.accept();
    QCOMPARE(accepted.size(), 1);
}

void Gs1DialogTests::acceptsWholeCodePlaceholderWithoutInventingFields()
{
    Gs1Dialog dialog(u"(01)09506000134352(21)SERIAL-3"_s);
    auto* source = dialog.findChild<QPlainTextEdit*>(u"gs1Source"_s);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* status = dialog.findChild<QLabel*>(u"gs1Status"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"gs1Apply"_s);
    QVERIFY(source);
    QVERIFY(table);
    QVERIFY(status);
    QVERIFY(apply);
    source->setPlainText(u"[[marking_code]]"_s);
    QCOMPARE(dialog.data(), u"[[marking_code]]"_s);
    QCOMPARE(source->toPlainText(), u"[[marking_code]]"_s);
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(apply->isEnabled());
    QVERIFY(status->text().contains(u"целого GS1-кода"_s));
    QVERIFY(status->text().contains(u"после подстановки"_s));
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.accept();
    QCOMPARE(accepted.size(), 1);
}

void Gs1DialogTests::templatesStillRejectMalformedAndInvalidLiteralValues()
{
    Gs1Dialog dialog(u"(01){{gtin}}(21)[[serial]]"_s);
    auto* source = dialog.findChild<QPlainTextEdit*>(u"gs1Source"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"gs1Apply"_s);
    QVERIFY(source);
    QVERIFY(apply);
    QVERIFY(apply->isEnabled());
    source->setPlainText(u"(01)09506000134353(21)[[serial]]"_s);
    QVERIFY(!apply->isEnabled());
    QVERIFY(dialog.data().isEmpty());
    source->setPlainText(u"(01){{gtin}}(21)[[serial]"_s);
    QVERIFY(!apply->isEnabled());
    source->setPlainText(u"(01){{gtin}}(999)[[serial]]"_s);
    QVERIFY(!apply->isEnabled());
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.accept();
    QVERIFY(accepted.isEmpty());
}

void Gs1DialogTests::insertsNamedVariableIntoSelectedField()
{
    Gs1Dialog dialog(u"(01)09506000134352(21)SERIAL-3"_s);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* name = dialog.findChild<QLineEdit*>(u"gs1VariableName"_s);
    auto* insert = dialog.findChild<QPushButton*>(u"gs1InsertVariable"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"gs1Apply"_s);
    auto* syntax = dialog.findChild<QComboBox*>(u"gs1VariableSyntax"_s);
    QVERIFY(table);
    QVERIFY(name);
    QVERIFY(insert);
    QVERIFY(apply);
    QVERIFY(syntax);
    syntax->setCurrentIndex(syntax->findData(u"named"_s));
    table->setCurrentCell(0, 1);
    QVERIFY(!insert->isEnabled());
    name->setText(u"gtin"_s);
    QVERIFY(insert->isEnabled());
    insert->click();
    QCOMPARE(table->item(0, 1)->text(), u"{{gtin}}"_s);
    QCOMPARE(table->item(1, 1)->text(), u"SERIAL-3"_s);
    QCOMPARE(dialog.data(), u"(01){{gtin}}(21)SERIAL-3"_s);
    QVERIFY(apply->isEnabled());
    name->setText(u"invalid name"_s);
    QVERIFY(!insert->isEnabled());
    name->setText(u"order.serial"_s);
    table->setCurrentCell(1, 1);
    insert->click();
    QCOMPARE(dialog.data(), u"(01){{gtin}}(21){{order.serial}}"_s);
}

void Gs1DialogTests::valuesDialogRequiresAndValidatesConcreteValues()
{
    Designer::DesignDocument document;
    auto element = Designer::makeElement(Designer::ElementType::DataMatrix);
    element.gs1 = true;
    element.text = u"(01){{gtin}}(21)[[serial]]"_s;
    document.elements = {element};
    Designer::PlaceholderValuesDialog dialog(document);
    auto* table = dialog.findChild<QTableWidget*>(u"placeholderValuesTable"_s);
    auto* status = dialog.findChild<QLabel*>(u"placeholderValuesStatus"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"placeholderValuesApply"_s);
    auto* gtin = dialog.findChild<QLineEdit*>(u"placeholderValue_gtin"_s);
    auto* serial = dialog.findChild<QLineEdit*>(u"placeholderValue_serial"_s);
    QVERIFY(table);
    QVERIFY(status);
    QVERIFY(apply);
    QVERIFY(gtin);
    QVERIFY(serial);
    QCOMPARE(table->rowCount(), 2);
    QVERIFY(!apply->isEnabled());
    QVERIFY(!dialog.values().contains(u"gtin"_s));
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.accept();
    QVERIFY(accepted.isEmpty());
    gtin->setText(u"09506000134352"_s);
    QVERIFY(!apply->isEnabled());
    serial->setText(u"SERIAL(10)"_s);
    QVERIFY(apply->isEnabled());
    gtin->setText(u"09506000134353"_s);
    QVERIFY(!apply->isEnabled());
    QVERIFY(status->text().contains(u"контрольная"_s));
    dialog.accept();
    QVERIFY(accepted.isEmpty());
    gtin->setText(u"09506000134352"_s);
    QVERIFY(apply->isEnabled());
    auto resolvedDocument = document;
    resolvedDocument.previewValues = dialog.values();
    const auto resolved = Designer::resolveDataMatrixValues(resolvedDocument);
    QVERIFY(resolved.has_value());
    const auto gs1 = Designer::parseGs1(resolved->elements.first().text);
    QVERIFY(gs1.has_value());
    QCOMPARE(gs1->fields.size(), 2);
    QCOMPARE(gs1->fields[1].value, u"SERIAL(10)"_s);
    QCOMPARE(document.elements.first().text, element.text);
    QVERIFY(document.previewValues.isEmpty());
    dialog.accept();
    QCOMPARE(accepted.size(), 1);
}

void Gs1DialogTests::valuesDialogSharesNamesAndPreservesRememberedValues()
{
    Designer::DesignDocument document;
    auto element = Designer::makeElement(Designer::ElementType::DataMatrix);
    element.gs1 = true;
    element.text = u"(01){{gtin}}(21)[[serial]]"_s;
    auto second = element;
    second.id = QUuid::createUuid();
    second.text = u"(01)[[gtin]](21){{serial}}"_s;
    document.elements = {element, second};
    document.previewValues = {{u"gtin"_s, u"09506000134352"_s},
        {u"serial"_s, u"SERIAL-1"_s}, {u"temporarilyAbsent"_s, u"KEEP"_s}};
    Designer::PlaceholderValuesDialog dialog(document);
    auto* table = dialog.findChild<QTableWidget*>(u"placeholderValuesTable"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"placeholderValuesApply"_s);
    auto* serial = dialog.findChild<QLineEdit*>(u"placeholderValue_serial"_s);
    QVERIFY(table);
    QVERIFY(apply);
    QVERIFY(serial);
    QCOMPARE(table->rowCount(), 2);
    QVERIFY(!table->item(0, 0)->flags().testFlag(Qt::ItemIsEditable));
    QCOMPARE(serial->text(), u"SERIAL-1"_s);
    QVERIFY(apply->isEnabled());
    serial->setText(u"SERIAL-2"_s);
    QCOMPARE(dialog.values().value(u"serial"_s), u"SERIAL-2"_s);
    QCOMPARE(dialog.values().value(u"temporarilyAbsent"_s), u"KEEP"_s);
    QCOMPARE(document.previewValues.value(u"serial"_s), u"SERIAL-1"_s);
    serial->clear();
    QVERIFY(!apply->isEnabled());
    QVERIFY(!dialog.values().contains(u"serial"_s));
    QCOMPARE(dialog.values().value(u"temporarilyAbsent"_s), u"KEEP"_s);
}

void Gs1DialogTests::valuesDialogChecksWholeCodeAndMalformedTemplates()
{
    Designer::DesignDocument document;
    auto element = Designer::makeElement(Designer::ElementType::DataMatrix);
    element.gs1 = true;
    element.text = u"{{code}}"_s;
    document.elements = {element};
    Designer::PlaceholderValuesDialog dialog(document);
    auto* code = dialog.findChild<QLineEdit*>(u"placeholderValue_code"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"placeholderValuesApply"_s);
    QVERIFY(code);
    QVERIFY(apply);
    QVERIFY(!apply->isEnabled());
    code->setText(u"[[unexpanded]]"_s);
    QVERIFY(!apply->isEnabled());
    code->setText(u"(01)09506000134352(21)SERIAL-1"_s);
    QVERIFY(apply->isEnabled());
    document.elements.first().text = u"(01){{gtin}"_s;
    Designer::PlaceholderValuesDialog malformed(document);
    auto* malformedApply = malformed.findChild<QPushButton*>(u"placeholderValuesApply"_s);
    QVERIFY(malformedApply);
    QVERIFY(!malformedApply->isEnabled());
    QSignalSpy accepted(&malformed, &QDialog::accepted);
    malformed.accept();
    QVERIFY(accepted.isEmpty());
}

void Gs1DialogTests::insertsMarkerAiAndAdditionalPlaceholders()
{
    Gs1Dialog dialog(u"(01)09506000134352(21)SERIAL-3(240)PRODUCT"_s);
    auto* table = dialog.findChild<QTableWidget*>(u"gs1Fields"_s);
    auto* insertAi = dialog.findChild<QPushButton*>(u"gs1InsertAiVariable"_s);
    auto* name = dialog.findChild<QLineEdit*>(u"gs1VariableName"_s);
    auto* insert = dialog.findChild<QPushButton*>(u"gs1InsertVariable"_s);
    auto* syntax = dialog.findChild<QComboBox*>(u"gs1VariableSyntax"_s);
    QVERIFY(table);
    QVERIFY(insertAi);
    QVERIFY(name);
    QVERIFY(insert);
    QVERIFY(syntax);
    table->setCurrentCell(0, 1);
    QVERIFY(insertAi->isEnabled());
    QVERIFY(insertAi->text().contains(u"{{01}}"_s));
    insertAi->click();
    QCOMPARE(table->item(0, 1)->text(), u"{{01}}"_s);
    table->setCurrentCell(1, 1);
    insertAi->click();
    QCOMPARE(table->item(1, 1)->text(), u"{{21}}"_s);
    table->setCurrentCell(2, 1);
    QVERIFY(!insertAi->isEnabled());
    QCOMPARE(syntax->currentData().toString(), u"additional"_s);
    name->setText(u"PRODUCT_NAME"_s);
    insert->click();
    QCOMPARE(table->item(2, 1)->text(), u"[[product_name]]"_s);
    QVERIFY(!dialog.data().isEmpty());
    name->setText(u"order.serial"_s);
    QVERIFY(!insert->isEnabled());
    name->setText(u"11"_s);
    QVERIFY(insert->isEnabled());
    insert->click();
    QCOMPARE(table->item(2, 1)->text(), u"[[11]]"_s);
}

void Gs1DialogTests::valuesDialogSeparatesMarkerDatesAndAdditionalDates()
{
    Designer::DesignDocument document;
    auto element = Designer::makeElement(Designer::ElementType::DataMatrix);
    element.gs1 = false;
    element.text = u"{{01}}|{{11}}|{{17}}|[[11]]|[[17]]|[[BATCH]]|[[batch]]|{{gS}}"_s;
    document.elements = {element};
    document.previewValues = {{u"01"_s, u"09506000134352"_s},
        {u"11"_s, u"261007"_s}, {u"17"_s, u"271007"_s},
        {u"[[11]]"_s, u"07.10.2026"_s}, {u"[[17]]"_s, u"2027/10/07"_s},
        {u"batch"_s, u"BATCH-1"_s}};
    Designer::PlaceholderValuesDialog dialog(document);
    auto* table = dialog.findChild<QTableWidget*>(u"placeholderValuesTable"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"placeholderValuesApply"_s);
    auto* numericDate = dialog.findChild<QLineEdit*>(u"placeholderValue_11"_s);
    auto* additionalDate = dialog.findChild<QLineEdit*>(u"placeholderValue_[[11]]"_s);
    auto* batch = dialog.findChild<QLineEdit*>(u"placeholderValue_batch"_s);
    QVERIFY(table);
    QVERIFY(apply);
    QVERIFY(numericDate);
    QVERIFY(additionalDate);
    QVERIFY(batch);
    QCOMPARE(table->rowCount(), 6);
    QCOMPARE(numericDate->text(), u"261007"_s);
    QCOMPARE(additionalDate->text(), u"07.10.2026"_s);
    QVERIFY(numericDate->placeholderText().contains(u"ГГММДД"_s));
    QVERIFY(additionalDate->placeholderText().contains(u"Готовая дата"_s));
    QVERIFY(!dialog.findChild<QLineEdit*>(u"placeholderValue_GS"_s));
    QVERIFY(!dialog.findChild<QLineEdit*>(u"placeholderValue_gS"_s));
    QVERIFY(apply->isEnabled());
    numericDate->setText(u"261008"_s);
    QCOMPARE(dialog.values().value(u"[[11]]"_s), u"07.10.2026"_s);
    QCOMPARE(dialog.values().value(u"11"_s), u"261008"_s);
    auto candidate = document;
    candidate.previewValues = dialog.values();
    const auto resolved = Designer::resolveDataMatrixValues(candidate);
    QVERIFY(resolved.has_value());
    QCOMPARE(resolved->elements.first().text,
        u"09506000134352|261008|271007|07.10.2026|2027/10/07|BATCH-1|BATCH-1|"_s + QChar(29));
}

void Gs1DialogTests::builtinSeparatorDoesNotRequireValue()
{
    Designer::DesignDocument document;
    auto element = Designer::makeElement(Designer::ElementType::DataMatrix);
    element.gs1 = false;
    element.text = u"A{{GS}}B"_s;
    document.elements = {element};
    Designer::PlaceholderValuesDialog dialog(document);
    auto* table = dialog.findChild<QTableWidget*>(u"placeholderValuesTable"_s);
    auto* apply = dialog.findChild<QPushButton*>(u"placeholderValuesApply"_s);
    QVERIFY(table);
    QVERIFY(apply);
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(apply->isEnabled());
    QVERIFY(dialog.values().isEmpty());
    const auto resolved = Designer::resolveDataMatrixValues(document);
    QVERIFY(resolved.has_value());
    QCOMPARE(resolved->elements.first().text, u"A"_s + QChar(29) + u"B"_s);
}

QTEST_MAIN(Gs1DialogTests)
#include "test_gs1_dialog.moc"
