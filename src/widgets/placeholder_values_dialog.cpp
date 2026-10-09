#include "placeholder_values_dialog.hpp"

#include "data_matrix_values.hpp"
#include "value_placeholders.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
QMap<QString, QStringList> parameterForms(const DesignDocument& document)
{
    QMap<QString, QStringList> forms;
    for (const auto& element : document.elements) {
        if (!supportsValueParameters(element.type) || element.literalText) continue;
        const auto placeholders = valuePlaceholders(element.text);
        if (!placeholders) continue;
        for (const auto& placeholder : *placeholders) {
            if (placeholder.kind == ValuePlaceholderKind::GroupSeparator) continue;
            QString form;
            if (placeholder.kind == ValuePlaceholderKind::AdditionalField)
                form = placeholder.name.startsWith(u"[[") ? placeholder.name : u"[[%1]]"_s.arg(placeholder.name);
            else form = u"{{%1}}"_s.arg(placeholder.name);
            auto& values = forms[placeholder.name];
            if (!values.contains(form)) values.append(form);
        }
    }
    return forms;
}
} // namespace

PlaceholderValuesDialog::PlaceholderValuesDialog(const DesignDocument& document, QWidget* parent)
    : QDialog(parent), document_(document), values_(document.previewValues)
{
    setObjectName(u"placeholderValuesDialog"_s);
    setWindowTitle(u"Значения плейсхолдеров"_s);
    setModal(true);
    resize(680, 440);
    setMinimumSize(480, 320);

    const auto parameters = dataMatrixParameters(document_);
    if (parameters) parameters_ = *parameters;
    else parameterError_ = parameters.error();
    const auto forms = parameterForms(document_);

    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        u"Задайте значения переменных текста и штрихкодов для превью и экспорта заполненного ZPL. "
        "{{01}} и {{21}} — поля маркера; дополнительные [[имена]] не зависят от регистра. "
        "{{GS}} добавляет разделитель автоматически и не требует значения."_s, this);
    explanation->setTextFormat(Qt::PlainText);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    table_ = new QTableWidget(static_cast<int>(parameters_.size()), 2, this);
    table_->setObjectName(u"placeholderValuesTable"_s);
    table_->setAccessibleName(u"Значения переменных этикетки"_s);
    table_->setHorizontalHeaderLabels({u"Плейсхолдер"_s, u"Значение для подстановки"_s});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QString& name = parameters_[row];
        const bool gs1Date = name == u"11" || name == u"17" || name.endsWith(u"_date_gs1");
        const bool additionalDate = name == u"[[11]]" || name == u"[[17]]";
        QString label = forms.value(name).join(u", "_s);
        if (label.isEmpty()) label = name;
        if (gs1Date) label += u" · ГГММДД"_s;
        if (additionalDate) label += u" · строка даты"_s;
        auto* item = new QTableWidgetItem(label);
        item->setData(Qt::UserRole, name);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, item);
        auto* editor = new QLineEdit(table_);
        editor->setObjectName(u"placeholderValue_%1"_s.arg(name));
        editor->setAccessibleName(u"Значение %1"_s.arg(label));
        editor->setPlaceholderText(gs1Date ? u"ГГММДД, например 261007"_s
            : additionalDate ? u"Готовая дата, например 07.10.2026"_s : u"Обязательное значение"_s);
        editor->setText(values_.value(name));
        table_->setCellWidget(row, 1, editor);
        connect(editor, &QLineEdit::textChanged, this, &PlaceholderValuesDialog::updateValues);
    }
    layout->addWidget(table_, 1);

    status_ = new QLabel(this);
    status_->setObjectName(u"placeholderValuesStatus"_s);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    auto* hint = new QLabel(
        u"Значения действуют только в текущей сессии. Сохранение шаблона оставляет "
        "плейсхолдеры без изменений и не записывает введённые значения. "
        "Даты {{11}}/{{17}} вводите как ГГММДД; [[11]]/[[17]] — отдельные готовые строки. "
        "Маски дат и перенос даты печати из L2 здесь не вычисляются. "
        "Для GS1 проверяются длина, даты и контрольные цифры после подстановки."_s, this);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setObjectName(u"placeholderValuesApply"_s);
    buttons_->button(QDialogButtonBox::Ok)->setText(u"Применить"_s);
    buttons_->button(QDialogButtonBox::Cancel)->setText(u"Отмена"_s);
    connect(buttons_, &QDialogButtonBox::accepted, this, &PlaceholderValuesDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons_);
    updateValues();
}

void PlaceholderValuesDialog::updateValues()
{
    // Keep remembered values for parameters temporarily absent from a template.
    values_ = document_.previewValues;
    QStringList missing;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const auto* editor = qobject_cast<QLineEdit*>(table_->cellWidget(row, 1));
        const auto& name = parameters_[row];
        const QString value = editor->text();
        if (value.isEmpty()) {
            values_.remove(name);
            missing.append(table_->item(row, 0)->text());
        } else values_.insert(name, value);
    }

    QString error = parameterError_;
    if (error.isEmpty() && !missing.isEmpty())
        error = u"Не заданы значения: %1."_s.arg(missing.join(u", "_s));
    if (error.isEmpty()) {
        auto candidate = document_;
        candidate.previewValues = values_;
        const auto resolved = resolveDataMatrixValues(candidate);
        if (!resolved) error = resolved.error();
        else if (const auto checked = validateForExport(*resolved, false); !checked)
            error = checked.error();
    }
    valid_ = error.isEmpty();
    status_->setText(valid_
        ? (parameters_.isEmpty() ? u"Значения для подстановки не требуются."_s
                                : u"Значения подставлены и проверены. Можно обновить превью и экспортировать заполненный ZPL."_s)
        : u"Ошибка: %1"_s.arg(error));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid_);
}

void PlaceholderValuesDialog::accept()
{
    if (valid_) QDialog::accept();
}

} // namespace Designer
