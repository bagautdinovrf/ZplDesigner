#include "properties_panel.hpp"
#include "document_session.hpp"
#include "preview_coordinator.hpp"
#include "element_selection.hpp"
#include "element_presentation.hpp"
#include "eac_mark.hpp"
#include "selection_properties.hpp"
#include "workspace_widgets.hpp"
#include "dot_spin_box.hpp"
#include "gs1_data.hpp"
#include "value_placeholders.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace Designer {

PropertiesPanel::PropertiesPanel(DocumentSession* session, PreviewCoordinator* previews,
                                 ElementSelection* selection, QWidget* parent)
    : QWidget(parent), session_(session), previews_(previews), selection_(selection)
{
    setObjectName(QStringLiteral("propertiesPanel"));
    auto* properties = new WorkspaceSurface(this, false, false, false);
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->addWidget(properties); auto* propertiesLayout = new QVBoxLayout(properties);
    propertiesLayout->setContentsMargins(0, 0, 0, 0); propertiesLayout->setSpacing(0);
    auto* summary = new WorkspaceSurface(properties); auto* summaryLayout = new QVBoxLayout(summary); summaryLayout->setContentsMargins(13, 12, 13, 12); summaryLayout->setSpacing(10);
    summaryLayout->addWidget(heading(QStringLiteral("Выбранный объект"), summary));
    auto* summaryRow = new QHBoxLayout; summaryRow->setSpacing(9);
    objectSymbol_ = new ObjectBadge(QStringLiteral("T"), summary); objectSymbol_->setFixedSize(29, 29); objectSymbol_->setAlignment(Qt::AlignCenter); objectSymbol_->setForegroundRole(QPalette::Link);
    auto symbolFont = objectSymbol_->font(); symbolFont.setPixelSize(15); objectSymbol_->setFont(symbolFont);
    auto* objectText = new QVBoxLayout; objectText->setSpacing(2);
    // A long element name elides instead of pushing the form past the inspector edge.
    typeTitle_ = heading(QStringLiteral("Выберите элемент"), summary, true); objectText->addWidget(typeTitle_);
    objectSubtitle_ = caption(QString(), summary); objectSubtitle_->setWordWrap(true); objectText->addWidget(objectSubtitle_);
    summaryRow->addWidget(objectSymbol_); summaryRow->addLayout(objectText, 1); summaryLayout->addLayout(summaryRow); propertiesLayout->addWidget(summary);
    propertyUnits_ = new QComboBox(properties);
    propertyUnits_->setObjectName(QStringLiteral("propertyUnits"));
    propertyUnits_->addItem(QStringLiteral("мм"), static_cast<int>(CanvasUnit::Millimeters));
    propertyUnits_->addItem(QStringLiteral("dots"), static_cast<int>(CanvasUnit::Dots));
    propertyUnits_->setFixedSize(60, 25); propertyUnits_->setProperty("compact", true);
    connect(propertyUnits_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_) return;
        emit unitsRequested(static_cast<CanvasUnit>(propertyUnits_->currentData().toInt()));
    });
    elementForm_ = new QWidget(properties); auto* elementLayout = new QVBoxLayout(elementForm_); elementLayout->setContentsMargins(0, 0, 0, 0); elementLayout->setSpacing(0);
    auto* geometry = new WorkspaceSurface(elementForm_); auto* geometryLayout = new QVBoxLayout(geometry); geometryLayout->setContentsMargins(13, 12, 13, 12); geometryLayout->setSpacing(10);
    auto* geometryTitle = new QHBoxLayout; geometryTitle->addWidget(sectionHeading(QStringLiteral("Положение и размер"), geometry)); geometryTitle->addStretch(); geometryTitle->addWidget(propertyUnits_); geometryLayout->addLayout(geometryTitle);
    auto* geometryGrid = new QGridLayout; geometryGrid->setSpacing(10); geometryGrid->setColumnStretch(0, 1); geometryGrid->setColumnStretch(1, 1);
    geometryLayout->addLayout(geometryGrid); elementLayout->addWidget(geometry);
    auto* contentSection = new WorkspaceSurface(elementForm_); contentSection_ = contentSection;
    auto* contentLayout = new QVBoxLayout(contentSection); contentLayout->setContentsMargins(13, 12, 13, 12); contentLayout->setSpacing(10);
    contentLayout->addWidget(sectionHeading(QStringLiteral("Содержимое"), contentSection));
    auto* contentGrid = new QGridLayout; contentGrid->setSpacing(10); contentGrid->setColumnStretch(0, 1); contentGrid->setColumnStretch(1, 1);
    contentLayout->addLayout(contentGrid); elementLayout->addWidget(contentSection);
    auto* advanced = new QWidget(elementForm_); advanced->setObjectName(QStringLiteral("advancedProperties"));
    auto* form = new QFormLayout(advanced); form->setObjectName(QStringLiteral("detailForm"));
    form->setContentsMargins(13, 12, 13, 12); form->setSpacing(8); form->setRowWrapPolicy(QFormLayout::WrapAllRows); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    barcodeType_ = new QComboBox(properties);
    barcodeType_->setObjectName(QStringLiteral("barcodeType"));
    contentLayout->insertWidget(1, propertyField(QStringLiteral("Тип кода"), barcodeType_, contentSection));
    connect(barcodeType_, &QComboBox::currentIndexChanged, this, &PropertiesPanel::editBarcodeType);
    eacStyle_ = new QComboBox(properties);
    eacStyle_->setObjectName(QStringLiteral("eacStyle"));
    eacStyle_->addItem(QStringLiteral("Чёрный на белом"), static_cast<int>(EacStyle::BlackOnWhite));
    eacStyle_->addItem(QStringLiteral("Белый на чёрном"), static_cast<int>(EacStyle::WhiteOnBlack));
    eacStyle_->addItem(QStringLiteral("Чёрный без фона"), static_cast<int>(EacStyle::BlackTransparent));
    eacStyle_->addItem(QStringLiteral("Белый без фона"), static_cast<int>(EacStyle::WhiteTransparent));
    contentLayout->insertWidget(1, propertyField(QStringLiteral("Оформление"), eacStyle_, contentSection));
    connect(eacStyle_, &QComboBox::currentIndexChanged, this, &PropertiesPanel::editProperties);
    x_ = dotSpin(QStringLiteral("elementX"), -4096, 4096, properties); geometryGrid->addWidget(propertyField(QStringLiteral("X, dots"), x_, geometry), 0, 0);
    y_ = dotSpin(QStringLiteral("elementY"), -4096, 4096, properties); geometryGrid->addWidget(propertyField(QStringLiteral("Y, dots"), y_, geometry), 0, 1);
    width_ = dotSpin(QStringLiteral("elementWidth"), 1, 4096, properties); geometryGrid->addWidget(propertyField(QStringLiteral("Ширина"), width_, geometry), 1, 0);
    height_ = dotSpin(QStringLiteral("elementHeight"), 1, 4096, properties); geometryGrid->addWidget(propertyField(QStringLiteral("Высота"), height_, geometry), 1, 1);
    geometryAspect_ = new QCheckBox(QStringLiteral("Сохранять пропорции"), geometry);
    auto aspectFont = geometryAspect_->font(); aspectFont.setPixelSize(11); geometryAspect_->setFont(aspectFont);
    geometryLayout->addWidget(geometryAspect_);
    connect(geometryAspect_, &QCheckBox::clicked, this, [this](bool checked) {
        if (!updating_ && selected_ >= 0 && session_->document().elements[selected_].type == ElementType::Image)
            imageKeepAspectRatio_->setChecked(checked);
    });
    sizeHint_ = new QLabel(properties); sizeHint_->setWordWrap(true);
    sizeHint_->setObjectName(QStringLiteral("elementSizeHint")); form->addRow(sizeHint_);
    content_ = new QPlainTextEdit(properties); content_->setObjectName(QStringLiteral("elementContent"));
    content_->setFixedHeight(30); content_->setTabChangesFocus(true); contentGrid->addWidget(propertyField(QStringLiteral("Содержимое"), content_, contentSection), 0, 0, 1, 2);
    gs1_ = new QCheckBox(QStringLiteral("GS1 DataMatrix (FNC1)"), properties);
    auto primaryRow = [contentLayout, contentSection](QWidget* control) {
        auto* row = new QWidget(contentSection); auto* layout = new QVBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0); layout->addWidget(control);
        control->setProperty("propertyField", QVariant::fromValue<QObject*>(row)); contentLayout->addWidget(row);
    };
    eacHint_ = caption(QString(), properties);
    eacHint_->setObjectName(QStringLiteral("eacHint"));
    eacHint_->setWordWrap(true);
    primaryRow(eacHint_);
    gs1_->setObjectName(QStringLiteral("dataMatrixGs1")); primaryRow(gs1_);
    gs1Builder_ = new QPushButton(QStringLiteral("Мастер GS1…"), properties);
    gs1Builder_->setObjectName(QStringLiteral("gs1Builder")); primaryRow(gs1Builder_);
    placeholderValues_ = new QPushButton(QStringLiteral("Значения плейсхолдеров…"), properties);
    placeholderValues_->setObjectName(QStringLiteral("placeholderValues"));
    placeholderValues_->setToolTip(QStringLiteral("Значения полей {{01}}, {{21}} и параметров [[имя]] для превью и заполненного ZPL"));
    primaryRow(placeholderValues_);
    connect(placeholderValues_, &QPushButton::clicked, this, &PropertiesPanel::valuesRequested);
    gs1Status_ = new QLabel(properties); gs1Status_->setWordWrap(true);
    gs1Status_->setObjectName(QStringLiteral("gs1Status")); primaryRow(gs1Status_);
    connect(gs1Builder_, &QPushButton::clicked, this, &PropertiesPanel::gs1Requested);
    fontHeight_ = dotSpin(QStringLiteral("fontHeight"), 1, 512, properties); contentGrid->addWidget(propertyField(QStringLiteral("Высота шрифта, dots"), fontHeight_, contentSection), 1, 0);
    fontWidth_ = dotSpin(QStringLiteral("fontWidth"), 0, 512, properties); fontWidth_->setSpecialValueText(QStringLiteral("Авто"));
    form->addRow(QStringLiteral("Ширина букв"), fontWidth_);
    blockWidth_ = dotSpin(QStringLiteral("blockWidth"), 0, 4096, properties);
    blockWidth_->setSpecialValueText(QStringLiteral("Без блока"));
    form->addRow(QStringLiteral("Ширина блока"), blockWidth_);
    blockLines_ = spin(QStringLiteral("blockMaxLines"), 1, 128, properties);
    form->addRow(QStringLiteral("Строк в блоке"), blockLines_);
    blockSpacing_ = dotSpin(QStringLiteral("blockLineSpacing"), -512, 512, properties);
    form->addRow(QStringLiteral("Интервал"), blockSpacing_);
    blockIndent_ = dotSpin(QStringLiteral("blockHangingIndent"), -4096, 4096, properties);
    form->addRow(QStringLiteral("Отступ"), blockIndent_);
    blockAlignment_ = new QComboBox(properties); blockAlignment_->setObjectName(QStringLiteral("blockAlignment"));
    blockAlignment_->addItem(QStringLiteral("Слева"), QStringLiteral("L"));
    blockAlignment_->addItem(QStringLiteral("По центру"), QStringLiteral("C"));
    blockAlignment_->addItem(QStringLiteral("Справа"), QStringLiteral("R"));
    blockAlignment_->addItem(QStringLiteral("По ширине"), QStringLiteral("J"));
    form->addRow(QStringLiteral("Выравнивание"), blockAlignment_);
    code128Mode_ = new QComboBox(properties); code128Mode_->setObjectName(QStringLiteral("code128Mode"));
    code128Mode_->addItem(QStringLiteral("Обычный (N)"), QStringLiteral("N"));
    code128Mode_->addItem(QStringLiteral("Автоматический (A)"), QStringLiteral("A"));
    code128Mode_->addItem(QStringLiteral("Subset B"), QStringLiteral("B"));
    code128Mode_->addItem(QStringLiteral("Subset C"), QStringLiteral("C"));
    code128Mode_->addItem(QStringLiteral("GS1-128 (D)"), QStringLiteral("D"));
    code128Mode_->addItem(QStringLiteral("UCC (U)"), QStringLiteral("U"));
    form->addRow(QStringLiteral("Режим Code 128"), code128Mode_);
    // ^BK k and l: Codabar data never carries its start and stop characters.
    const auto codabarCharacter = [&](QComboBox*& combo, const QString& name, const QString& label) {
        combo = new QComboBox(properties); combo->setObjectName(name);
        for (const QChar value : QStringView{u"ABCD"}) combo->addItem(QString(value), QString(value));
        form->addRow(label, combo);
    };
    codabarCharacter(codabarStart_, QStringLiteral("codabarStart"), QStringLiteral("Старт Codabar"));
    codabarCharacter(codabarStop_, QStringLiteral("codabarStop"), QStringLiteral("Стоп Codabar"));
    interpretationAbove_ = new QCheckBox(QStringLiteral("Подпись над кодом"), properties);
    interpretationAbove_->setObjectName(QStringLiteral("interpretationAbove"));
    form->addRow(interpretationAbove_);
    thickness_ = dotSpin(QStringLiteral("thickness"), 1, 512, properties); contentGrid->addWidget(propertyField(QStringLiteral("Толщина, dots"), thickness_, contentSection), 1, 0);
    module_ = dotSpin(QStringLiteral("moduleWidth"), 1, 10, properties); contentGrid->addWidget(propertyField(QStringLiteral("Модуль, dots"), module_, contentSection), 1, 0);
    orientation_ = new QComboBox(properties); orientation_->setObjectName(QStringLiteral("orientation"));
    for (const auto& pair : {qMakePair("0°", 'N'), qMakePair("90°", 'R'), qMakePair("180°", 'I'), qMakePair("270°", 'B')})
        orientation_->addItem(QString::fromUtf8(pair.first), QString(QChar::fromLatin1(pair.second)));
    contentGrid->addWidget(propertyField(QStringLiteral("Поворот"), orientation_, contentSection), 1, 1);
    readable_ = new QCheckBox(QStringLiteral("Текст под штрихкодом"), properties); form->addRow(readable_);
    reverse_ = new QCheckBox(QStringLiteral("Инверсия поля"), properties); form->addRow(reverse_);
    whiteGraphic_ = new QCheckBox(QStringLiteral("Белая фигура"), properties);
    whiteGraphic_->setObjectName(QStringLiteral("whiteGraphic")); form->addRow(whiteGraphic_);
    connect(whiteGraphic_, &QCheckBox::toggled, this, &PropertiesPanel::editProperties);
    diagonalDirection_ = new QComboBox(properties); diagonalDirection_->setObjectName(QStringLiteral("diagonalDirection"));
    diagonalDirection_->addItem(QStringLiteral("Восходящая /"), QStringLiteral("R"));
    diagonalDirection_->addItem(QStringLiteral("Нисходящая \\"), QStringLiteral("L"));
    form->addRow(QStringLiteral("Диагональ"), diagonalDirection_);
    connect(diagonalDirection_, &QComboBox::currentIndexChanged, this, &PropertiesPanel::editProperties);
    threshold_ = spin(QStringLiteral("imageThreshold"), 0, 255, properties); form->addRow(QStringLiteral("Порог чёрного"), threshold_);
    dither_ = new QCheckBox(QStringLiteral("Дизеринг Floyd–Steinberg"), properties); form->addRow(dither_);
    imageKeepAspectRatio_ = new QCheckBox(QStringLiteral("Сохранять пропорции"), properties);
    imageKeepAspectRatio_->setObjectName(QStringLiteral("imageKeepAspectRatio")); form->addRow(imageKeepAspectRatio_);
    imageEdit_ = new QPushButton(QStringLiteral("Изменить изображение…"), properties);
    imageEdit_->setToolTip(QStringLiteral("Обрезка, поворот и отражение изображения"));
    imageEdit_->setObjectName(QStringLiteral("editImage")); form->addRow(imageEdit_);
    connect(imageEdit_, &QPushButton::clicked, this, &PropertiesPanel::imageRequested);
    tableEdit_ = new QPushButton(QStringLiteral("Ячейки таблицы…"), properties);
    tableEdit_->setObjectName(QStringLiteral("editTable")); form->addRow(tableEdit_);
    connect(tableEdit_, &QPushButton::clicked, this, &PropertiesPanel::tableRequested);
    barcodeOptions_ = new QPushButton(QStringLiteral("Параметры кода…"), properties);
    barcodeOptions_->setObjectName(QStringLiteral("barcodeOptions")); form->addRow(barcodeOptions_);
    connect(barcodeOptions_, &QPushButton::clicked, this, &PropertiesPanel::barcodeRequested);
    advancedText_ = new QPushButton(QStringLiteral("Направление текста…"), properties);
    advancedText_->setToolTip(QStringLiteral("Положение, базовая линия и направление текста"));
    advancedText_->setObjectName(QStringLiteral("advancedText")); form->addRow(advancedText_);
    connect(advancedText_, &QPushButton::clicked, this, &PropertiesPanel::textRequested);
    qrCorrection_ = new QComboBox(properties); qrCorrection_->setObjectName(QStringLiteral("qrCorrection"));
    for (const auto& level : {QStringLiteral("L"), QStringLiteral("M"), QStringLiteral("Q"), QStringLiteral("H")})
        qrCorrection_->addItem(level, level);
    form->addRow(QStringLiteral("Коррекция QR"), qrCorrection_);
    qrPayload_ = new QPushButton(QStringLiteral("Содержимое QR…"), properties);
    qrPayload_->setToolTip(QStringLiteral("Ссылка, Wi-Fi или контакт"));
    qrPayload_->setObjectName(QStringLiteral("qrPayload")); form->addRow(qrPayload_);
    connect(qrPayload_, &QPushButton::clicked, this, &PropertiesPanel::qrRequested);
    connect(qrCorrection_, &QComboBox::currentIndexChanged, this, &PropertiesPanel::editProperties);
    pdfRowHeight_ = dotSpin(QStringLiteral("pdfRowHeight"), 1, 64, properties);
    pdfColumns_ = spin(QStringLiteral("pdfColumns"), 0, 30, properties); pdfColumns_->setSpecialValueText(QStringLiteral("Авто"));
    pdfSecurity_ = spin(QStringLiteral("pdfSecurity"), 0, 8, properties);
    form->addRow(QStringLiteral("Высота строки PDF417"), pdfRowHeight_);
    form->addRow(QStringLiteral("Колонки PDF417"), pdfColumns_);
    form->addRow(QStringLiteral("Коррекция PDF417"), pdfSecurity_);
    for (auto* control : {pdfRowHeight_, pdfColumns_, pdfSecurity_})
        connect(control, &QSpinBox::valueChanged, this, &PropertiesPanel::editProperties);
    auto* alignment = new WorkspaceSurface(elementForm_); auto* alignmentLayout = new QVBoxLayout(alignment); alignmentLayout->setContentsMargins(13, 12, 13, 12); alignmentLayout->setSpacing(10);
    alignmentLayout->addWidget(sectionHeading(QStringLiteral("Выравнивание"), alignment)); auto* alignmentRow = new QHBoxLayout; alignmentRow->setSpacing(4);
    const QStringList alignNames{QStringLiteral("alignLeft"), QStringLiteral("alignHorizontalCenter"), QStringLiteral("alignRight")};
    const QStringList alignTexts{QStringLiteral("Слева"), QStringLiteral("Центр"), QStringLiteral("Справа")};
    for (int i = 0; i < 3; ++i) { auto* button = new QPushButton(alignTexts[i], alignment); button->setProperty("compact", true); alignmentRow->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name = alignNames[i]] { emit alignmentRequested(name); }); }
    alignmentLayout->addLayout(alignmentRow); elementLayout->addWidget(alignment);
    auto* more = new QToolButton(elementForm_); more->setObjectName(QStringLiteral("advancedPropertiesToggle")); more->setText(QStringLiteral("Дополнительные параметры")); more->setCheckable(true); more->setProperty("flat", true); more->setArrowType(Qt::RightArrow); more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    elementLayout->addWidget(more); elementLayout->addWidget(advanced); advanced->hide();
    connect(more, &QToolButton::toggled, advanced, &QWidget::setVisible);
    connect(more, &QToolButton::toggled, more, [more](bool open) { more->setArrowType(open ? Qt::DownArrow : Qt::RightArrow); });
    propertiesLayout->addWidget(elementForm_);
    selectionProperties_ = new SelectionProperties(properties);
    propertiesLayout->addWidget(selectionProperties_);
    connect(selectionProperties_, &SelectionProperties::changesRequested, this, [this](const SelectionPropertyValues& values) {
        emit editRequested(UpdateSelectionProperties{selection_->ids(), values}, QStringLiteral("Общие свойства выделения"));
    });
    auto* actions = new QHBoxLayout; actions->setContentsMargins(13, 12, 13, 12);
    auto* duplicate = new QPushButton(QStringLiteral("Дублировать"), properties);
    auto* remove = new QPushButton(QStringLiteral("Удалить"), properties); actions->addWidget(duplicate); actions->addWidget(remove);
    propertiesLayout->addLayout(actions); propertiesLayout->addStretch();
    connect(duplicate, &QPushButton::clicked, this, &PropertiesPanel::duplicateRequested);
    connect(remove, &QPushButton::clicked, this, &PropertiesPanel::deleteRequested);
    for (auto* value : {x_, y_, fontHeight_, fontWidth_, thickness_, module_, threshold_,
                       blockWidth_, blockLines_, blockSpacing_, blockIndent_})
        connect(value, &QSpinBox::valueChanged, this, &PropertiesPanel::editProperties);
    auto resizeProperty = [this](bool horizontal, int value) {
        if (updating_ || selected_ < 0) return;
        if (previews_->pending()) previews_->refresh();
        QSize size = elementSize(selected_);
        if (horizontal) size.setWidth(value); else size.setHeight(value);
        emit resizeRequested(size);
    };
    connect(width_, &QSpinBox::valueChanged, this, [resizeProperty](int value) { resizeProperty(true, value); });
    connect(height_, &QSpinBox::valueChanged, this, [resizeProperty](int value) { resizeProperty(false, value); });
    connect(content_, &QPlainTextEdit::textChanged, this, &PropertiesPanel::editProperties);
    connect(orientation_, &QComboBox::currentIndexChanged, this, &PropertiesPanel::editProperties);
    connect(blockAlignment_, &QComboBox::currentIndexChanged, this, &PropertiesPanel::editProperties);
    connect(code128Mode_, &QComboBox::currentIndexChanged, this, &PropertiesPanel::editProperties);
    for (auto* combo : {codabarStart_, codabarStop_})
        connect(combo, &QComboBox::currentIndexChanged, this, &PropertiesPanel::editProperties);
    connect(interpretationAbove_, &QCheckBox::toggled, this, &PropertiesPanel::editProperties);
    for (auto* value : {readable_, reverse_, dither_, imageKeepAspectRatio_}) connect(value, &QCheckBox::toggled, this, &PropertiesPanel::editProperties);
    connect(gs1_, &QCheckBox::toggled, this, [this] {
        if (updating_ || selectedIndices().size() != 1 || !session_->document().visualEditable
            || selected_ < 0 || selected_ >= session_->document().elements.size()) return;
        const auto& element = session_->document().elements[selected_];
        if (element.type != ElementType::DataMatrix) return;
        if (session_->hasPendingSource()) {
            const QSignalBlocker blocker(gs1_);
            gs1_->setChecked(element.gs1);
            emit noticeRequested(QStringLiteral("Перейдите в «Дизайн» или «Превью», чтобы применить изменённый ZPL."));
            return;
        }
        editProperties();
    });
}

