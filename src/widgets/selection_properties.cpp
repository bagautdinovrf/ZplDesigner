#include "selection_properties.hpp"
#include "dot_spin_box.hpp"
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalBlocker>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
enum class Kind { Code, Shape, Text };
struct Field {
    const char* key;
    const char16_t* title;
    Kind kind;
    int Element::*member;
    std::optional<int> SelectionPropertyValues::*requestMember;
    int minimum;
    int maximum;
};
constexpr Field fields[] = {
    {"moduleWidth", u"Модуль кодов", Kind::Code, &Element::moduleWidth, &SelectionPropertyValues::moduleWidth, 1, 10},
    {"thickness", u"Толщина линий", Kind::Shape, &Element::thickness, &SelectionPropertyValues::thickness, 1, 4096},
    {"fontHeight", u"Кегль текста", Kind::Text, &Element::fontHeight, &SelectionPropertyValues::fontHeight, 1, 512},
    {"fontWidth", u"Ширина букв (0 — авто)", Kind::Text, &Element::fontWidth, &SelectionPropertyValues::fontWidth, 0, 512},
    {"blockWidth", u"Ширина блока (0 — нет)", Kind::Text, &Element::blockWidth, &SelectionPropertyValues::blockWidth, 0, 4096},
    {"blockMaxLines", u"Строк в блоке", Kind::Text, &Element::blockMaxLines, &SelectionPropertyValues::blockMaxLines, 1, 128},
    {"blockLineSpacing", u"Интервал строк", Kind::Text, &Element::blockLineSpacing, &SelectionPropertyValues::blockLineSpacing, -512, 512},
    {"blockHangingIndent", u"Отступ строк", Kind::Text, &Element::blockHangingIndent, &SelectionPropertyValues::blockHangingIndent, -4096, 4096}
};
bool accepts(ElementType type, Kind kind)
{
    if (kind == Kind::Text) return type == ElementType::Text;
    if (kind == Kind::Code) return isBarcodeType(type) && type != ElementType::MaxiCode;
    return type == ElementType::Box || type == ElementType::Line || type == ElementType::Circle
        || type == ElementType::Ellipse || type == ElementType::Diagonal;
}
}
SelectionProperties::SelectionProperties(QWidget* parent) : QWidget(parent), form_(new QFormLayout(this))
{
    setObjectName(u"selectionProperties"_s);
    // Long captions move above their fields so the narrowest inspector still fits.
    form_->setContentsMargins(13, 12, 13, 0);
    form_->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto* hint = new QLabel(u"Параметры применяются ко всем подходящим объектам выделения. «Разные» оставляет значения без изменений."_s, this);
    hint->setWordWrap(true); form_->addRow(hint);
    for (const auto& field : fields) {
        const auto key = QString::fromLatin1(field.key);
        QSpinBox* spin = key == QStringLiteral("blockMaxLines") ? new QSpinBox(this) : new DotSpinBox(this);
        spin->setObjectName(u"selection_"_s + key);
        spin->setRange(field.minimum - 1, field.maximum);
        spin->setSpecialValueText(u"Разные"_s);
        spin->setKeyboardTracking(false);
        fields_.insert(key, spin);
        connect(spin, &QSpinBox::valueChanged, this, [this, key] { changedFields_.insert(key); });
        form_->addRow(QString::fromUtf16(field.title), spin);
    }
    apply_ = new QPushButton(u"Применить к выделению"_s, this);
    apply_->setObjectName(u"applySelectionProperties"_s);
    form_->addRow(apply_);
    connect(apply_, &QPushButton::clicked, this, [this] {
        SelectionPropertyValues values;
        for (const auto& field : fields) {
            const auto key = QString::fromLatin1(field.key);
            const auto* spin = fields_.value(key);
            if (changedFields_.contains(key) && !spin->isHidden() && spin->value() >= field.minimum)
                values.*field.requestMember = spin->value();
        }
        emit changesRequested(values);
    });
}
void SelectionProperties::setDisplayUnits(CanvasUnit unit, int dpi) {
    for (auto* spin : findChildren<DotSpinBox*>()) spin->setDisplayUnits(unit, dpi);
}
void SelectionProperties::setSelection(const DesignDocument& document, const QList<int>& indices)
{
    QString state;
    for (const int index : indices) {
        if (index < 0 || index >= document.elements.size()) continue;
        const auto& element = document.elements[index];
        state += element.id.toString() + u':' + QString::number(int(element.type));
        for (const auto& field : fields) state += u',' + QString::number(element.*field.member);
        state += u';';
    }
    // Preview refreshes do not change selection values and must not erase
    // the user's unapplied edits in this panel.
    if (state == selectionState_) return;
    selectionState_ = state;
    changedFields_.clear();
    bool any = false;
    for (const auto& field : fields) {
        auto* spin = fields_.value(QString::fromLatin1(field.key));
        int count = 0, value = field.minimum - 1;
        bool mixed = false;
        for (const int index : indices) {
            if (index < 0 || index >= document.elements.size()) continue;
            const auto& element = document.elements[index];
            if (!accepts(element.type, field.kind)) continue;
            const int current = element.*field.member;
            if (!count) value = current; else mixed |= current != value;
            ++count;
        }
        const QSignalBlocker blocker(spin);
        form_->setRowVisible(spin, count > 0);
        spin->setValue(mixed ? field.minimum - 1 : value);
        spin->setToolTip(u"Подходящих объектов: %1"_s.arg(count));
        any |= count > 0;
    }
    apply_->setEnabled(any);
}
}
