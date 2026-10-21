#include "canvas_layout_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace Designer {
using namespace Qt::StringLiterals;

CanvasLayoutDialog::CanvasLayoutDialog(CanvasLayoutSettings settings, QSize page, int dpi, QWidget* parent)
    : QDialog(parent), settings_(std::move(settings)), page_(page), dpi_(std::max(1, dpi))
{
    setObjectName(u"canvasLayoutDialog"_s);
    setWindowTitle(u"Холст и направляющие"_s);
    resize(540, 620);
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(u"Ориентиры видны только на холсте. В ZPL, чистое превью и PNG они не попадают."_s, this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* form = new QFormLayout;
    auto* units = new QComboBox(this);
    units->setObjectName(u"canvasUnits"_s);
    units->addItem(u"Миллиметры"_s, int(CanvasUnit::Millimeters));
    units->addItem(u"Точки"_s, int(CanvasUnit::Dots));
    units->setCurrentIndex(units->findData(int(settings_.unit)));
    form->addRow(u"Единицы линеек и настроек"_s, units);
    auto* visibility = new QHBoxLayout;
    const auto check = [this, visibility](const QString& text, const QString& name, bool CanvasLayoutSettings::* member) {
        auto* control = new QCheckBox(text, this);
        control->setObjectName(name);
        control->setChecked(settings_.*member);
        visibility->addWidget(control);
        connect(control, &QCheckBox::toggled, this, [this, member](bool value) { settings_.*member = value; updateHint(); });
    };
    check(u"Сетка"_s, u"layoutShowGrid"_s, &CanvasLayoutSettings::gridVisible);
    check(u"Линейки"_s, u"layoutShowRulers"_s, &CanvasLayoutSettings::rulersVisible);
    check(u"Направляющие"_s, u"layoutShowGuides"_s, &CanvasLayoutSettings::guidesVisible);
    form->addRow(visibility);
    auto* selection = new QHBoxLayout;
    const auto selectionCheck = [this, selection](const QString& text, const QString& name) {
        auto* control = new QCheckBox(text, this);
        control->setObjectName(name);
        control->setChecked(true);
        control->setToolTip(u"Общая настройка редактора для всех этикеток; в файл этикетки не сохраняется."_s);
        selection->addWidget(control);
        return control;
    };
    geometrySizes_ = selectionCheck(u"Размеры"_s, u"layoutShowGeometrySizes"_s);
    geometryCoordinates_ = selectionCheck(u"Координаты"_s, u"layoutShowGeometryCoordinates"_s);
    selection->addStretch();
    form->addRow(u"Подписи выделения"_s, selection);
    auto* snap = new QCheckBox(u"Привязка к сетке и видимым ориентирам (Alt — временно отключить)"_s, this);
    snap->setObjectName(u"layoutSnap"_s);
    snap->setChecked(settings_.snapEnabled);
    connect(snap, &QCheckBox::toggled, this, [this](bool enabled) { settings_.snapEnabled = enabled; });
    form->addRow(snap);
    const auto spin = [this](const QString& name) {
        auto* control = new QDoubleSpinBox(this);
        control->setObjectName(name);
        control->setKeyboardTracking(false);
        return control;
    };
    step_ = spin(u"layoutGridStep"_s);
    margin_ = spin(u"layoutMargin"_s);
    form->addRow(u"Шаг сетки"_s, step_);
    auto* marginVisible = new QCheckBox(u"Рамка безопасного отступа"_s, this);
    marginVisible->setObjectName(u"layoutShowMargin"_s);
    marginVisible->setChecked(settings_.marginsVisible);
    connect(marginVisible, &QCheckBox::toggled, this, [this](bool enabled) { settings_.marginsVisible = enabled; updateHint(); });
    form->addRow(marginVisible, margin_);
    layout->addLayout(form);
    auto* tabs = new QTabWidget(this);
    for (int axis = 0; axis < 2; ++axis) {
        const QString prefix = axis == 0 ? u"horizontal"_s : u"vertical"_s;
        auto* tab = new QWidget(tabs);
        auto* column = new QVBoxLayout(tab);
        guides_[axis] = new QListWidget(tab);
        guides_[axis]->setObjectName(prefix + u"Guides"_s);
        guides_[axis]->setMinimumHeight(100);
        column->addWidget(guides_[axis]);
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(axis == 0 ? u"Y:"_s : u"X:"_s, tab));
        position_[axis] = spin(prefix + u"GuidePosition"_s);
        row->addWidget(position_[axis], 1);
        auto* add = new QPushButton(u"Добавить"_s, tab);
        add->setObjectName(prefix + u"GuideAdd"_s);
        auto* change = new QPushButton(u"Изменить"_s, tab);
        change->setObjectName(prefix + u"GuideChange"_s);
        auto* remove = new QPushButton(u"Удалить"_s, tab);
        remove->setObjectName(prefix + u"GuideRemove"_s);
        row->addWidget(add); row->addWidget(change); row->addWidget(remove);
        column->addLayout(row);
        tabs->addTab(tab, axis == 0 ? u"Горизонтальные"_s : u"Вертикальные"_s);
        connect(guides_[axis], &QListWidget::currentRowChanged, this, [this, axis, change, remove](int index) {
            const auto& values = axis == 0 ? settings_.horizontalGuides : settings_.verticalGuides;
            const bool valid = index >= 0 && index < values.size();
            change->setEnabled(valid); remove->setEnabled(valid);
            if (valid) position_[axis]->setValue(values[index] * unitsPerDot());
        });
        change->setEnabled(false); remove->setEnabled(false);
        const auto store = [this, axis](bool replace) {
            auto& values = axis == 0 ? settings_.horizontalGuides : settings_.verticalGuides;
            const int selected = guides_[axis]->currentRow();
            if (replace && (selected < 0 || selected >= values.size())) return;
            if (!replace && values.size() >= 128) return;
            const int coordinate = std::clamp(qRound(position_[axis]->value() / unitsPerDot()), 0,
                                              axis == 0 ? page_.height() : page_.width());
            if (replace) values.removeAt(selected);
            if (!values.contains(coordinate)) values.append(coordinate);
            std::ranges::sort(values);
            refreshGuides(axis);
            guides_[axis]->setCurrentRow(int(values.indexOf(coordinate)));
        };
        connect(add, &QPushButton::clicked, this, [store] { store(false); });
        connect(change, &QPushButton::clicked, this, [store] { store(true); });
        connect(remove, &QPushButton::clicked, this, [this, axis] {
            auto& values = axis == 0 ? settings_.horizontalGuides : settings_.verticalGuides;
            const int selected = guides_[axis]->currentRow();
            if (selected < 0 || selected >= values.size()) return;
            values.removeAt(selected);
            refreshGuides(axis);
        });
    }
    layout->addWidget(tabs, 1);
    auto* help = new QLabel(u"Направляющие можно вытянуть из линеек и перемещать мышью. Чтобы удалить — вынесите за этикетку. Esc отменяет перетаскивание."_s, this);
    help->setWordWrap(true);
    layout->addWidget(help);
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    hint_->setObjectName(u"layoutResult"_s);
    layout->addWidget(hint_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    apply_ = buttons->button(QDialogButtonBox::Ok);
    apply_->setText(u"Применить"_s);
    buttons->button(QDialogButtonBox::Cancel)->setText(u"Отмена"_s);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(units, &QComboBox::currentIndexChanged, this, [this, units] {
        settings_.unit = CanvasUnit(units->currentData().toInt());
        refreshUnits();
    });
    connect(step_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        settings_.gridStep = std::clamp(qRound(value / unitsPerDot()), 1, 4096);
        updateHint();
    });
    connect(margin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        settings_.margin = std::clamp(qRound(value / unitsPerDot()), 0, 2048);
        updateHint();
    });
    refreshUnits();
}

