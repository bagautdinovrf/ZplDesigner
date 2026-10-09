#include "gs1_dialog.hpp"

#include "gs1_data.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace Designer {
using namespace Qt::StringLiterals;

namespace {
QString fieldText(const QTableWidget* table, int row, int column)
{
    const auto* item = table->item(row, column);
    return item ? item->text() : QString{};
}

QString fieldRule(const QString& ai)
{
    const auto& identifiers = gs1ApplicationIdentifiers();
    const auto found = std::ranges::find(identifiers, ai, &Gs1Ai::ai);
    if (found == identifiers.end()) return u"Неизвестный AI"_s;
    const QString length = found->minimumLength == found->maximumLength
        ? QString::number(found->maximumLength)
        : u"%1–%2"_s.arg(found->minimumLength).arg(found->maximumLength);
    return u"%1 · %2 %3"_s.arg(found->title, length,
        found->numeric ? u"цифр"_s : u"символов"_s);
}
} // namespace

Gs1Dialog::Gs1Dialog(QString input, QWidget* parent) : QDialog(parent)
{
    setObjectName(u"gs1Dialog"_s);
    setWindowTitle(u"Настройка GS1 DataMatrix"_s);
    resize(800, 720);
    setMinimumSize(600, 500);

    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        u"Добавьте идентификаторы применения (AI) и их значения. "
        "Требуется GTIN (01) или SSCC (00). Разделители полей переменной длины "
        "и начальный FNC1 при экспорте ZPL добавляются автоматически."_s, this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* tools = new QHBoxLayout;
    identifiers_ = new QComboBox(this);
    identifiers_->setObjectName(u"gs1Identifiers"_s);
    identifiers_->setAccessibleName(u"Идентификатор применения GS1"_s);
    identifiers_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    identifiers_->setMinimumContentsLength(20);
    for (const auto& ai : gs1ApplicationIdentifiers())
        identifiers_->addItem(u"(%1) %2"_s.arg(ai.ai, ai.title), ai.ai);
    identifiers_->setCurrentIndex(identifiers_->findData(u"01"_s));
    tools->addWidget(identifiers_, 1);
    const auto button = [this, tools](const QString& text, const QString& name) {
        auto* result = new QPushButton(text, this);
        result->setObjectName(name);
        result->setAutoDefault(false);
        tools->addWidget(result);
        return result;
    };
    auto* add = button(u"Добавить"_s, u"gs1AddField"_s);
    remove_ = button(u"Удалить"_s, u"gs1RemoveField"_s);
    up_ = button(u"↑"_s, u"gs1MoveUp"_s);
    up_->setAccessibleName(u"Переместить поле выше"_s);
    up_->setToolTip(u"Переместить поле выше"_s);
    down_ = button(u"↓"_s, u"gs1MoveDown"_s);
    down_->setAccessibleName(u"Переместить поле ниже"_s);
    down_->setToolTip(u"Переместить поле ниже"_s);
    layout->addLayout(tools);

    fields_ = new QTableWidget(0, 3, this);
    fields_->setObjectName(u"gs1Fields"_s);
    fields_->setAccessibleName(u"Поля GS1 в порядке кодирования"_s);
    fields_->setHorizontalHeaderLabels({u"AI"_s, u"Значение"_s, u"Формат"_s});
    fields_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fields_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fields_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    fields_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fields_->setSelectionMode(QAbstractItemView::SingleSelection);
    fields_->setMinimumHeight(150);
    layout->addWidget(fields_, 1);

    auto* variableRow = new QHBoxLayout;
    insertAiVariable_ = new QPushButton(u"Вставить {{AI}}"_s, this);
    insertAiVariable_->setObjectName(u"gs1InsertAiVariable"_s);
    insertAiVariable_->setAutoDefault(false);
    variableRow->addWidget(insertAiVariable_);
    variableSyntax_ = new QComboBox(this);
    variableSyntax_->setObjectName(u"gs1VariableSyntax"_s);
    variableSyntax_->setAccessibleName(u"Вид плейсхолдера"_s);
    variableSyntax_->addItem(u"Дополнительное [[имя]]"_s, u"additional"_s);
    variableSyntax_->addItem(u"Именованное {{имя}}"_s, u"named"_s);
    variableRow->addWidget(variableSyntax_);
    variableName_ = new QLineEdit(this);
    variableName_->setObjectName(u"gs1VariableName"_s);
    variableName_->setAccessibleName(u"Имя переменной для выбранного поля GS1"_s);
    variableName_->setPlaceholderText(u"batch, order_name"_s);
    variableValidator_ = new QRegularExpressionValidator(
        QRegularExpression(u"[A-Za-z0-9_]+"_s), variableName_);
    variableName_->setValidator(variableValidator_);
    variableRow->addWidget(variableName_, 1);
    insertVariable_ = new QPushButton(u"Вставить [[имя]]"_s, this);
    insertVariable_->setObjectName(u"gs1InsertVariable"_s);
    insertVariable_->setAutoDefault(false);
    insertVariable_->setToolTip(u"Заменить значение выбранной строки на переменную. "
                                 "Текст и несколько переменных можно сочетать вручную в ячейке."_s);
    variableRow->addWidget(insertVariable_);
    layout->addLayout(variableRow);

    auto* sourceLabel = new QLabel(u"Строка данных: (AI)значение, шаблон или строка сканера"_s, this);
    layout->addWidget(sourceLabel);
    source_ = new QPlainTextEdit(this);
    source_->setObjectName(u"gs1Source"_s);
    source_->setAccessibleName(u"Исходная строка GS1"_s);
    source_->setPlaceholderText(u"(01){{01}}(17){{17}}(10)[[batch]](21){{21}}"_s);
    source_->setMaximumHeight(90);
    source_->setTabChangesFocus(true);
    sourceLabel->setBuddy(source_);
    layout->addWidget(source_);

    status_ = new QLabel(this);
    status_->setObjectName(u"gs1Status"_s);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    auto* encodedLabel = new QLabel(u"Данные или шаблон (⟨GS⟩ — разделитель ASCII 29):"_s, this);
    layout->addWidget(encodedLabel);
    encoded_ = new QPlainTextEdit(this);
    encoded_->setObjectName(u"gs1Encoded"_s);
    encoded_->setAccessibleName(u"Данные GS1 с видимыми разделителями"_s);
    encoded_->setReadOnly(true);
    encoded_->setMaximumHeight(75);
    encodedLabel->setBuddy(encoded_);
    layout->addWidget(encoded_);

    auto* help = new QLabel(
        u"Поля маркера: {{01}}, {{21}}; {{11}} и {{17}} — даты ГГММДД. "
        "Дополнительные поля: [[batch]], [[order_name]] без учёта регистра. "
        "[[11]] и [[17]] — отдельные, уже отформатированные строки даты: вводите их вручную. "
        "{{GS}} — встроенный разделитель ASCII 29; значение для него не нужно. "
        "Между AI конструктор добавляет разделители сам. Именованные {{gtin}} сохранены для совместимости. "
        "Кнопка «Значения плейсхолдеров…» задаёт данные текущей сессии для превью и заполненного ZPL; "
        "сохранение шаблона оставляет плейсхолдеры. Для целого кода используйте [[code]]."_s, this);
    help->setTextFormat(Qt::PlainText);
    help->setWordWrap(true);
    layout->addWidget(help);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(u"gs1Buttons"_s);
    buttons_->button(QDialogButtonBox::Ok)->setText(u"Применить"_s);
    buttons_->button(QDialogButtonBox::Ok)->setObjectName(u"gs1Apply"_s);
    buttons_->button(QDialogButtonBox::Cancel)->setText(u"Отмена"_s);
    layout->addWidget(buttons_);

    connect(source_, &QPlainTextEdit::textChanged, this, &Gs1Dialog::readSource);
    connect(fields_, &QTableWidget::itemChanged, this, &Gs1Dialog::readFields);
    connect(fields_, &QTableWidget::currentCellChanged, this, &Gs1Dialog::updateActions);
    connect(variableName_, &QLineEdit::textChanged, this, &Gs1Dialog::updateActions);
    connect(variableSyntax_, &QComboBox::currentIndexChanged, this, &Gs1Dialog::updateVariableSyntax);
    connect(insertVariable_, &QPushButton::clicked, this, &Gs1Dialog::insertVariable);
    connect(insertAiVariable_, &QPushButton::clicked, this, &Gs1Dialog::insertAiVariable);
    connect(add, &QPushButton::clicked, this, &Gs1Dialog::addField);
    connect(remove_, &QPushButton::clicked, this, &Gs1Dialog::removeField);
    connect(up_, &QPushButton::clicked, this, [this] { moveField(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveField(1); });
    connect(buttons_, &QDialogButtonBox::accepted, this, &Gs1Dialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    {
        const QSignalBlocker blocker(source_);
        source_->setPlainText(input);
    }
    readSource();
}

void Gs1Dialog::readSource()
{
    const auto parsed = parseGs1Template(source_->toPlainText());
    if (!parsed) {
        showError(parsed.error());
        return;
    }
    {
        const QSignalBlocker blocker(fields_);
        fields_->setRowCount(static_cast<int>(parsed->fields.size()));
        for (int row = 0; row < fields_->rowCount(); ++row) {
            const auto& field = parsed->fields[row];
            fields_->setItem(row, 0, new QTableWidgetItem(field.ai));
            fields_->setItem(row, 1, new QTableWidgetItem(field.value));
        }
    }
    updateRowRules();
    showValidData(*parsed);
}

void Gs1Dialog::readFields()
{
    QList<Gs1Field> values;
    QString source;
    for (int row = 0; row < fields_->rowCount(); ++row) {
        const QString ai = fieldText(fields_, row, 0);
        const QString value = fieldText(fields_, row, 1);
        values.append({ai, value});
        source += u"(%1)%2"_s.arg(ai, value);
    }
    const auto built = buildGs1Template(values);
    {
        const QSignalBlocker blocker(source_);
        source_->setPlainText(built ? built->canonicalText : source);
    }
    updateRowRules();
    if (built) showValidData(*built);
    else showError(built.error());
}

void Gs1Dialog::showValidData(const Gs1Data& data)
{
    valid_ = true;
    data_ = data.canonicalText;
    QString visible = data.elementString;
    visible.replace(QChar(0x1d), u"⟨GS⟩"_s);
    encoded_->setPlainText(visible);
    if (!data.requiredParameters.isEmpty()) {
        status_->setText((data.wholeCodePlaceholder
            ? u"Шаблон целого GS1-кода. Параметры: %1. Поля и значения проверяются после подстановки."_s
            : u"Структура шаблона GS1 корректна. Параметры: %1. Значения проверяются после подстановки."_s)
                .arg(data.requiredParameters.join(u", "_s)));
    } else {
        status_->setText(u"Данные корректны для поддерживаемого набора GS1: %1 полей, %2 байт. "
                       "Начальный FNC1 добавляется при экспорте."_s
                         .arg(data.fields.size()).arg(data.elementString.size()));
    }
    updateActions();
}

void Gs1Dialog::showError(const QString& error)
{
    valid_ = false;
    data_.clear();
    encoded_->clear();
    status_->setText(u"Ошибка: %1\nИсправьте строку данных или отредактируйте поля таблицы."_s.arg(error));
    updateActions();
}

void Gs1Dialog::updateRowRules()
{
    const QSignalBlocker blocker(fields_);
    for (int row = 0; row < fields_->rowCount(); ++row) {
        auto* item = fields_->item(row, 2);
        if (!item) {
            item = new QTableWidgetItem;
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            fields_->setItem(row, 2, item);
        }
        item->setText(fieldRule(fieldText(fields_, row, 0)));
        item->setToolTip(item->text());
    }
}

void Gs1Dialog::updateActions()
{
    const int row = fields_->currentRow();
    const bool selected = row >= 0 && row < fields_->rowCount();
    remove_->setEnabled(selected);
    up_->setEnabled(selected && row > 0);
    down_->setEnabled(selected && row + 1 < fields_->rowCount());
    insertVariable_->setEnabled(selected && variableName_->hasAcceptableInput());
    const QString ai = selected ? fieldText(fields_, row, 0) : QString{};
    const bool numericAi = ai.size() == 2 && std::ranges::all_of(ai, [](QChar ch) {
        return ch >= u'0' && ch <= u'9';
    });
    insertAiVariable_->setEnabled(numericAi);
    insertAiVariable_->setText(numericAi ? u"Вставить {{%1}}"_s.arg(ai) : u"Вставить {{AI}}"_s);
    insertAiVariable_->setToolTip(numericAi
        ? u"Заменить значение AI (%1) на поле маркера {{%1}}."_s.arg(ai)
        : u"Выберите AI из двух цифр. Для AI из 3–4 цифр используйте дополнительное поле [[имя]]."_s);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid_);
}

void Gs1Dialog::addField()
{
    const int row = fields_->rowCount();
    {
        const QSignalBlocker blocker(fields_);
        fields_->insertRow(row);
        fields_->setItem(row, 0, new QTableWidgetItem(identifiers_->currentData().toString()));
        fields_->setItem(row, 1, new QTableWidgetItem);
        fields_->setCurrentCell(row, 1);
    }
    readFields();
    fields_->editItem(fields_->item(row, 1));
}

void Gs1Dialog::removeField()
{
    const int row = fields_->currentRow();
    if (row < 0) return;
    {
        const QSignalBlocker blocker(fields_);
        fields_->removeRow(row);
        if (fields_->rowCount() > 0)
            fields_->setCurrentCell(std::min(row, fields_->rowCount() - 1), 1);
    }
    readFields();
}

void Gs1Dialog::moveField(int delta)
{
    const int row = fields_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= fields_->rowCount()) return;
    {
        const QSignalBlocker blocker(fields_);
        for (int column = 0; column < fields_->columnCount(); ++column) {
            auto* source = fields_->takeItem(row, column);
            auto* destination = fields_->takeItem(target, column);
            fields_->setItem(target, column, source);
            fields_->setItem(row, column, destination);
        }
        fields_->setCurrentCell(target, 1);
    }
    readFields();
}

