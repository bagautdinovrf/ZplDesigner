#include "table_dialog.hpp"
#include "dot_spin_box.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <numeric>
#include <algorithm>

namespace Designer {
TableDialog::TableDialog(const Element& element, CanvasUnit unit, int dpi, QWidget* parent)
    : QDialog(parent), element_(element), dpi_(dpi) {
    setObjectName(QStringLiteral("tableDialog"));
    setWindowTitle(tr("Таблица")); resize(790, 680);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Двойной щелчок редактирует ячейку. Можно вставлять {{name}} и [[name]] с латинскими именами. Выберите ячейку для точного размера её строки и колонки; заголовки можно растягивать мышью."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* structure = new QHBoxLayout;
    const auto count = [this, structure](const QString& text, const QString& name, int value) {
        structure->addWidget(new QLabel(text, this));
        auto* control = new QSpinBox(this); control->setObjectName(name);
        control->setRange(1, 12); control->setKeyboardTracking(false); control->setValue(value);
        structure->addWidget(control); return control;
    };
    rows_ = count(tr("Строки"), QStringLiteral("tableRows"), int(element_.tableRowHeights.size()));
    columns_ = count(tr("Колонки"), QStringLiteral("tableColumns"), int(element_.tableColumnWidths.size()));
    layout->addLayout(structure);
    cells_ = new QTableWidget(this); cells_->setObjectName(QStringLiteral("tableCells"));
    cells_->setSelectionMode(QAbstractItemView::SingleSelection);
    cells_->horizontalHeader()->setMinimumSectionSize(24);
    cells_->verticalHeader()->setMinimumSectionSize(20);
    layout->addWidget(cells_, 1);
    auto* form = new QFormLayout;
    const auto dimension = [this, form, unit, dpi](const QString& label, const QString& name, int minimum, int maximum, int value) {
        auto* control = new DotSpinBox(this); control->setObjectName(name); control->setRange(minimum, maximum);
        control->setValue(value); control->setDisplayUnits(unit, dpi); form->addRow(label, control); return control;
    };
    rowHeight_ = dimension(tr("Высота выбранной строки"), QStringLiteral("tableRowHeight"), 8, 4096, 48);
    columnWidth_ = dimension(tr("Ширина выбранной колонки"), QStringLiteral("tableColumnWidth"), 8, 4096, 120);
    auto* font = dimension(tr("Высота букв"), QStringLiteral("tableFontHeight"), 1, 512, element_.fontHeight);
    auto* padding = dimension(tr("Отступ текста"), QStringLiteral("tablePadding"), 0, 128, element_.tablePadding);
    auto* border = dimension(tr("Толщина границ"), QStringLiteral("tableBorderWidth"), 1, 64, element_.thickness);
    auto* borders = new QCheckBox(tr("Печатать границы"), this); borders->setChecked(element_.tableBorders);
    borders->setObjectName(QStringLiteral("tableBorders")); form->addRow(borders);
    auto* alignment = new QComboBox(this); alignment->setObjectName(QStringLiteral("tableAlignment"));
    alignment->addItem(tr("Слева"), QStringLiteral("L")); alignment->addItem(tr("По центру"), QStringLiteral("C"));
    alignment->addItem(tr("Справа"), QStringLiteral("R"));
    alignment->setCurrentIndex(std::max(0, alignment->findData(QString(element_.blockJustification))));
    form->addRow(tr("Выравнивание текста"), alignment);
    layout->addLayout(form);
    result_ = new QLabel(this); result_->setWordWrap(true); result_->setObjectName(QStringLiteral("tableSizeResult"));
    layout->addWidget(result_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    const auto gridChanged = [this] {
        resizeTableGrid(element_, rows_->value(), columns_->value()); refreshGrid();
    };
    connect(rows_, &QSpinBox::valueChanged, this, gridChanged);
    connect(columns_, &QSpinBox::valueChanged, this, gridChanged);
    connect(cells_, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (!updating_ && cells_->item(row, column))
            element_.tableCells[row * columns_->value() + column] = cells_->item(row, column)->text();
        validateSize();
    });
    connect(cells_, &QTableWidget::currentCellChanged, this, [this] { refreshSelection(); });
    connect(rowHeight_, &QSpinBox::valueChanged, this, [this](int value) {
        if (updating_ || cells_->currentRow() < 0) return;
        element_.tableRowHeights[cells_->currentRow()] = value; refreshGrid();
    });
    connect(columnWidth_, &QSpinBox::valueChanged, this, [this](int value) {
        if (updating_ || cells_->currentColumn() < 0) return;
        element_.tableColumnWidths[cells_->currentColumn()] = value; refreshGrid();
    });
    connect(cells_->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int index, int, int value) {
        if (updating_) return;
        element_.tableColumnWidths[index] = std::max(8, value); refreshSelection(); validateSize();
    });
    connect(cells_->verticalHeader(), &QHeaderView::sectionResized, this, [this](int index, int, int value) {
        if (updating_) return;
        element_.tableRowHeights[index] = std::max(8, value); refreshSelection(); validateSize();
    });
    connect(font, &QSpinBox::valueChanged, this, [this](int v) { element_.fontHeight = v; validateSize(); });
    connect(padding, &QSpinBox::valueChanged, this, [this](int v) { element_.tablePadding = v; validateSize(); });
    connect(border, &QSpinBox::valueChanged, this, [this](int v) { element_.thickness = v; validateSize(); });
    connect(borders, &QCheckBox::toggled, this, [this](bool v) { element_.tableBorders = v; validateSize(); });
    connect(alignment, &QComboBox::currentIndexChanged, this, [this, alignment] {
        element_.blockJustification = alignment->currentData().toString().at(0);
    });
    refreshGrid();
}