void PropertiesPanel::refresh() {
    updating_ = true;
    selected_ = selectedIndex();
    barcodeType_->setEnabled(!session_->hasPendingSource());
    refreshUnits();
    const bool multi = selectedIndices().size() > 1;
    const bool valid = !multi && session_->document().visualEditable && selected_ >= 0 && selected_ < session_->document().elements.size();
    elementForm_->setEnabled(valid && !selectionLocked() && !session_->hasPendingSource());
    elementForm_->setVisible(valid);
    selectionProperties_->setVisible(multi && session_->document().visualEditable);
    selectionProperties_->setEnabled(!selectionLocked() && !session_->hasPendingSource());
    if (multi) selectionProperties_->setSelection(session_->document(), selectedIndices());
    typeTitle_->setText(multi ? QStringLiteral("Выбрано элементов: %1").arg(selectedIndices().size())
        : valid ? (session_->document().elements[selected_].name.isEmpty() ? elementTypeName(session_->document().elements[selected_].type) : session_->document().elements[selected_].name) : QStringLiteral("Выберите элемент"));
    typeTitle_->setToolTip(typeTitle_->text());
    objectSubtitle_->setText(multi ? QStringLiteral("Выравнивание — в меню «Компоновка»")
        : valid ? QStringLiteral("%1 · %2 из %3").arg(elementTypeName(session_->document().elements[selected_].type)).arg(session_->document().elements.size() - selected_).arg(session_->document().elements.size()) : QString());
    objectSymbol_->setText(valid ? elementSymbol(session_->document().elements[selected_].type) : QStringLiteral("↖"));
    if (valid) {
        const auto& e = session_->document().elements[selected_];
        const QSize size = elementSize(selected_);
        const bool eac = e.type == ElementType::Eac;
        const int minimumSize = eac ? eacMinimumSize(session_->document().dpi, e.eacStyle) : 1;
        width_->setMinimum(minimumSize); height_->setMinimum(minimumSize);
        width_->setSingleStep(eac ? eacGridUnits(e.eacStyle) : 1);
        height_->setSingleStep(eac ? eacGridUnits(e.eacStyle) : 1);
        x_->setValue(e.x); y_->setValue(e.y); width_->setValue(size.width()); height_->setValue(size.height());
        eacStyle_->setCurrentIndex(eacStyle_->findData(static_cast<int>(e.eacStyle)));
        if (content_->toPlainText() != e.text) content_->setPlainText(e.text);
        content_->setFixedHeight(e.text.contains(u'\n') || e.blockWidth > 0 ? 90 : 30);
        fontHeight_->setValue(e.fontHeight); fontWidth_->setValue(e.fontWidth); thickness_->setValue(e.thickness);
        module_->setValue(e.moduleWidth); threshold_->setValue(e.threshold);
        orientation_->setCurrentIndex(orientation_->findData(QString(e.orientation)));
        readable_->setChecked(e.humanReadable); reverse_->setChecked(e.reverse); dither_->setChecked(e.dither);
        whiteGraphic_->setChecked(e.graphicWhite);
        diagonalDirection_->setCurrentIndex(diagonalDirection_->findData(QString(e.diagonalDirection)));
        imageKeepAspectRatio_->setChecked(e.imageKeepAspectRatio);
        gs1_->setChecked(e.gs1);
        blockWidth_->setValue(e.blockWidth); blockLines_->setValue(e.blockMaxLines);
        blockSpacing_->setValue(e.blockLineSpacing); blockIndent_->setValue(e.blockHangingIndent);
        blockAlignment_->setCurrentIndex(blockAlignment_->findData(QString(e.blockJustification)));
        code128Mode_->setCurrentIndex(code128Mode_->findData(QString(e.code128Mode)));
        codabarStart_->setCurrentIndex(codabarStart_->findData(QString(e.codabarStart)));
        codabarStop_->setCurrentIndex(codabarStop_->findData(QString(e.codabarStop)));
        qrCorrection_->setCurrentIndex(qrCorrection_->findData(QString(e.qrErrorCorrection)));
        pdfRowHeight_->setValue(e.pdf417RowHeight); pdfColumns_->setValue(e.pdf417Columns); pdfSecurity_->setValue(e.pdf417Security);
        interpretationAbove_->setChecked(e.interpretationAbove);
        const bool text = e.type == ElementType::Text;
        const bool code = isBarcodeType(e.type);
        const bool table = e.type == ElementType::Table;
        const bool image = e.type == ElementType::Image;
        geometryAspect_->setEnabled(image && !selectionLocked() && !session_->hasPendingSource());
        geometryAspect_->setChecked(image ? e.imageKeepAspectRatio
            : eac || e.type == ElementType::Circle || e.type == ElementType::QrCode || e.type == ElementType::DataMatrix);
        auto* form = elementForm_->findChild<QFormLayout*>(QStringLiteral("detailForm"));
        setPropertyRowVisible(form, eacStyle_, eac);
        setPropertyRowVisible(form, eacHint_, eac);
        if (eac) {
            eacHint_->setText(QStringLiteral("По решению КТС № 711: знак не менее 5 мм, пропорции фиксированы. "
                "Шаг размера: %1 dots. DPI макета (%2) должен совпадать с DPI печати. Обеспечьте контраст с фоном.")
                .arg(eacGridUnits(e.eacStyle)).arg(session_->document().dpi)
                + (e.eacStyle == EacStyle::WhiteOnBlack
                    ? QStringLiteral("\nРазмер включает чёрное поле вокруг знака.") : QString()));
            eacStyle_->setToolTip(QStringLiteral("Варианты «без фона» не добавляют фоновый квадрат. "
                "Для белого знака нужен тёмный фон."));
        }
        setPropertyRowVisible(form, barcodeType_, code);
        if (code) {
            const QSignalBlocker blocker(barcodeType_);
            const auto types = availableBarcodeTypes(e.type);
            if (barcodeType_->count() != static_cast<int>(types.size())
                || barcodeType_->itemData(0).toInt() != static_cast<int>(types.front())) {
                barcodeType_->clear();
                for (const auto type : types) barcodeType_->addItem(elementTypeName(type), static_cast<int>(type));
            }
            barcodeType_->setCurrentIndex(barcodeType_->findData(static_cast<int>(e.type)));
        }
        contentSection_->setVisible(!image && !table);
        setPropertyRowVisible(form, content_, text || code);
        const bool dataMatrix = e.type == ElementType::DataMatrix;
        setPropertyRowVisible(form, gs1_, dataMatrix); setPropertyRowVisible(form, gs1Builder_, dataMatrix);
        setPropertyRowVisible(form, placeholderValues_, table);
        setPropertyRowVisible(form, gs1Status_, false);
        content_->setPlaceholderText(dataMatrix ? (e.gs1 ? QStringLiteral("(01){{01}}(21){{21}} или 01{{01}}21{{21}}{{GS}}91{{91}}")
                                                                        : QStringLiteral("{{21}} или PREFIX-[[serial]]")) : QString());
        if (text || code) {
            const auto names = e.literalText ? std::expected<QStringList, QString>(QStringList{}) : requiredValueParameters(e.text);
            const auto gs1Data = dataMatrix && e.gs1 ? parseGs1Template(e.text) : std::expected<Gs1Data, QString>(Gs1Data{});
            setPropertyRowVisible(form, placeholderValues_, !names || !names->isEmpty());
            setPropertyRowVisible(form, gs1Status_, !names || !gs1Data || previews_->editorPreview().elementErrors.contains(selected_));
            if (!names) gs1Status_->setText(names.error());
            else if (!gs1Data) gs1Status_->setText(QStringLiteral("GS1 включён. %1\nИсправьте данные или используйте «Мастер GS1…».").arg(gs1Data.error()));
            else if (previews_->editorPreview().elementErrors.contains(selected_)) gs1Status_->setText(previews_->editorPreview().elementErrors.value(selected_));
            else if (!names->isEmpty()) gs1Status_->setText(QStringLiteral("Плейсхолдеры: %1. Задайте «Значения плейсхолдеров…» для превью. Сохранение оставляет шаблон.").arg(names->join(QStringLiteral(", "))));
            else gs1Status_->setText(e.gs1 ? QStringLiteral("Поля GS1 корректны. FNC1 и разделители добавляются автоматически.")
                                          : QStringLiteral("В данных можно использовать {{имя}} и [[имя]]."));
        }
        setPropertyRowVisible(form, fontHeight_, text); setPropertyRowVisible(form, fontWidth_, text);
        setPropertyRowVisible(form, blockWidth_, text);
        for (QWidget* widget : {static_cast<QWidget*>(blockLines_), static_cast<QWidget*>(blockSpacing_),
                               static_cast<QWidget*>(blockIndent_), static_cast<QWidget*>(blockAlignment_)})
            setPropertyRowVisible(form, widget, text && e.blockWidth > 0);
        setPropertyRowVisible(form, code128Mode_, e.type == ElementType::Code128);
        setPropertyRowVisible(form, codabarStart_, e.type == ElementType::Codabar);
        setPropertyRowVisible(form, codabarStop_, e.type == ElementType::Codabar);
        setPropertyRowVisible(form, interpretationAbove_, isLinearBarcodeType(e.type) && e.humanReadable
            && e.type != ElementType::Codabar && e.type != ElementType::Code39);
        setPropertyRowVisible(form, thickness_, !text && !code && !image && !table && !eac);
        setPropertyRowVisible(form, whiteGraphic_, !text && !code && !image && !table && !eac);
        setPropertyRowVisible(form, diagonalDirection_, e.type == ElementType::Diagonal);
        setPropertyRowVisible(form, reverse_, !table && !eac);
        setPropertyRowVisible(form, module_, code && e.type != ElementType::MaxiCode);
        setPropertyRowVisible(form, orientation_, (text || code) && e.type != ElementType::MaxiCode);
        orientation_->setEnabled(!e.useDefaultOrientation);
        setPropertyRowVisible(form, readable_, isLinearBarcodeType(e.type)
            && e.type != ElementType::Codabar && e.type != ElementType::Code39);
        setPropertyRowVisible(form, qrCorrection_, e.type == ElementType::QrCode);
        setPropertyRowVisible(form, qrPayload_, e.type == ElementType::QrCode);
        setPropertyRowVisible(form, tableEdit_, table);
        setPropertyRowVisible(form, barcodeOptions_, code);
        setPropertyRowVisible(form, advancedText_, text);
        for (auto* field : {pdfRowHeight_, pdfColumns_, pdfSecurity_}) setPropertyRowVisible(form, field, e.type == ElementType::Pdf417);
        setPropertyRowVisible(form, threshold_, image); setPropertyRowVisible(form, dither_, image);
        setPropertyRowVisible(form, imageKeepAspectRatio_, image); setPropertyRowVisible(form, imageEdit_, image);
        setPropertyRowVisible(form, width_, true);
        setPropertyRowVisible(form, height_, true);
        width_->setEnabled(e.type != ElementType::MaxiCode);
        height_->setEnabled(e.type != ElementType::MaxiCode);
        sizeHint_->setText(text ? (e.blockWidth > 0
            ? QStringLiteral("Размер меняет шрифт и текстовый блок. Точная ширина блока задаётся ниже.")
            : QStringLiteral("Размер меняет шрифт. Фактические размеры округляются до точек."))
            : code ? QStringLiteral("Модуль: %1 мм. Размер меняется целыми модулями; пропорции QR и DataMatrix сохраняются.")
                .arg(e.moduleWidth * 25.4 / session_->document().dpi, 0, 'f', 3)
            : eac ? QStringLiteral("Ширина и высота изменяются вместе. Минимальный размер зависит от DPI печати.")
            : e.type == ElementType::Circle ? QStringLiteral("Ширина и высота задают один диаметр.")
            : QStringLiteral("Меняйте размеры здесь или тяните маркеры рамки на холсте."));
        sizeHint_->setText(sizeHint_->text() + QStringLiteral("\nФактически: %1 × %2 dots = %3 × %4 мм.")
            .arg(size.width()).arg(size.height()).arg(size.width() * 25.4 / session_->document().dpi, 0, 'f', 3)
            .arg(size.height() * 25.4 / session_->document().dpi, 0, 'f', 3));
    }
    updating_ = false;
}


