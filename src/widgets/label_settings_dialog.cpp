#include "label_settings_dialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Designer {
LabelSettingsDialog::LabelSettingsDialog(const DesignDocument& document, QWidget* parent)
    : QDialog(parent), document_(document)
{
    setObjectName(QStringLiteral("labelSettingsDialog"));
    setWindowTitle(tr("Преобразования этикетки")); resize(570, 370);
    auto* layout = new QVBoxLayout(this); auto* form = new QFormLayout; layout->addLayout(form);
    auto* mirror = new QCheckBox(tr("Зеркально по горизонтали (PM)"), this);
    mirror->setObjectName(QStringLiteral("labelMirror")); mirror->setChecked(document.printMirror); form->addRow(mirror);
    auto* inverted = new QCheckBox(tr("Повернуть всю этикетку на 180° (PO)"), this);
    inverted->setObjectName(QStringLiteral("labelInverted")); inverted->setChecked(document.printInverted); form->addRow(inverted);
    auto* reverse = new QCheckBox(tr("Общая инверсия полей (LR)"), this);
    reverse->setObjectName(QStringLiteral("labelReverse")); reverse->setChecked(document.labelReverse); form->addRow(reverse);
    auto* orientation = new QComboBox(this); orientation->setObjectName(QStringLiteral("labelDefaultOrientation"));
    for (const auto& pair : {qMakePair("0°", 'N'), qMakePair("90°", 'R'), qMakePair("180°", 'I'), qMakePair("270°", 'B')})
        orientation->addItem(QString::fromUtf8(pair.first), QString(QChar::fromLatin1(pair.second)));
    orientation->setCurrentIndex(orientation->findData(QString(document.defaultOrientation)));
    form->addRow(tr("Поворот полей по умолчанию (FW)"), orientation);
    auto* anchor = new QComboBox(this); anchor->setObjectName(QStringLiteral("labelDefaultJustification"));
    anchor->addItem(tr("Слева"), 0); anchor->addItem(tr("Справа"), 1); anchor->addItem(tr("Автоматически"), 2);
    anchor->setCurrentIndex(anchor->findData(document.defaultJustification)); form->addRow(tr("Привязка полей по умолчанию"), anchor);
    auto* hint = new QLabel(tr("Отражение и поворот применяются ко всей этикетке. Координаты объектов сохраняются в исходной системе; холст показывает итоговое расположение. Настройки FW используются полями с включённым наследованием. Инверсия LR действует совместно с инверсией отдельных полей."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Применить"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this, mirror, inverted, reverse, orientation, anchor] {
        document_.printMirror = mirror->isChecked(); document_.printInverted = inverted->isChecked();
        document_.labelReverse = reverse->isChecked();
        document_.defaultOrientation = orientation->currentData().toString().front();
        document_.defaultJustification = anchor->currentData().toInt();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
DesignDocument LabelSettingsDialog::editedDocument() const { return document_; }
}
