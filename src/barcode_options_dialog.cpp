#include "barcode_options_dialog.hpp"
#include "dot_spin_box.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace Designer {
BarcodeOptionsDialog::BarcodeOptionsDialog(const Element& element, CanvasUnit unit, int dpi, QWidget* parent)
    : QDialog(parent), element_(element)
{
    setObjectName(QStringLiteral("barcodeOptionsDialog"));
    setWindowTitle(tr("Параметры %1").arg(elementTypeName(element.type)));
    resize(550, 360);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout; layout->addLayout(form);
    const auto number = [this, form, unit, dpi](const QString& label, const QString& name,
                                              int Element::*member, int minimum, int maximum, bool dots = false) {
        QSpinBox* spin;
        if (dots) {
            auto* dimension = new DotSpinBox(this); dimension->setDisplayUnits(unit, dpi); spin = dimension;
        } else spin = new QSpinBox(this);
        spin->setObjectName(name); spin->setRange(minimum, maximum); spin->setValue(element_.*member);
        spin->setKeyboardTracking(false);
        if (minimum == 0) spin->setSpecialValueText(tr("Автоматически / 0"));
        form->addRow(label, spin);
        connect(spin, &QSpinBox::valueChanged, this, [this, member](int value) { element_.*member = value; validateOptions(); });
        return spin;
    };
    const auto flag = [this, form](const QString& label, const QString& name, bool Element::*member) {
        auto* box = new QCheckBox(label, this); box->setObjectName(name); box->setChecked(element_.*member);
        form->addRow(box);
        connect(box, &QCheckBox::toggled, this, [this, member](bool value) { element_.*member = value; validateOptions(); });
    };
    const auto note = [this, layout](const QString& text) {
        auto* label = new QLabel(text, this); label->setWordWrap(true); layout->addWidget(label);
    };
    if (element.type != ElementType::MaxiCode)
        flag(tr("Использовать поворот этикетки по умолчанию (^FW)"), QStringLiteral("barcodeDefaultOrientation"), &Element::useDefaultOrientation);
    if (isLinearBarcodeType(element.type)) {
        if (element.type != ElementType::Code39 && element.type != ElementType::Codabar) {
            flag(tr("Подпись под штрихкодом"), QStringLiteral("barcodeCaption"), &Element::humanReadable);
            flag(tr("Подпись над штрихкодом"), QStringLiteral("barcodeCaptionAbove"), &Element::interpretationAbove);
        }
        if (element.type == ElementType::Code39 || element.type == ElementType::Codabar
            || element.type == ElementType::Interleaved2of5 || element.type == ElementType::Industrial2of5) {
            auto* ratio = new QDoubleSpinBox(this); ratio->setObjectName(QStringLiteral("barcodeRatio"));
            ratio->setRange(2, 3); ratio->setDecimals(2); ratio->setSingleStep(0.1); ratio->setValue(element.barcodeRatio);
            form->addRow(tr("Отношение широкого штриха к узкому"), ratio);
            connect(ratio, &QDoubleSpinBox::valueChanged, this, [this](double value) { element_.barcodeRatio = value; validateOptions(); });
        }
        if (element.type == ElementType::Code93 || element.type == ElementType::Interleaved2of5)
            flag(element.type == ElementType::Code93 ? tr("Показать контрольные символы в подписи") : tr("Добавить контрольную цифру Mod 10"),
                 QStringLiteral("barcodeCheckDigit"), &Element::barcodeCheckDigit);
        if (element.type == ElementType::Code128) {
            auto* mode = new QComboBox(this); mode->setObjectName(QStringLiteral("barcodeCode128Mode"));
            for (const QChar value : QStringView{u"NABCDU"}) mode->addItem(QString(value), QString(value));
            mode->setCurrentIndex(mode->findData(QString(element.code128Mode))); form->addRow(tr("Режим Code 128"), mode);
            connect(mode, &QComboBox::currentIndexChanged, this, [this, mode] {
                element_.code128Mode = mode->currentData().toString().front(); validateOptions();
            });
            flag(tr("Контрольная цифра UCC Mod 10"), QStringLiteral("barcodeUccCheck"), &Element::code128CheckDigit);
        }
        if (element.type == ElementType::Code39)
            note(tr("QtZpl поддерживает цифры, A–Z, пробел, дефис и точку. Подпись добавляется отдельным текстом."));
        if (element.type == ElementType::Codabar)
            note(tr("Используйте начальный/конечный символ A–D. Подпись добавляется отдельным текстом."));
    } else if (element.type == ElementType::DataMatrix) {
        flag(tr("Прямоугольный DataMatrix"), QStringLiteral("dataMatrixRectangular"), &Element::dataMatrixRectangular);
        number(tr("Колонки модулей (0 — авто)"), QStringLiteral("dataMatrixColumns"), &Element::dataMatrixColumns, 0, 144);
        number(tr("Строки модулей (0 — авто)"), QStringLiteral("dataMatrixRows"), &Element::dataMatrixRows, 0, 144);
        note(tr("ECC200. Прямоугольные размеры: 18×8, 32×8, 26×12, 36×12, 36×16, 48×16. Для автоматического выбора оставьте оба размера 0."));
    } else if (element.type == ElementType::Pdf417 || element.type == ElementType::MicroPdf417) {
        number(tr("Высота строки"), QStringLiteral("barcodeRowHeight"), &Element::pdf417RowHeight, 1, 64, true);
        if (element.type == ElementType::Pdf417) {
            number(tr("Коррекция ошибок"), QStringLiteral("barcodeSecurity"), &Element::pdf417Security, 0, 8);
            number(tr("Колонки (0 — авто)"), QStringLiteral("barcodeColumns"), &Element::pdf417Columns, 0, 30);
            number(tr("Строки (0 — авто, иначе 3–90)"), QStringLiteral("barcodeRows"), &Element::pdf417Rows, 0, 90);
            flag(tr("Усечённый PDF417"), QStringLiteral("barcodeTruncated"), &Element::pdf417Truncated);
        } else number(tr("Режим размера (0–33)"), QStringLiteral("microPdf417Mode"), &Element::microPdf417Mode, 0, 33);
        note(tr("Байтовые данные Latin-1. Ёмкость выбранного размера проверяется при построении превью."));
    } else if (element.type == ElementType::Gs1DataBar) {
        auto* type = new QComboBox(this); type->setObjectName(QStringLiteral("dataBarType"));
        const QStringList names{tr("Omnidirectional"), tr("Truncated"), tr("Stacked"), tr("Stacked Omnidirectional"), tr("Limited"), tr("Expanded / Expanded Stacked")};
        for (int index = 0; index < names.size(); ++index) type->addItem(names[index], index + 1);
        type->setCurrentIndex(element.dataBarType - 1); form->addRow(tr("Вариант"), type);
        connect(type, &QComboBox::currentIndexChanged, this, [this, type] { element_.dataBarType = type->currentData().toInt(); validateOptions(); });
        auto* segments = number(tr("Сегментов в строке Expanded (0 — одна строка)"), QStringLiteral("dataBarSegments"), &Element::dataBarSegments, 0, 22);
        segments->setSingleStep(2);
        number(tr("Высота разделителя"), QStringLiteral("dataBarSeparator"), &Element::dataBarSeparator, 1, 2);
        number(tr("Высота полос"), QStringLiteral("dataBarHeight"), &Element::height, 1, 1024, true);
        note(tr("Типы 1–5 используют стандартную высоту символа QtZpl. Expanded принимает строку AI с ASCII GS; {{GS}} вставляет разделитель. Composite 7–12 не поддерживается библиотекой."));
    } else if (element.type == ElementType::Aztec) {
        number(tr("Размер / коррекция"), QStringLiteral("aztecSize"), &Element::aztecSize, 1, 300);
        flag(tr("Инициализация считывателя"), QStringLiteral("aztecReaderInit"), &Element::aztecReaderInit);
        note(tr("1–99: минимальный процент коррекции; 101–104: compact layers; 201–232: full layers; 300: rune (число 0–255). ECI и structured append пока отсутствуют в QtZpl."));
    } else if (element.type == ElementType::MaxiCode) {
        number(tr("Режим"), QStringLiteral("maxiCodeMode"), &Element::maxiCodeMode, 2, 6);
        number(tr("Номер символа"), QStringLiteral("maxiCodePosition"), &Element::maxiCodePosition, 1, 8);
        number(tr("Всего символов"), QStringLiteral("maxiCodeTotal"), &Element::maxiCodeTotal, 1, 8);
        note(tr("Размер MaxiCode фиксирован. Режимы 2/3 ожидают первичный почтовый префикс и сообщение перевозчика; 4/5/6 кодируют байтовые данные поля."));
    } else if (element.type == ElementType::QrCode) {
        auto* correction = new QComboBox(this); correction->setObjectName(QStringLiteral("barcodeQrCorrection"));
        for (const QChar value : QStringView{u"LMQH"}) correction->addItem(QString(value), QString(value));
        correction->setCurrentIndex(correction->findData(QString(element.qrErrorCorrection))); form->addRow(tr("Коррекция ошибок"), correction);
        connect(correction, &QComboBox::currentIndexChanged, this, [this, correction] {
            element_.qrErrorCorrection = correction->currentData().toString().front(); validateOptions();
        });
    }
    status_ = new QLabel(this); status_->setWordWrap(true); layout->addWidget(status_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this); layout->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    validateOptions();
}

void BarcodeOptionsDialog::validateOptions()
{
    if (!status_ || !buttons_) return;
    DesignDocument document; document.elements = {element_}; document.elements.first().printable = true;
    const auto valid = validateForExport(document);
    status_->setText(valid ? tr("Все размеры задаются в точках принтера. После применения проверьте превью и диагностику.") : valid.error());
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid.has_value());
}

Element BarcodeOptionsDialog::editedElement() const { return element_; }
}