void PropertiesPanel::editBarcodeType() {
    if (updating_ || selectedIndices().size() != 1 || selected_ < 0
        || selected_ >= session_->document().elements.size() || barcodeType_->currentIndex() < 0) return;
    emit editRequested(ChangeBarcodeType{session_->document().elements[selected_].id,
        static_cast<ElementType>(barcodeType_->currentData().toInt())}, QStringLiteral("Тип штрихкода"));
}


void PropertiesPanel::editProperties() {
    if (updating_ || selectionLocked() || selectedIndices().size() != 1 || !session_->document().visualEditable || selected_ < 0 || selected_ >= session_->document().elements.size()) return;
    ElementProperties e;
    e.x = x_->value(); e.y = y_->value();
    if (session_->document().elements[selected_].type == ElementType::Eac) {
        e.eacStyle = static_cast<EacStyle>(eacStyle_->currentData().toInt());
        emit editRequested(UpdateElementProperties{session_->document().elements[selected_].id, std::move(e)},
            QStringLiteral("Изменить свойства EAC"));
        return;
    }
    e.text = content_->toPlainText(); e.fontHeight = fontHeight_->value(); e.fontWidth = fontWidth_->value();
    e.thickness = thickness_->value(); e.moduleWidth = module_->value(); e.threshold = threshold_->value();
    e.orientation = orientation_->currentData().toString().at(0);
    e.humanReadable = readable_->isChecked(); e.reverse = reverse_->isChecked(); e.dither = dither_->isChecked();
    e.graphicWhite = whiteGraphic_->isChecked();
    e.diagonalDirection = diagonalDirection_->currentData().toString().at(0);
    e.imageKeepAspectRatio = imageKeepAspectRatio_->isChecked();
    e.gs1 = gs1_->isChecked();
    e.blockWidth = blockWidth_->value(); e.blockMaxLines = blockLines_->value();
    e.blockLineSpacing = blockSpacing_->value(); e.blockHangingIndent = blockIndent_->value();
    e.blockJustification = blockAlignment_->currentData().toString().at(0);
    e.code128Mode = code128Mode_->currentData().toString().at(0);
    // An imported value outside A-D has no item; leave it to validation.
    if (codabarStart_->currentIndex() >= 0) e.codabarStart = codabarStart_->currentData().toString().at(0);
    if (codabarStop_->currentIndex() >= 0) e.codabarStop = codabarStop_->currentData().toString().at(0);
    e.interpretationAbove = interpretationAbove_->isChecked();
    e.qrErrorCorrection = qrCorrection_->currentData().toString().at(0);
    e.pdf417RowHeight = pdfRowHeight_->value(); e.pdf417Columns = pdfColumns_->value(); e.pdf417Security = pdfSecurity_->value();
    emit editRequested(UpdateElementProperties{session_->document().elements[selected_].id, std::move(e)},
        QStringLiteral("Изменить свойства"));
}


