#include "workspace_widgets.hpp"
#include "designer_theme.hpp"
#include "dot_spin_box.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QPainter>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

namespace Designer {

WorkspaceSurface::WorkspaceSurface(QWidget* parent, bool alternate, bool rounded, bool bottomSeparator)
    : QWidget(parent), alternate_(alternate), rounded_(rounded), bottomSeparator_(bottomSeparator) {}

void WorkspaceSurface::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, rounded_);
    p.setPen(designerColor(DesignerColor::Border));
    p.setBrush(designerColor(alternate_ ? DesignerColor::PanelAlternate : DesignerColor::Panel));
    if (rounded_) p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 7, 7);
    else {
        p.fillRect(rect(), p.brush());
        if (bottomSeparator_) p.drawLine(0, height() - 1, width(), height() - 1);
    }
}

void ObjectBadge::paintEvent(QPaintEvent* event)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen);
    const bool outline = property("outline").toBool();
    if (outline) p.setPen(designerColor(DesignerColor::Border));
    p.setBrush(designerColor(outline ? DesignerColor::PanelAlternate : DesignerColor::Tint));
    p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), outline ? 3 : 5, outline ? 3 : 5); p.end();
    QLabel::paintEvent(event);
}

QSize ElidedLabel::minimumSizeHint() const
{
    return {fontMetrics().horizontalAdvance(QStringLiteral("W…")), QLabel::minimumSizeHint().height()};
}

void ElidedLabel::paintEvent(QPaintEvent*)
{
    QStringList lines = text().split(u'\n');
    for (auto& line : lines) line = fontMetrics().elidedText(line, Qt::ElideRight, contentsRect().width());
    QPainter p(this);
    style()->drawItemText(&p, contentsRect(), QStyle::visualAlignment(layoutDirection(), alignment()),
                          palette(), isEnabled(), lines.join(u'\n'), foregroundRole());
}

QLabel* caption(const QString& text, QWidget* parent, int size) {
    auto* label = new QLabel(text, parent);
    auto font = label->font(); font.setPixelSize(size); label->setFont(font);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

QWidget* propertyField(const QString& text, QWidget* control, QWidget* parent) {
    auto* field = new QWidget(parent);
    auto* layout = new QVBoxLayout(field); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(5);
    layout->addWidget(caption(text, field)); layout->addWidget(control);
    control->setFixedHeight(30);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (auto* combo = qobject_cast<QComboBox*>(control)) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(4);
    }
    control->setProperty("propertyField", QVariant::fromValue<QObject*>(field));
    if (auto* spin = qobject_cast<QSpinBox*>(control)) {
        spin->setProperty("hideUnitSuffix", true);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setMinimumWidth(0);
    }
    return field;
}

void setPropertyRowVisible(QFormLayout* form, QWidget* control, bool visible) {
    if (auto* field = qobject_cast<QWidget*>(control->property("propertyField").value<QObject*>())) field->setVisible(visible);
    else form->setRowVisible(control, visible);
}



QLabel* heading(const QString& text, QWidget* parent, bool elide) {
    auto* label = elide ? new ElidedLabel(text, parent) : new QLabel(text, parent);
    auto font = label->font(); font.setWeight(QFont::DemiBold); font.setPixelSize(12);
    label->setFont(font);
    return label;
}
QLabel* sectionHeading(const QString& text, QWidget* parent) {
    auto* label = heading(text, parent); auto font = label->font(); font.setPixelSize(11); label->setFont(font); return label;
}
QSpinBox* spin(const QString& name, int minimum, int maximum, QWidget* parent) {
    auto* value = new QSpinBox(parent);
    value->setObjectName(name);
    value->setRange(minimum, maximum);
    value->setKeyboardTracking(false);
    value->setMinimumHeight(30);
    return value;
}
QSpinBox* dotSpin(const QString& name, int minimum, int maximum, QWidget* parent) {
    auto* value = new DotSpinBox(parent);
    value->setObjectName(name);
    value->setRange(minimum, maximum);
    return value;
}
} // namespace Designer
