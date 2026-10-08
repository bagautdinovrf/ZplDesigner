#include "advanced_text_dialog.hpp"
#include "dot_spin_box.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace Designer {
AdvancedTextDialog::AdvancedTextDialog(const Element& element, CanvasUnit unit, int dpi, QWidget* parent)
    : QDialog(parent), element_(element)
{
    setObjectName(QStringLiteral("advancedTextDialog"));
    setWindowTitle(tr("Положение и направление текста")); resize(580, 380);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Координаты X и Y относятся к выбранной точке привязки. При смене привязки текст может сдвинуться; результат виден на холсте после применения."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* form = new QFormLayout; layout->addLayout(form);
    auto* origin = new QComboBox(this); origin->setObjectName(QStringLiteral("textOrigin"));
    origin->addItem(tr("Верхняя точка поля (FO)"), false);
    origin->addItem(tr("Базовая линия текста (FT)"), true);
    origin->setCurrentIndex(element.baselineOrigin ? 1 : 0); form->addRow(tr("Точка по вертикали"), origin);
    auto* anchor = new QComboBox(this); anchor->setObjectName(QStringLiteral("textAnchor"));
    anchor->addItem(tr("По настройке этикетки"), -1); anchor->addItem(tr("Слева"), 0);
    anchor->addItem(tr("Справа"), 1); anchor->addItem(tr("Автоматически"), 2);
    anchor->setCurrentIndex(anchor->findData(element.originJustification)); form->addRow(tr("Точка по горизонтали"), anchor);
    auto* inherit = new QCheckBox(tr("Использовать поворот этикетки по умолчанию (FW)"), this);
    inherit->setObjectName(QStringLiteral("textInheritOrientation")); inherit->setChecked(element.useDefaultOrientation);
    form->addRow(inherit);
    auto* orientation = new QComboBox(this); orientation->setObjectName(QStringLiteral("textOrientation"));
    for (const auto& pair : {qMakePair("0°", 'N'), qMakePair("90°", 'R'), qMakePair("180°", 'I'), qMakePair("270°", 'B')})
        orientation->addItem(QString::fromUtf8(pair.first), QString(QChar::fromLatin1(pair.second)));
    orientation->setCurrentIndex(orientation->findData(QString(element.orientation)));
    orientation->setEnabled(!element.useDefaultOrientation); form->addRow(tr("Собственный поворот"), orientation);
    auto* direction = new QComboBox(this); direction->setObjectName(QStringLiteral("textDirection"));
    direction->addItem(tr("Горизонтально"), QStringLiteral("H"));
    direction->addItem(tr("Вертикально, буквы друг под другом"), QStringLiteral("V"));
    direction->addItem(tr("В обратном порядке"), QStringLiteral("R"));
    direction->setCurrentIndex(direction->findData(QString(element.textDirection))); form->addRow(tr("Направление букв"), direction);
    auto* spacing = new DotSpinBox(this); spacing->setObjectName(QStringLiteral("characterSpacing"));
    spacing->setRange(-10, 9999); spacing->setDisplayUnits(unit, dpi); spacing->setValue(element.characterSpacing);
    form->addRow(tr("Межбуквенный интервал"), spacing);
    spacing->setEnabled(element.textDirection != u'V');
    spacing->setToolTip(tr("Интервал действует для горизонтального и обратного направления; в вертикальном направлении шаг равен высоте буквы."));
    connect(direction, &QComboBox::currentIndexChanged, spacing, [direction, spacing] {
        spacing->setEnabled(direction->currentData().toString() != QStringLiteral("V"));
    });
    auto* error = new QLabel(this); error->setWordWrap(true); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this); layout->addWidget(buttons);
    connect(inherit, &QCheckBox::toggled, orientation, [orientation](bool value) { orientation->setEnabled(!value); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this, origin, anchor, inherit, orientation, direction, spacing, error] {
        element_.baselineOrigin = origin->currentData().toBool();
        element_.originJustification = anchor->currentData().toInt();
        element_.useDefaultOrientation = inherit->isChecked();
        element_.orientation = orientation->currentData().toString().front();
        element_.textDirection = direction->currentData().toString().front();
        element_.characterSpacing = spacing->value();
        DesignDocument check; check.elements = {element_}; check.elements.first().printable = true;
        if (const auto valid = validateForExport(check); !valid) { error->setText(valid.error()); return; }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
Element AdvancedTextDialog::editedElement() const { return element_; }
}