void PropertiesPanel::refreshUnits() {
    const auto unit = session_->layoutSettings().unit;
    const int dpi = session_->document().dpi;
    const QSignalBlocker blocker(propertyUnits_);
    propertyUnits_->setCurrentIndex(propertyUnits_->findData(static_cast<int>(unit)));
    for (auto* control : elementForm_->findChildren<DotSpinBox*>()) {
        // X/Y are the ^FO/^FT numbers, so they stay in printer dots like the
        // dots-labelled content controls; only sizes follow the selected units.
        const bool fixedDots = control == x_ || control == y_
            || control == fontHeight_ || control == module_ || control == thickness_;
        control->setDisplayUnits(fixedDots ? CanvasUnit::Dots : unit, dpi);
    }
    selectionProperties_->setDisplayUnits(unit, dpi);
}


bool PropertiesPanel::selectionLocked() const {
    return std::ranges::any_of(selectedIndices(), [this](int index) { return session_->document().elements[index].locked; });
}


QSize PropertiesPanel::elementSize(int index) const {
    if (index < 0 || index >= session_->document().elements.size()) return {1, 1};
    const auto& element = session_->document().elements[index];
    const bool renderedSize = element.type == ElementType::Text || isBarcodeType(element.type);
    if (renderedSize && !previews_->pending()
        && previews_->requestIdentity(PreviewMode::Editor).revision == session_->revision()
        && index < previews_->editorPreview().bounds.size() && !previews_->editorPreview().bounds[index].isEmpty())
        return previews_->editorPreview().bounds[index].size();
    return {element.width, element.type == ElementType::Circle ? element.width : element.height};
}

void PropertiesPanel::focusContent() { content_->setFocus(Qt::OtherFocusReason); }

QList<int> PropertiesPanel::selectedIndices() const
{
    return selection_->indices(session_->document());
}

int PropertiesPanel::selectedIndex() const
{
    const auto id = selection_->primaryId();
    const auto& elements = session_->document().elements;
    for (qsizetype i = 0; i < elements.size(); ++i)
        if (elements[i].id == id) return static_cast<int>(i);
    return -1;
}

} // namespace Designer