void Gs1Dialog::accept()
{
    if (valid_) QDialog::accept();
}

void Gs1Dialog::insertVariable()
{
    const int row = fields_->currentRow();
    if (row < 0 || row >= fields_->rowCount() || !variableName_->hasAcceptableInput()) return;
    auto* value = fields_->item(row, 1);
    if (!value) return;
    value->setText(variableSyntax_->currentData().toString() == u"additional"_s
        ? u"[[%1]]"_s.arg(variableName_->text().toLower())
        : u"{{%1}}"_s.arg(variableName_->text()));
    fields_->setCurrentCell(row, 1);
}

void Gs1Dialog::insertAiVariable()
{
    if (!insertAiVariable_->isEnabled()) return;
    const int row = fields_->currentRow();
    auto* value = fields_->item(row, 1);
    if (!value) return;
    value->setText(u"{{%1}}"_s.arg(fieldText(fields_, row, 0)));
    fields_->setCurrentCell(row, 1);
}

void Gs1Dialog::updateVariableSyntax()
{
    const bool additional = variableSyntax_->currentData().toString() == u"additional"_s;
    variableValidator_->setRegularExpression(QRegularExpression(additional
        ? u"[A-Za-z0-9_]+"_s : u"[A-Za-z_][A-Za-z0-9_.-]*"_s));
    insertVariable_->setText(additional ? u"Вставить [[имя]]"_s : u"Вставить {{имя}}"_s);
    updateActions();
}

} // namespace Designer