CanvasLayoutSettings CanvasLayoutDialog::settings() const { return settings_; }

void CanvasLayoutDialog::setGeometryLabelsVisible(bool sizes, bool coordinates)
{
    geometrySizes_->setChecked(sizes);
    geometryCoordinates_->setChecked(coordinates);
}

bool CanvasLayoutDialog::geometrySizesVisible() const { return geometrySizes_->isChecked(); }

bool CanvasLayoutDialog::geometryCoordinatesVisible() const { return geometryCoordinates_->isChecked(); }

double CanvasLayoutDialog::unitsPerDot() const
{
    return settings_.unit == CanvasUnit::Dots ? 1.0 : 25.4 / dpi_;
}

void CanvasLayoutDialog::refreshUnits()
{
    const double factor = unitsPerDot();
    const bool dots = settings_.unit == CanvasUnit::Dots;
    const auto configure = [factor, dots](QDoubleSpinBox* control, int min, int max, double value) {
        const QSignalBlocker blocker(control);
        control->setDecimals(dots ? 0 : 3);
        control->setSuffix(dots ? u" dots"_s : u" мм"_s);
        control->setRange(min * factor, max * factor);
        control->setSingleStep(dots ? 1 : 0.5);
        control->setValue(value * factor);
    };
    configure(step_, 1, 4096, settings_.gridStep);
    configure(margin_, 0, 2048, settings_.margin);
    for (int axis = 0; axis < 2; ++axis) {
        // Only the temporary coordinate editor is reset on a unit switch.
        configure(position_[axis], 0, axis == 0 ? page_.height() : page_.width(), 0);
        refreshGuides(axis);
    }
    updateHint();
}

void CanvasLayoutDialog::refreshGuides(int axis)
{
    const auto& values = axis == 0 ? settings_.horizontalGuides : settings_.verticalGuides;
    guides_[axis]->clear();
    const int extent = axis == 0 ? page_.height() : page_.width();
    for (const int value : values) {
        QString text = settings_.unit == CanvasUnit::Dots ? u"%1 dots"_s.arg(value)
            : u"%1 мм · %2 dots"_s.arg(value * unitsPerDot(), 0, 'f', 3).arg(value);
        if (value > extent) text += u" (за этикеткой)"_s;
        guides_[axis]->addItem(text);
    }
}

void CanvasLayoutDialog::updateHint()
{
    if (!hint_ || !apply_) return;
    const bool valid = !settings_.marginsVisible || settings_.margin * 2 < std::min(page_.width(), page_.height());
    apply_->setEnabled(valid);
    hint_->setText(valid ? u"Фактически: сетка %1 dots (%2 мм), отступ %3 dots (%4 мм). Настройки холста сохраняются рядом с ZPL при сохранении этикетки; подписи выделения — общая настройка редактора."_s
        .arg(settings_.gridStep).arg(settings_.gridStep * 25.4 / dpi_, 0, 'f', 3)
        .arg(settings_.margin).arg(settings_.margin * 25.4 / dpi_, 0, 'f', 3)
        : u"Отступ должен быть меньше половины ширины и высоты этикетки."_s);
}

} // namespace Designer