void TableDialog::refreshGrid() {
    const int row = std::clamp(cells_->currentRow(), 0, rows_->value() - 1);
    const int column = std::clamp(cells_->currentColumn(), 0, columns_->value() - 1);
    updating_ = true;
    cells_->setRowCount(rows_->value()); cells_->setColumnCount(columns_->value());
    for (int r = 0; r < rows_->value(); ++r) {
        cells_->setRowHeight(r, std::min(240, element_.tableRowHeights[r]));
        for (int c = 0; c < columns_->value(); ++c) {
            if (!cells_->item(r, c)) cells_->setItem(r, c, new QTableWidgetItem);
            cells_->item(r, c)->setText(element_.tableCells.value(r * columns_->value() + c));
        }
    }
    for (int c = 0; c < columns_->value(); ++c) cells_->setColumnWidth(c, std::min(400, element_.tableColumnWidths[c]));
    cells_->setCurrentCell(row, column);
    updating_ = false; refreshSelection(); validateSize();
}

void TableDialog::refreshSelection() {
    const QSignalBlocker rowBlock(rowHeight_), columnBlock(columnWidth_);
    rowHeight_->setValue(element_.tableRowHeights.value(cells_->currentRow(), 48));
    columnWidth_->setValue(element_.tableColumnWidths.value(cells_->currentColumn(), 120));
}

void TableDialog::validateSize() {
    element_.width = std::accumulate(element_.tableColumnWidths.begin(), element_.tableColumnWidths.end(), 0);
    element_.height = std::accumulate(element_.tableRowHeights.begin(), element_.tableRowHeights.end(), 0);
    bool valid = element_.width <= 4096 && element_.height <= 4096;
    for (const auto& text : element_.tableCells) valid = valid && text.size() <= 1024;
    for (int width : element_.tableColumnWidths) valid = valid && width > 2 * element_.tablePadding;
    for (int height : element_.tableRowHeights) valid = valid && height >= element_.fontHeight + 2 * element_.tablePadding;
    if (element_.tableBorders) {
        for (int width : element_.tableColumnWidths) valid = valid && element_.thickness <= width;
        for (int height : element_.tableRowHeights) valid = valid && element_.thickness <= height;
    }
    result_->setText(valid ? tr("%1 × %2 dots · %3 × %4 мм. Текст ограничен размерами ячейки. Уменьшение числа строк/колонок удаляет соответствующие ячейки после нажатия OK.")
        .arg(element_.width).arg(element_.height).arg(element_.width * 25.4 / dpi_, 0, 'f', 3)
        .arg(element_.height * 25.4 / dpi_, 0, 'f', 3)
        : tr("Проверьте размеры: до 4096 dots на сторону; высота строки должна вмещать буквы и отступы, толщина границ — помещаться в ячейку; до 1024 символов в ячейке."));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

Element TableDialog::editedElement() const { return element_; }
}
